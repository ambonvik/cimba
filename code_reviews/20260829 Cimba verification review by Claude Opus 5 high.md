# Cimba Verification Review — Closing the RC1 Review Cycle

**Subject:** Cimba — multithreaded discrete event simulation library in C
**Version reviewed:** 3.0.0, `main` at commit `35a5dcf` (29 August 2026)
**Baseline reviewed against:** `4541f0a` (23 August 2026), the snapshot assessed in the 24 August full review
**Reviewer:** Claude (Anthropic), AI-assisted verification requested by the maintainer
**Date:** 29 August 2026
**Environment:** Ubuntu 24.04 x86-64, GCC 13.3, Meson 1.12, NASM 2.16, single-vCPU container. Linux paths exercised at runtime; Windows/MSYS2, ThreadSanitizer and CUDA configurations not exercised here (CI covers the first two).

## Purpose and method

This document closes the review cycle opened by the 24 August full review, which reported five gating defects (R1–R5) and nine secondary findings (A1–A9). Thirty-eight commits landed between the two snapshots. Every finding was re-checked against the pushed source, and each defect that had an executable reproduction in the original review was re-run against a fresh build of the current tree rather than confirmed by reading the diff.

Verification was build-and-run based throughout. The tree was built in release, sanitizer (ASan + UBSan) and profile-guided configurations; the reproduction programs from the original review were rebuilt and re-executed; and the statistical defects were re-measured against the same exact-CDF references used to find them.

**Result: all five gating defects are fixed and verified. Eight of the nine secondary findings are fixed and verified; the ninth (A7) is consciously declined with sound reasoning. No regressions were found in the reviewed areas.** Two defects introduced during the fix campaign were caught and corrected during this verification, both by execution rather than inspection.

## Verification status

### Build and test baseline

| Configuration | Result |
|---|---|
| Release, `warning_level=3` | Compiles clean, zero warnings |
| Release test suite | **24/24 pass** |
| Debug + ASan/UBSan, per-module | **16/16 pass** |
| Debug + ASan/UBSan, `test_cimba` end-to-end | **PASS** (466.8 s), no sanitizer findings |
| Debug + ASan/UBSan, `stochastic` | **PASS** (12.5 s) |
| `-Dbenchopt=generate` (PGO) | **Links successfully** — was a hard link failure at baseline |

### Gating defects

**R1 — pattern-cancel scratch buffer heap overflow (HIGH) — FIXED, VERIFIED BY EXECUTION.**
Both sites now read `cmi_realloc(match_buf, hsz * sizeof(*match_buf))` (`cmb_event.c:552`, `cmi_hashheap.c:861`). The original reproduction — schedule 100 events sharing one action, then `cmb_event_pattern_cancel` with wildcards — previously produced an ASan heap-buffer-overflow with an 8-byte write past a 128-byte region. It now completes cleanly under ASan: *"cancelled 100 events, survived"*, no diagnostic.

**R2 — `cmb_random_std_gamma` incorrect for shape < 1 (HIGH) — FIXED, VERIFIED BY MEASUREMENT.**
The Marsaglia–Tsang boost now happens inside `std_gamma` itself, with the exponent taken from the original shape and the uniform drawn as `1.0 - cmb_random()` so a zero sample cannot be produced. Re-measured against exact CDFs at 20 M samples per case:

| shape | quantile | before | after |
|---|---|---|---|
| 0.5 | 10⁻³ | +0.97 % | **+0.03 %** |
| 0.4 | 10⁻⁴ | +19.5 % | **+0.14 %** |
| 0.4 | 10⁻³ | +8.7 % | **−0.02 %** |
| 0.4 | 10⁻² | +3.1 % | **−0.01 %** |

`cmb_random_std_gamma(0.25)`, which previously returned a silent `-nan` in release builds, now returns a valid variate. `cmb_random_std_beta` inherits the fix through the same `extern` call, and `cmb_random_gamma`'s now-redundant branch has been simplified away.

**R3 — ACF divisor mismatch (MEDIUM-HIGH) — FIXED, VERIFIED BY EXECUTION.**
`cmb_dataset.c:858` now computes `acf[ulag] = dk / m2`, the standard biased estimator with the same full-sample sum of squares on both sides. The reproduction that previously aborted a release build via `data_bar_print`'s range assert (exit 134 after printing six bars) now prints a complete correlogram with every value inside [−1, 1]. Independently confirmed on a noiseless sine of period 12, where the computed ACF matches the exact cosine values to three decimals (0.8651 vs 0.8660, 0.4995 vs 0.5000, 0.0017 vs 0.0000, −0.4945 vs −0.5000; the residuals are the expected finite-sample end effects of the biased estimator).

