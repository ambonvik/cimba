# Cimba Verification Review — Numerics & Statistics Fixes

**Reviewer:** Claude (Anthropic), independent automated review
**Date:** 18 June 2026
**Subject:** Cimba discrete event simulation library, version 3.0.0-beta
**Snapshot:** post-fix tree following commits `9e7626b … 7b9076d` (17 June 2026)
**Platform exercised:** Linux x86-64, GCC 13.3.0, NASM 2.16, Meson 1.11 / Ninja
**Method:** Build-and-run verification. The tree was built in release, debug,
ASan+UBSan, release+ASan, ThreadSanitizer, and benchopt configurations; the full
test suite was run (23/23 pass); and each fix was re-checked with the same
reproduction programs that demonstrated the original defects, plus targeted
regression programs. Findings marked **[verified]** were demonstrated by running
code.

---

## Purpose

This is a follow-up to the 17 June review (`20260617 Cimba code review by Claude
Opus 4.8 extra.md`), which found four defects in the numerics/statistics layer
(N1–N4) plus several lower-severity items. This review confirms, by re-running the
original reproductions against the new tree, that each has been fixed, and checks
the changes for regressions. **All four defects are resolved, the lower-severity
items are addressed, no regressions were found, and no new defects of significance
surfaced.**

---

## Status of the 17 June findings

### N1 — out-of-range index in `cmb_random_discrete_nonuniform` → release-build heap overflow — RESOLVED [verified]

The selection loop now iterates to `n-1` and lets the final bucket absorb the
remainder, exactly the segmentation/rounding-safe form recommended:

```c
for (ui = 0; ui < n - 1; ui++) { q += pa[ui]; if (x < q) break; }
return ui;                       /* always in [0, n-1] */
```

Re-running the original reproduction (`{0.333, 0.333, 0.333}`, sum 0.999, which
passes `sums_to_one`) in a **release + ASan** build over 50,000,000 draws: maximum
index 2, zero out-of-range, and the downstream `cmb_random_hyperexponential`
(`ma[ui]`) ran clean with no heap-buffer-overflow. The fix propagates correctly to
the new `cmb_random_loaded_dice(a, b, pa)`: over 30,000,000 draws on `[10,12]` with
the same 0.999-sum array, every result stayed in `[10,12]` (previously it could
return `b+1`). The `n == 1` single-outcome case returns 0 as expected. The Vose
alias sampler was also checked and stays in bounds.

### N2 — `cmb_wtdsummary` higher moments normalized by count, not weight — RESOLVED [verified]

Both halves of the fix are present. The four accessors now compute the
population-weighted moments normalized by `wsum`:

```c
variance = m2 / wsum;
stddev   = sqrt(variance);
skewness = sqrt(wsum) * m3 / pow(m2, 1.5);
kurtosis = wsum * m4 / (m2 * m2) - 3.0;
```

and `cmb_wtdsummary_print` no longer delegates to `cmb_datasummary_print`; it now
calls the weighted accessors, so the printed report — and therefore
`cmb_resource_print_report` — shows the correct figures.

Verified against NumPy on a fixed unequal-weight dataset: mean, variance,
skewness, and excess kurtosis agree to eight significant figures. The result is now
**segmentation-invariant** (splitting every interval into three sub-intervals
leaves all four moments unchanged) and **weight-scale-invariant** (multiplying all
weights by 1000 is unchanged) — the two properties whose violation was the original
bug. The printed line now reports `Variance 7.933`, not the previous count-based
`9.916`.

The new `test_wsummary()` in `test/test_data.c` pins this down: I confirmed it
passes on the fixed tree and (when compiled against the original header, since the
accessors are `inline`) traps on the old code at the first weighted-variance check.

### N3 — `cmb_random_geometric` / `cmb_random_negative_binomial` wrong at p = 1 — RESOLVED [verified]

`cmb_random_geometric` now special-cases `p == 1.0` and returns 1, and the caching
path correctly assigns `prev = p` so it no longer recomputes the logarithm on every
call. Verified: `cmb_random_geometric(1.0)` returns 1 on all of 100,000 trials;
`cmb_random_negative_binomial(3, 1.0)` returns 0 (previously ≈ `UINT64_MAX` from
unsigned underflow); and `cmb_random_geometric(0.25)` has sample mean 4.0016 over
2,000,000 draws, confirming the now-active cache still produces the correct
distribution.

### N4 — `cmb_dataset_fivenum_print` crash on small datasets — RESOLVED [verified]

The function now guards `count < 4` with a warning and only computes the
five-number summary for `count >= 4` (where the lower/upper half sizes are at least
2, so `data_array_median` is never called with `n == 0`). Verified under ASan for
counts 1–6: counts 1–3 emit a clean warning with no crash, counts 4–6 produce
correct summaries, and there are no SEGVs or heap errors.

### Lower-severity items — all addressed [verified]

- **MXCSR now preserved in the default build.** `-DNMXCSR` was moved out of the
  default flags into the `benchopt` flag sets only. Disassembling the context-switch
  object confirms `stmxcsr`/`ldmxcsr` are present in the default release build and
  absent in a `-Dbenchopt=use` build. The NASM side keys off the same
  `desired_flags`-derived set, so the C and assembly halves stay consistent (both
  preserve MXCSR by default; both skip it under benchopt). The speed/accuracy
  trade-off is now an explicit opt-in.

