# Cimba Code Review — Release Candidate 1, Fresh Full Pass

**Subject:** Cimba — multithreaded discrete event simulation library in C
**Version reviewed:** 3.0.0-RC1, `main` branch snapshot (commit `4541f0a`, 23 August 2026)
**Reviewer:** Claude (Anthropic), AI-assisted code review requested by the maintainer
**Date:** 24 August 2026
**Review environment:** Ubuntu 24.04 x86-64, GCC 13.3, Meson 1.11, NASM 2.16, single-vCPU container. Linux paths exercised at runtime; Windows port and CUDA tutorials not exercised.

## Scope and method

This is the seventh review in the series and the first *full* review since 6 July. The 20 August verification review closed every previously reported finding against snapshot `e9094f2` and found no outstanding barrier to 3.0.0. Since then, `git diff e9094f2..HEAD` touches only `README.md`, `docs/`, `code_reviews/`, and two tutorials (`tut_4_2.c`, `tut_4_3.c`); **the library source under `src/` and `include/` is byte-identical to the verified snapshot**. The status of all previously reported defects is therefore *fixed, by source identity*, and this review does not re-litigate them.

Instead, this pass had two goals: (1) re-establish the build-and-test baseline on the current tree, and (2) take a fresh adversarial pass over the territory the earlier reviews touched least. The June–August cycle concentrated, for good reason, on the process-interaction engine — wakeups, teardown, coroutines, the hash-heap. The layers that make Cimba a *simulation analysis* tool rather than just an engine — the random-variate generators, the statistics accumulators (`cmb_dataset`, `cmb_datasummary`, `cmb_wtdsummary`, `cmb_timeseries`), the diagnostic output functions (ACF/PACF, correlograms, reports) — and the newest RC1 machinery (memory registry, pattern-cancel scratch buffers, the abandonment path) had never received the same treatment. They received it here.

The review was build-and-run based. The tree was built in release and debug+ASan/UBSan configurations. **All 24 release tests pass; all 18 test targets pass under ASan/UBSan, including `test_cimba` run to completion (458 s) and the stochastic runner.** Five reproduction programs were written and executed against the release and sanitizer builds, and the two profile-guided `benchopt` configurations were exercised. Findings marked **[verified]** were demonstrated by running code; findings marked **[by inspection]** follow from reading the source. Two supporting mathematical claims (the validity boundary of the gamma squeeze, and the exactness of the Marsaglia–Tsang recursion for shape > 1/3) were checked numerically outside the library before the corresponding empirical tests were run against it.

## Executive summary

The engine is in the strong shape the August verification found it in — unchanged, and re-confirmed here by a clean full-suite sanitizer run. The build, test, and packaging infrastructure remains a model of its kind. The documentation, tutorials, and comment discipline are well above the norm for a solo open-source project.

The fresh pass, however, found **five new verified defects — none in the event/process engine, all in the supporting layers** that this cycle examined for the first time:

- **R1 (HIGH, memory safety):** the thread-local scratch buffer used by both `cmb_event_pattern_cancel` and `cmi_hashheap_pattern_cancel` is allocated with a size in *items* passed to an allocator that takes *bytes* — an 8× undersize. Any pattern cancel matching more than one eighth of the heap capacity writes off the end of the allocation. Demonstrated as an ASan heap-buffer-overflow through the public API.
- **R2 (HIGH, statistical correctness):** `cmb_random_std_gamma` is incorrect across most of its documented domain `shape > 0`. For `shape ≤ 1/3` it silently returns NaN in release builds; for `1/3 < shape ≲ 0.53` it produces a measurably biased distribution because the Marsaglia–Tsang squeeze constant is applied outside its region of validity. `cmb_random_std_beta` and `cmb_random_beta` inherit both failure modes for any shape parameter below 1 — including `Beta(0.5, 0.5)`, which the project's own test suite samples and passes, revealing that the moment-based quality tests cannot see this class of error.
- **R3 (MEDIUM-HIGH):** `cmb_dataset_ACF` divides the autocovariance and the variance by different denominators, so |acf| can exceed 1 on perfectly legitimate data — silently corrupting the values in release builds, and **aborting a release build** via a release assert when the same data is passed to `cmb_dataset_correlogram_print`.
- **R4 (MEDIUM):** `cmb_timeseries_finalize` on an empty timeseries — a monitored quantity that never changed during a trial — dereferences `NULL[UINT64_MAX]` and segfaults the release build. It is the sharpest instance of a systematic pattern: the statistics layer's empty-data edges are unhandled.
- **R5 (MEDIUM, build):** both profile-guided configurations (`-Dbenchopt=generate` and `-Dbenchopt=use`) fail to link. A macro used on the new abandonment path expands to a helper that is only defined when `NASSERT` is *not* set. The library's maximum-performance configuration cannot currently be built at all.