**R4 — `cmb_timeseries_finalize` NULL wild read (MEDIUM) — FIXED, VERIFIED BY EXECUTION.**
The empty case is guarded before any array access. The reproduction that previously segfaulted (exit 139) now logs *"Finalizing empty time series, nothing added"* and returns 0. The paired `cmb_timeseries_summarize` gap identified alongside R4 has also been closed, and the `ta[n-1] <= t` precondition was promoted to a release assert since it validates caller input.

**R5 — profile-guided configurations fail to link (MEDIUM) — FIXED, VERIFIED BY BUILD.**
`do_assert` is now defined unconditionally, above the `#ifndef NASSERT` block, so `logger_assert_always` resolves in `NASSERT` builds. `-Dbenchopt=generate` configures and links successfully; at baseline it failed with `undefined reference to do_assert`.

### Secondary findings

| # | Finding | Status |
|---|---|---|
| A1 | `cmb_datasummary_merge` NaN-poisons on two empty sources | **Fixed** — `count > 0` guard; zero-moment else branch |
| A2 | `cmb_dataset_copy(d, d)` use-after-free | **Fixed** — `cmb_assert_release(tgt != src)` in both dataset and timeseries |
| A3 | Empty-data edges unhandled across the statistics layer | **Fixed** — systematic sweep; 39 warn/info paths across dataset, timeseries and priorityqueue; new empty-object tests added |
| A4 | `unsigned` truncation of 64-bit counts in quantile helpers | **Fixed** — `data_array_median` takes `uint64_t`; `lhsz`/`uhsz` widened |
| A5 | Latent underflow in `cmb_dataset_sort` | **Fixed** — `count == 0` guard with warning |
| A6 | PACF stride bug; unguarded `1 - densum` | **Fixed** — stride now `ui * (n + 1u)`; Durbin–Levinson prediction variance maintained recursively with an exhaustion guard replacing the blunt asserts |
| A7 | Registry teardown through a cast function-pointer type | **Declined, with reasoning** — see below |
| A8 | Alias-table leftover entries; small distribution nits | **Fixed** — `alias[i] = i` in both leftover loops; triangular assert relaxed to `<=`; geometric zero-guard added |
| A9 | Per-trial leak warning vs. intentional cross-trial objects | **Fixed** — aggregated counters reported once per `cimba_run`, debug-only |

**A7 is declined on a sound argument.** The proposed remedy — making destructors take `void *` — would remove the last vestige of type checking on this path, allowing any object to reach any destroy function with no diagnostic. Since Clang CFI and `-fsanitize=function` are not planned, the cast remains well-defined on every targeted ABI, and the existing comment documents the caveat honestly. Accepting a small, documented portability note in exchange for retaining compile-time type discrimination is the better trade. The finding is correctly closed as "will not fix" rather than left open.

## Defects introduced during the campaign, caught during verification

Two defects entered with the fixes and were found by this verification. Both are worth recording because of how they hid.

**A duplicate PACF loop.** The rewritten Durbin–Levinson implementation was added but the original loop was not deleted, so it ran immediately afterwards and overwrote every result. All three improvements were inert: the blunt `densum < 1.0` assert was back, the clamp was bypassed, and the variance-exhaustion path was defeated — the new loop would `break` and fill zeros, then the old loop would overwrite them with values computed from a near-zero denominator. It hid because both loops write `phi[uk][uj]` identically and produce the same numbers on well-behaved data, so the build was clean and the tests passed. Now corrected; only one loop remains.

**A transposed assignment in the `cmb_datasummary_merge` empty path**, where `dstmp.m2` was assigned twice and `m3` never. Harmless at the time — `cmb_datasummary_initialize` had already zeroed the struct — but it would have surfaced as garbage skewness on a zero-count summary if that ever changed. Now corrected.

A third issue, a mismatch between the committed golden reference and the pushed source, was a stale-artifact problem rather than a code defect and has been resolved.

## Work beyond the review scope

The maintainer went considerably past the reported findings. These were verified to the same standard where they were checked, though they were not part of the original review and have not had an independent adversarial pass:

- **Poisson**: replaced the O(r) exponential-sum sampler with chop-down inversion below r = 17 and Hörmann's PTRD above, plus a continuity-corrected normal approximation above r = 10¹⁰, where the normal is measurably *more* accurate than PTRD's own log-density evaluation. Constants taken from Hörmann's 1992 preprint directly.
- **Binomial**: chop-down inversion below np = 30, Hörmann's BTRD above, with the `p > 0.5` symmetry fold and `exp(n·log1p(-p))` to avoid the cancellation that would otherwise make small `p` return zero. Two transcription errors caught during development by chi-square against the exact pmf (2787 vs 46 on 47 bins), while the sample mean stayed correct throughout.
- **Negative binomial**: now O(1) in `m` via the Poisson–gamma mixture, replacing an O(m) geometric loop.
- **Erlang, geometric**: geometric gained `log1p` and a saturating cast; both had domain limits documented.
- **`cmb_random()`**: the `ldexp` call — which GCC cannot fold under default `errno` semantics, and which was compiled into *user* code as a `static inline` in a public header — was replaced with an exactly equivalent multiply. Measured 3.1× faster on the uniform itself and 15–50 % on everything derived from it.
- **Exponential ziggurat**: converted from unsigned to signed 63-bit conversion, aligning it with the normal ziggurat and eliminating the branch fixup that x86-64 requires for `uint64_t → double`. The two ziggurats now time within 5 % of each other, where the exponential was previously 2.5× slower.
- **Histogram harness**: integer-aligned bucketing for discrete data, round-number widths for narrow ranges, and offset labelling for data far from zero — three classes of display artifact that were each producing misleading output on correct results.
- **Constants**: the Stirling tail table, `½·log(2π)` and related values re-derived at 50 digits and checked against primary sources. One transposed table entry and several last-bit errors were found and corrected this way.

## Assessment

Every finding from the 24 August review is resolved. The two gating memory-safety and statistical-correctness defects (R1, R2) are fixed and their reproductions verified clean; the release-build aborts and crashes (R3, R4) no longer occur; the PGO configuration builds again (R5). The secondary findings are closed, with A7 declined on merit rather than overlooked.

Three observations for the record.

**The fix campaign was verified by execution, not by inspection, and this mattered.** Of the defects found in this cycle — including the two introduced during the campaign — nearly all surfaced from running code and comparing against independently computed truth, not from reading it. A duplicate loop that produces identical results on ordinary data, a wrong constant that passes chi-square because it only affects k = 7, a display artifact that makes a correct histogram look asymmetric: none of these are visible in a diff.

**The test suite has grown in the right direction but the strongest checks are not yet in it.** Empty-object tests were added, which closes the A3 family permanently. The checks that actually caught the campaign's defects — chi-square against exact pmfs, targeted tail probabilities such as `P(X = 0)` at a branch threshold, table constants verified against `lgamma` — were run ad hoc during development. Landing them in `test_random` and `test_data` would convert this cycle's hard-won knowledge into standing protection. The PACF variance-exhaustion branch in particular is currently exercised by nothing.

**One harness issue is worth addressing before it causes a false negative.** The golden reference files embed source line numbers, so any edit above a logging call fails the stochastic test for cosmetic reasons. That trains the reflex of regenerating references without reading the diff — and this cycle produced one genuine mismatch that looked exactly like the cosmetic case. Normalising line numbers in the comparison, rather than in the log output, would remove the noise without touching what users see.

The library is in materially better shape than at the start of this cycle: two silent-wrong-answer defects eliminated from the numerical core, a heap overflow closed, three classes of release-build abort removed, and a substantial performance improvement in the random-variate layer that also happens to be the one users were actually paying for. **No outstanding barrier to 3.0.0-RC2 was found in the reviewed areas.**

The customary caveat applies with more force than usual: the Poisson, binomial, ziggurat and histogram work is new code written during this cycle, verified as it was developed but never subjected to an independent adversarial review of the kind that produced R1–R5. Its defect density is unknown. A full pass over `cmb_random` and `cmb_dataset` as they now stand — treating them as new code rather than as reviewed code with fixes applied — would be the natural next step, and on this cycle's evidence would not come back empty.

---

*Verification performed against commit `35a5dcf` in release, debug + ASan/UBSan, and `benchopt=generate` configurations. Reproduction programs from the 24 August review were rebuilt and re-executed against the current tree; statistical claims were re-measured at 20 M samples per case against exact reference distributions.*