- **Boundary draws fixed.** `cmb_random_logistic` loops until it draws a nonzero
  uniform (no more `-inf`); over 5,000,000 draws, zero non-finite results.
  `cmb_random_bernoulli` now uses `<` rather than `<=`, so `bernoulli(0.0)` returned
  0 on all 2,000,000 trials and `bernoulli(1.0)` returned 1 on all of them.

- **Event-queue leak on the error path fixed.** A new `cmi_event_queue_reset()`
  (called from the worker-thread `longjmp` recovery path) terminates and destroys
  the thread's hash-heap so the next trial starts clean. Confirmed it frees the
  backing allocation rather than merely resetting counters.

- **Reproducibility guidance added.** `include/cimba.h` now documents the
  recommended idiom: derive each trial's seed from a master seed and the trial index
  via `cmb_random_fmix64(master_seed, trl_idx)`, store it in the trial struct, and
  seed the PRNG from it in the trial function. This makes runs reproducible
  regardless of which worker thread runs which trial — the gap noted previously.

### The ThreadSanitizer artifact — correctly handled [verified]

The 17 June review root-caused the TSan crash on the resource-contention-in-worker
path as a TSan signal-handler artifact (not a Cimba defect), correct in release and
clean under ASan. The TSan CI job now sets
`TSAN_OPTIONS: …:handle_segv=0:handle_sigbus=0`, with a commit message recording
why. Re-running the resource-contention reproduction (4 contending processes, 16
trials across worker threads) under TSan with the CI options: clean across repeated
runs — exit 0, zero data races, zero signals, correct results. Race detection
remains fully active (independently confirmed that a deliberate race is still
reported with `handle_segv=0`).

---

## Regression and new-defect check

The whole suite passes (23/23) and the library compiles clean at `warning_level=3`.
The core data-structure, process, resource, and random tests are clean under
ASan+UBSan. The changed areas — `discrete_nonuniform` and its callers, the weighted
summary and its print path, the geometric/negative-binomial path, the five-number
summary, and the build flags — were re-exercised individually and behave correctly.
No regressions were found, and no new defects of significance surfaced.

A few minor, non-blocking observations remain:

- **Test teardown hygiene.** `test_dataset()` in `test/test_data.c` still exits
  without freeing one `cmb_dataset` (a 128 KB allocation flagged by ASan leak
  detection). It is a test-cleanup omission, not a library leak — the library's free
  paths are clean — and CI runs ASan with `detect_leaks=0`, so it does not gate the
  build. Worth a `cmb_dataset_destroy` for tidiness.

- **Coverage gap persists for one path.** The resource-guard contention path running
  inside `cimba_run` worker threads is still exercised by no test (only
  `test_cimba.c` uses `cimba_run`, via the buffer path). The TSan artifact is now
  suppressed regardless, so this is low priority, but a small multithreaded
  resource-contention test would close the gap and guard the fiber resume path under
  the sanitizers.

- **Demo warnings.** `test_cimba.c` and three tutorials ignore the return value of
  `system()` (launching ParaView/gnuplot), producing `-Wunused-result` warnings.
  These are in demonstration/test code, not the library, and are harmless; casting
  to `(void)` or checking the result would silence them.

- **`cmb_random_loaded_dice` width.** `n = b - a + 1u` is still evaluated with a
  signed `b - a`, which would overflow only for near-full-width `int64` ranges — a
  pre-existing, extreme edge case noted in an earlier review, not affected by these
  changes.

---

## Verification statement

On Linux x86-64 with GCC 13.3.0, I verified by building and executing the code that
this post-fix snapshot of Cimba 3.0.0-beta builds cleanly in release, debug,
ASan+UBSan, release+ASan, ThreadSanitizer, and benchopt configurations; that the
full test suite passes (23/23); and that the library is clean under ASan+UBSan on
the core tests.

By re-running the original reproduction programs against the new tree, I confirmed
that all four previously reported defects are resolved: the out-of-range index and
release-build heap overflow in `cmb_random_discrete_nonuniform` (N1); the
count-normalized, segmentation-dependent weighted higher moments in
`cmb_wtdsummary`, including the printed-report path (N2); the incorrect results from
`cmb_random_geometric` / `cmb_random_negative_binomial` at p = 1 (N3); and the crash
in `cmb_dataset_fivenum_print` on small datasets (N4). I also confirmed the
lower-severity items are addressed — MXCSR preservation in the default build, the
logistic and Bernoulli boundary draws, the event-queue leak on the error path, and
the per-trial seeding guidance — and that the ThreadSanitizer signal-handler
artifact is correctly handled in CI without weakening race detection.

**Assessment:** the numerics and statistics layer is now solid alongside the engine,
which earlier reviews had already verified. I no longer see an outstanding
correctness or memory-safety barrier in the exercised paths to relying on this
snapshot for high-stakes work. The remaining items are minor and non-blocking: test
teardown hygiene, one untested multithreaded path, cosmetic `-Wunused-result`
warnings in demo code, and a long-standing theoretical width limit in
`cmb_random_loaded_dice`. As always for a beta, continued caution is appropriate for
the Windows port (not exercised here) and for code paths beyond the test suite.