Every one of these fixes is small and local, and none touches the engine. But R1 is a heap overflow reachable from a public API, and R2 puts silently wrong numbers into the hands of exactly the researchers the library is written for. **I would treat R1, R2, R3, and R5 as gates for 3.0.0.** R4 is a one-line guard that should ride along. Given how far the engine hardening has come, it would be a shame to ship it wrapped in a statistics layer that hasn't had the same scrutiny — this review is the start of that scrutiny, and the medium/low section below sketches the systematic "empty and degenerate input" hardening pass that would finish it.

## Baseline verification

- Release build: compiles clean at `warning_level=3`, 24/24 tests pass (`test_cimba` 46 s on one vCPU).
- Debug + ASan/UBSan build: 18/18 test targets pass, including `test_cimba` to completion (458 s) and `stochastic` (11 s). No sanitizer findings from the suite.
- `meson install` staging and header self-containment were not re-verified this round (unchanged since the 20 August review, which verified them).
- ThreadSanitizer, Windows/MSYS2, and CUDA configurations were not exercised in this container; the project's CI covers the first two.

## New verified defects

### R1 — Pattern-cancel scratch buffer sized in items, allocated in bytes: heap buffer overflow (HIGH) [verified]

The RC1 campaign replaced per-call allocation in the pattern-cancel functions with a thread-local scratch buffer that grows to the heap capacity. Both implementations size it like this (`src/cmb_event.c:549–553`; identically `src/cmi_hashheap.c:857–863`):

```c
const uint64_t hsz = event_queue->heap_size;    /* heap_size = "Max number of items" */
if (hsz > match_buf_size) {
    match_buf = (uint64_t*)cmi_realloc(match_buf, hsz);   /* <-- hsz BYTES */
    match_buf_size = hsz;
}
```

`heap_size` is documented in `cmi_hashheap.h:104` as the **maximum number of items**, but `cmi_realloc` takes a byte count. The buffer therefore holds `heap_size / 8` keys while the first pass records up to `heap_count` keys (`match_buf[cnt++]`, `cmb_event.c:570` / `cmi_hashheap.c:870`). **Whenever a pattern matches more than one eighth of the heap capacity, the recording pass writes up to 8× past the end of the allocation.**

*Repro (provided, `pc_repro.c`):* initialize the event queue, schedule 100 events sharing one action function (the heap grows to 128 slots), and call `cmb_event_pattern_cancel(my_event, CMB_ANY_SUBJECT, CMB_ANY_OBJECT)` — a natural modeling operation ("cancel all pending arrivals at shutdown"). Under ASan:

```
ERROR: AddressSanitizer: heap-buffer-overflow ...
WRITE of size 8 at 0x50c0000000c0 thread T0
    #0 cmb_event_pattern_cancel src/cmb_event.c:570
0x50c0000000c0 is located 0 bytes after 128-byte region
    allocated by ... cmi_realloc src/cmb_event.c:552
```

In a release build the same call silently corrupts the heap — with 100 matches, roughly 700 bytes of adjacent allocations are overwritten with event keys.

Why the clean sanitizer suite never caught it: the library's own ten-odd internal sweep sites (`cmb_event.c:360–361`, `cmb_process.c:220–222/922–923`, `cmb_resource.c:378`, `cmb_resourceguard.c:236–237`, and the pool/buffer/condition equivalents) all cancel per-process or per-object wakeups — a handful of matches per call, comfortably under the 1/8 threshold once the shared buffer has grown past a trivial size. The overflow needs either a user-level mass cancel or a single object accounting for more than an eighth of all scheduled events at teardown. The test suite does neither.

**Fix:** at both sites, `cmi_realloc(match_buf, hsz * sizeof *match_buf)`. `match_buf_size` then consistently counts items, as the comparison already assumes. Side effects: none functional; the scratch buffer grows to 8 bytes per heap slot (still trivial, and freed by the existing `*_thread_cleanup` paths, which I confirmed free both buffers). Two follow-ups: (a) add a unit test that mass-cancels more than `capacity/8` events — it fails instantly under ASan against the current code; (b) the deferred consolidation of the per-teardown sweeps into one pass (noted in the August discussions) would route *more* matches through a single call, so land this fix first.

### R2 — `cmb_random_std_gamma` is wrong for `shape < 1`; `std_beta` inherits it (HIGH) [verified]

`cmb_random_std_gamma` (`src/cmb_random.c:466–505`) implements Marsaglia–Tsang (2000) and its public documentation (`include/cmb_random.h:437`) states the domain `shape > 0`, guarded by `cmb_assert_release(shape > 0.0)`. The algorithm as implemented is only correct for `shape ≳ 0.53`, and fails in two distinct ways below that:

**(a) `shape ≤ 1/3`: silent NaN in release builds.** `d = shape − 1/3 ≤ 0`, so `c = 1/sqrt(9d)` is NaN (line 475). The candidate `v = 1 + c·x` is NaN, the `while (v <= 0.0)` rejection test is false for NaN, and the first squeeze test `u < 1 − 0.331x⁴` — computed from a *real* normal sample — accepts with high probability, returning `d·w` = NaN. *Verified:* `cmb_random_std_gamma(0.25)` returns `-nan` in the release build. The debug build catches it at the `cmb_assert_debug(ret >= 0.0)` postcondition; the release build hands NaN to the model. (Downstream containment is partial: a NaN reaching `cmb_process_hold` trips its `dur >= 0.0` release assert, but a NaN flowing into statistics, comparisons, or a `NASSERT` build propagates silently.)

**(b) `1/3 < shape ≲ 0.53`: biased distribution, silent in every build.** The Marsaglia–Tsang acceptance recursion itself is an exact rejection sampler for all `d > 0` — I verified numerically that the acceptance ratio `exp(x²/2 + d(1 − v + ln v))` stays ≤ 1 throughout, and the transformed proposal density works out to exactly `t^(α−1)e^(−t)`. The defect is the *fast-path squeeze* `u < 1 − 0.331x⁴` (line 488), a lower bound Marsaglia and Tsang derived for `α ≥ 1`. Below `α ≈ 0.5265` (numerically determined boundary) the squeeze **exceeds** the true acceptance ratio near `v → 0` — by up to 0.86 at `α = 0.4` — so candidates near zero are accepted far too often, piling spurious probability mass into the left tail.

*Verified empirically*, 20 M samples per case against the exact CDF:

| shape | quantile q | empirical P(X≤q) | exact | rel. error |
|---|---|---|---|---|
| 0.4 | 10⁻⁴ | 0.033828 | 0.028310 | **+19.5 %** |
| 0.4 | 10⁻³ | 0.077299 | 0.071092 | **+8.7 %** |
| 0.4 | 10⁻² | 0.183619 | 0.178118 | **+3.1 %** |
| 0.5 | 10⁻³ | 0.036017 | 0.035671 | **+0.97 %** (≈ 8σ at this N) |

**Propagation.** `cmb_random_gamma` is *correct* — it already applies the standard `Γ(α+1)·U^(1/α)` boost for `shape < 1` (`cmb_random.h:459–471`). But `cmb_random_std_beta` (`cmb_random.h:484–496`) calls `cmb_random_std_gamma` directly, so **every beta draw with `a < 1` or `b < 1` inherits the defect**: biased for shape in (1/3, ~0.53), NaN at or below 1/3. U-shaped and J-shaped betas — `Beta(0.5, 0.5)`, Jeffreys-prior-like shapes — are bread and butter in simulation input modeling. Notably, `test_random.c:1011–1012` samples `Beta(0.5, 2.0)` and `Beta(0.5, 0.5, 2, 5)` and **passes**: the mean/variance tolerances cannot detect a sub-percent distortion concentrated near a boundary. That is a test-design lesson worth keeping.

**Fix:** move the `shape < 1` boost from `cmb_random_gamma` down into `cmb_random_std_gamma` itself:

```c
double cmb_random_std_gamma(const double shape)
{
    cmb_assert_release(shape > 0.0);
    if (shape < 1.0) {
        return cmb_random_std_gamma(shape + 1.0)
               * pow(cmb_random(), 1.0 / shape);
    }
    /* ... existing Marsaglia–Tsang body, now only ever sees shape >= 1 ... */
}
```

(or the iterative equivalent, if you prefer to avoid the self-call in coroutine stack frames — it recurses exactly once). Side effects to be aware of:

- `cmb_random_std_beta` and `cmb_random_beta` are `static inline` header functions calling the `extern` `std_gamma`, so the fix repairs beta at the next *link*, not only at recompile.
- `cmb_random_gamma`'s own `shape < 1` branch becomes redundant but stays correct (its `shape + 1 ≥ 1` call takes the plain path). Simplify it to `scale * cmb_random_std_gamma(shape)` at leisure; leaving it costs one extra uniform draw and one `pow` per small-shape sample relative to the simplified form — no correctness issue either way.
- The random-stream consumption pattern changes for `shape < 1`, so **seeded runs using small-shape gamma or beta will reproduce differently** — necessarily, since the old stream was biased. Any golden values in the stochastic tests that touch these parameters need re-baselining.
- The thread-local `a_prev/c/d` cache keys on `shape`, which after the fix is always ≥ 1 inside the M-T body — no cache-coherence subtlety is introduced.
- Add small-quantile CDF checks (say P(X ≤ q) at q = 10⁻³, 10⁻² against closed forms — `erf(sqrt(q))` for shape ½ is free) for shapes 0.25, 0.4, 0.5 to `test_random`; these fail loudly against the current code and pin the fix.

Also fix the copy-paste in the docstring at `cmb_random.h:437`: "Equal to `cmb_random_std_gamma(shape, 1.0)`" should read `cmb_random_gamma(shape, 1.0)`.

### R3 — ACF divisor mismatch lets |acf| exceed 1; correlogram printing then aborts a release build (MEDIUM-HIGH) [verified]

`cmb_dataset_ACF` (`src/cmb_dataset.c:699–745`) computes the lag-k autocovariance divided by `count − lag` (line 740) but normalizes by a variance divided by `count − 1`. The two denominators differ, so the ratio is not bounded by 1: only the standard "biased" estimator — the *same* full-sample sum of squares in both numerator and denominator — carries the Cauchy–Schwarz guarantee |r_k| ≤ 1 (it is also what Box–Jenkins practice and every mainstream statistics package computes, precisely because the r_k then form a positive-semidefinite sequence).

*Verified:* a 10-point ramp `0,1,…,9` — as legitimate as data gets — yields `acf[7..9]` = −1.26, −1.72, **−2.21** in the release build, silently. In a debug build, the library's own `cmb_assert_debug((acf[ulag] >= -1.0) && (acf[ulag] <= 1.0))` (line 742) aborts on the same call — the assert is right and the estimator is wrong. Worse, `data_bar_print` (`cmb_dataset.c:822`) checks the same range with a **release** assert, so *(verified)* `cmb_dataset_correlogram_print` on the same ten numbers prints six bars and then **aborts the default release build** (SIGABRT). With `-DNASSERT` the abort becomes a `uint16_t` underflow in the bar-width arithmetic and ~65 000 characters of garbage output. Downstream, `cmb_dataset_PACF` consumes the invalid sequence; its Durbin–Levinson denominator `1 − densum` loses its positivity guarantee and the recursion can blow up.

**Fix:** compute the biased estimator — accumulate the full-sample centered sum of squares once (it is `m2` from the existing one-pass loop) and use `acf[k] = dk / m2`. Side effects: acf values shift slightly for *all* users (toward the textbook-standard values; anyone comparing against R/Python will see agreement improve); both asserts become genuinely valid invariants and should stay; the near-constant-data guard (`var < 1e-9`) keeps working unchanged. While in the neighborhood, two PACF notes below.

### R4 — `cmb_timeseries_finalize` on an empty timeseries: NULL wild read, release-build segfault (MEDIUM) [verified]

`src/cmb_timeseries.c:180–195`: the entry release assert explicitly *permits* `n == 0` — `(n == 0u) || ((tsp->ta != NULL) && ...)` — and the very next statement reads `dsp->xa[n − 1u]`, i.e. `NULL[UINT64_MAX]` (line 189). *Verified:* SIGSEGV in the release build; under UBSan, "applying non-zero offset 18446744073709551608 to null pointer" followed by the ASan SEGV report. The triggering pattern is mundane: a monitored quantity that never changed during a trial (an empty queue, an idle machine, a zero-arrival replication), finalized unconditionally at end-of-run — exactly what a batch experiment harness does.

**Fix:** guard `n == 0` with the same warn-and-return treatment `cmb_dataset_median` already gives empty input. Side effect to handle at the same time: after an empty finalize returns 0, a model may go on to call `cmb_timeseries_summarize`, which on an empty series currently aborts via its `ta != NULL` release assert *before* reaching its own `un − 1u` underflow (`cmb_timeseries.c:225–229`) — decide one policy (warn + zero-sample summary seems right, matching the "N 0" print path) and apply it to both, or the fix to finalize just moves the failure one call later.

### R5 — Both `benchopt` (PGO) configurations fail to link (MEDIUM) [verified]

`src/cmb_logger.c` defines the recursion-safe `do_assert` helper *inside* `#ifndef NASSERT` (lines 41–56), but defines `logger_assert_always(x)` as `do_assert(x)` unconditionally (line 70), and the RC1 abandonment work added two uses on always-on paths: `cmi_logger_error`'s teardown re-entry guard (line 260) and its not-reached terminator (line 273). With `-DNASSERT` — which both `benchopt_flags` and `benchopt_gen_flags` set in `meson.build` — `do_assert` is an implicit function declaration and the link fails:

```
ld: in function `cmi_logger_error': undefined reference to `do_assert'
FAILED: src/libcimba.so.3.0.0
```

*Verified* with `meson setup --buildtype=release -Dbenchopt=generate` on a clean tree. The "no-holds-barred" configuration the build system advertises — the one the benchmarks and the events/second/core comparisons presumably want — cannot currently be built. This is almost certainly a regression from the abandonment campaign (the two call sites are new), unnoticed because CI builds neither benchopt mode.

**Fix:** move the `do_assert` definition above and outside the `#ifndef NASSERT` block so `logger_assert_always` always has it (that is the semantic the name promises — these two checks *should* survive NASSERT, and after the fix they do, satisfying the `CMB_NORETURN` contract on `cmi_logger_error` as a bonus — the NASSERT build currently also warns "'noreturn' function does return"). Side effects: none for existing working configurations; benchopt builds gain two live aborts on paths that must never execute. Add a CI job that *compiles* `-Dbenchopt=generate` (no profile data needed to build it); that permanently covers the NASSERT code paths the same way `verify_install.sh` covers packaging.

## Additional findings (medium-low and low)

**A1 — Merging two empty summaries NaN-poisons the accumulator [by inspection].** `cmb_datasummary_merge` computes `d21 / n` with `n = 0` when both sources are empty (`cmb_datasummary.c:148`) — 0/0 = NaN into `m1…m4`; every subsequent `cmb_datasummary_add` then stays NaN (`d = y − NaN`). `cmb_wtdsummary_merge` has the identical hazard via `d21 / ws` with `ws = 0` (`cmb_wtdsummary.c:215`). The realistic route is reducing per-thread or per-trial summaries where some contributed nothing. One guard — if the combined count (or weight) is zero, write zero moments — closes both. The count>0 getters already protect the *read* side; it is the write-then-add sequence that poisons.

**A2 — `cmb_dataset_copy(d, d)` is a use-after-free [by inspection].** Self-copy frees `tgt->xa` and then reads `src->xa` — the same freed block (`cmb_dataset.c:299–331`). This deserves attention mostly because `cmb_dataset_merge` *does* handle aliasing and documents its reasoning carefully — the asymmetry is a trap. Either apply merge's local-then-swap discipline or `cmb_assert_release(tgt != src)`. (`cmb_timeseries_copy` deserves the same check.)

**A3 — Empty-data edges are systematically unhandled across the statistics layer [by inspection, one abort verified adjacent].** Beyond R4: `cmb_priorityqueue_report_print` on a queue whose recording was never started aborts on `cmb_timeseries_summarize`'s `ta != NULL` release assert; on a started-but-never-changed queue it reaches `qmax = cmb_wtdsummary_max()` of a zero-sample summary = `-DBL_MAX`, and `(uint32_t)(qmax + 1.0)` is UB (`cmb_priorityqueue.c:214–231`). `cmb_dataset_median` and `fivenum` handle empties gracefully with warnings — that is the right policy; one sweep applying it uniformly (finalize, summarize, report_print, histogram ranges) plus an "empty objects" unit test calling every public statistics entry point on freshly initialized objects would close the family permanently.

**A4 — `unsigned` truncation of 64-bit counts in quantile helpers [by inspection].** `data_array_median(const unsigned n, ...)` (`cmb_dataset.c:411`) receives `dtmp.count` (uint64_t), and `fivenum` computes `const unsigned lhsz = dtmp.count / 2`. Datasets beyond 2³² samples (34 GB — unlikely but not impossible in a long campaign) silently compute quantiles over `count mod 2³²` elements. Widen to `uint64_t` for type hygiene.

**A5 — Latent underflow in `cmb_dataset_sort` [by inspection].** With `count == 0` but `xa != NULL`, `for (uint64_t ui = un − 1u; ui > 0u; ...)` starts at UINT64_MAX and the first swap writes wild. I could not construct that state through the current public API (terminate/reset free the array; copy from an empty source leaves `xa` NULL), so it is latent — but it is one `if (un < 2) return;` away from being impossible rather than unreached, and A2's fix touches the same neighborhood.

**A6 — PACF: stride bug that is currently harmless, and two robustness notes [by inspection].** (a) `phi[ui] = phi[0] + ui * n` (`cmb_dataset.c:775`) lays rows out with stride `n` into a matrix allocated for stride `n + 1`. Adjacent rows overlap by one element; the recursion never *uses* the overlapping cells only because column index never exceeds row index. Correct today by accident — change it to `ui * (n + 1u)` before some future edit makes it a real corruption. (b) The `1 − densum` denominator has no guard; with R3 fixed the ACF sequence becomes PSD and the exposure shrinks, but a tiny-denominator check would make it robust to near-unit-root data. (c) The `pacf ∈ [−1, 1]` debug assert can fire on legitimate borderline data for the same reason and should become a clamp-with-warning or be dropped once (b) exists.

**A7 — Registry teardown calls through a cast function-pointer type [by inspection].** `(cmi_teardown_func *)cmb_X_destroy` and the call `(*item->teardown)(item->object)` invoke a `void (struct X *)` through a `void (void *)` type — formally UB, fine on the targeted ABIs (the code says so, honestly), but it will trip Clang CFI and `-fsanitize=function` should you ever enable them. A per-type one-line wrapper, or making the real signatures take `void *`, removes the caveat. Low priority; noted for the portability ledger alongside the existing function↔object pointer cast in pattern-cancel.

**A8 — Small distribution nits [by inspection].** `cmb_random_triangular(a, a, a)` returns `max` but the debug postcondition demands `x < max` (and `x` can also round to `max` when `1 − u` underflows against a large-magnitude `max`) — make the assert `<=`. In the Vose alias sampler, leftover entries get `uprob = UINT64_MAX` but their `alias[]` stays 0; the `>=` comparison then redirects to index 0 with probability 2⁻⁶⁴ per draw — set `alias[i] = i` in the two leftover loops (or use `>`), free of cost and exact. `cmb_random_geometric` can in principle return 0 if the exponential sample is exactly 0.0 (probability ~2⁻⁶⁴); `x = ceil(...)` followed by `x += (x == 0)` is exact.

**A9 — Normal-trial leak detection interacts oddly with intentional cross-trial objects [by inspection].** On a *normal* trial exit with a non-empty registry, `thread_worker_func` warns "Memory leak detected" and unlinks the entries without tearing them down (`cimba.c:300–312`). Reasonable — but a model that deliberately keeps `cmb_` objects across trials in its thread context will be warned every trial, and those objects silently lose abandonment protection for the rest of the run. If cross-trial objects are meant to be supported, that deserves a documented idiom; if not, the warning text could say what the user should have done.

## Style and maintainability

The codebase remains a pleasure to read: strict `cmb_`/`cmi_`/`cmg_` namespace discipline, per-file rationale comments that explain *why* (the CET property note in the NASM sources, the `__builtin_setjmp` design notes, the logger's recursion-avoiding local asserts, merge's aliasing analysis are exemplary), a uniform four-phase object lifecycle with the `cookie ⟺ registered` invariant, and asserts dense enough to make most latent errors loud in debug builds. Small items collected in passing: `sum_tolerance` (`cmb_random.c`) is a mutable file-scope static that is only ever read — make it `const` (as-is it is also a benign shared non-atomic across threads); stray double semicolons (`cmb_dataset.c` bar printer, `cimba.c:305`); a dead `hp == NULL` test after taking a member address (`cmb_priorityqueue.c:337`); `cimba_threads_use` stores atomically but re-reads non-atomically two lines later — harmless, but inconsistent with the discipline used elsewhere; duplicate `extern void cmi_event_thread_cleanup(void);` declaration (`cimba.c:82/84`).

## Performance notes

No regressions were measured; the engine hot paths are unchanged from the snapshot whose benchmarks the README reports, and the PGO configuration could not be built this round (R5) — re-run the events/second/core numbers once it links again. Two documentation-or-upgrade items in the variate layer: `cmb_random_poisson` simulates arrival-by-arrival, O(r) exponentials per sample — fine for the small rates typical of DES time steps, but a user drawing Poisson(10⁶) counts will not enjoy it; `cmb_random_binomial` is likewise O(n) Bernoulli flips. Either document the intended parameter regime (consistent with the hyperexponential docstring, which already does this well) or add PTRD/BTPE-style O(1) samplers later. `cmb_dataset_median`/`fivenum` copy and heapsort per call — worth one docstring line so nobody puts them in a per-event loop. The pattern-cancel scratch-buffer design itself is sound (and after R1's fix, correctly sized): O(heap) scan, zero steady-state allocation, freed at thread exit.

## Recommendations

1. **Gate 3.0.0 on R1, R2, R3, R5.** All four fixes are local: two `* sizeof *match_buf`, a five-line boost in `std_gamma`, one denominator in ACF, one macro moved out of an `#ifndef`. R4 is a one-line guard that should ride along.
2. **Add the tests these defects earned:** a mass pattern-cancel exceeding capacity/8 (fails under ASan today); small-quantile CDF checks for gamma/beta at shapes 0.25/0.4/0.5 (fail today); an ACF |r| ≤ 1 property test on trending data (aborts today); an "empty objects" sweep over every public statistics entry point; a CI compile job for `-Dbenchopt=generate`.
3. **Schedule the empty/degenerate-input hardening pass (A1–A5)** as the statistics layer's equivalent of the engine's teardown campaign — same philosophy, much smaller scope.
4. After R2 lands, **re-baseline any seeded stochastic expectations** involving `shape < 1` gamma/beta draws, and re-run the benchmark suite once benchopt links.

## Closing assessment

The pattern across seven reviews is consistent: each adversarial pass into previously unexamined territory finds real defects, and each fix campaign closes not just the instances but the class. The engine went through that fire between June and August and came out genuinely solid — this review's clean full-suite ASan run on the identical source is one more confirmation. The statistics and variate layers had simply not taken their turn yet; today they did, and the findings are exactly the kind that matter for the library's stated mission — silent numerical bias and a heap overflow are the two failure modes a research-infrastructure library can least afford. They are also, without exception, small fixes. Close R1–R5, add the tests that would have caught them, and 3.0.0 stands on ground as firm as the engine's.

---

*Reproduction programs (`pc_repro.c`, `repro_gamma.c`, `acf_repro.c` ×2, `ts_repro.c`) and the numerical squeeze-validity analysis are available on request; all were executed against release and debug+ASan/UBSan builds of commit `4541f0a` as described above.*
