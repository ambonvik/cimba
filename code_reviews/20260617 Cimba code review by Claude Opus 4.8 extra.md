# Cimba Code Review — Numerics & Statistics Focus

**Reviewer:** Claude (Anthropic), independent automated review
**Date:** 17 June 2026
**Subject:** Cimba discrete event simulation library, version 3.0.0-beta
**Platform exercised:** Linux x86-64, GCC 13.3.0, NASM 2.16, Meson 1.11 / Ninja
**Method:** Source reading plus building and running code. The library was built
from source in release, debug, AddressSanitizer+UndefinedBehaviorSanitizer, and
ThreadSanitizer configurations. The full shipped test suite was run (23/23 pass).
Eleven independent minimal reproduction programs were written and run against the
release and sanitizer builds — including a controlled isolation series that
root-caused the ThreadSanitizer crash described below — and statistical output was
validated numerically against NumPy/SciPy. Findings marked **[verified]** were
demonstrated by running code; **[by inspection]** follow from reading the source.

---

## Relationship to the prior reviews

Two earlier reviews live in `code_reviews/` (10 June and a 15 June verification
follow-up). Those passes concentrated on the **process / resource / coroutine
engine** and reported defects F1–F7. I re-checked those areas against the current
snapshot and confirm they are **addressed**: resource-guard entries are now found
by a stored `guard_key` rather than by process address (F1); the worker-thread
recovery path calls `cmi_coroutine_reset_to_main()` and `cmi_event_queue_reset()`
after an in-model `cmb_logger_error` (F2); `cmb_resource_acquire` rechecks
availability after resume instead of grabbing on trust (F3); `guard_queue_check`
is now a valid strict weak ordering (F6); and the Windows TEB offset is `0x1478`
(F7).

This review then went where the previous two did **not**: deep into the
**random-number distributions and the statistics accumulators**. All four
confirmed defects below are new — none of them appears in either prior review.

---

## Executive summary

Cimba's foundations are strong and hold up under scrutiny: the hash-heap event
queue, the hand-rolled coroutine context switch, the sfc64/ziggurat RNG core, and
the one-pass Pébay/Meng moment statistics are all correct and well above typical
open-source quality. The engine defects from the earlier reviews have been fixed
and locked in with tests.

However, the **numerics layer carries four reachable defects that the
engine-focused reviews did not reach**, one of which is a memory-safety bug in the
default release build triggered by inputs the library's own validation accepts.
For a library aimed at high-stakes simulation and COIN-OR inclusion, these should
be fixed before the work can be called solid end-to-end. All four are small,
localized fixes.

---

## Confirmed defects

### N1 — `cmb_random_discrete_nonuniform` can return an out-of-range index → heap-buffer-overflow in release builds (HIGH, memory safety) [verified]

`sums_to_one()` accepts any probability array whose sum is within `1e-3` of 1.0.
The realistic input `{0.333, 0.333, 0.333}` sums to 0.999 and passes. The
selection loop then accumulates `q` and, when the uniform draw falls in the
0.001 gap below the total, never breaks — so the loop counter reaches `n` and the
function returns `n`, one past the valid range `[0, n-1]`.

In a **release build** the guarding `cmb_assert_debug(ui < n)` is compiled out, so
the bad index is returned silently. It then flows into:
- `cmb_random_hyperexponential()` as `ma[ui]` — an out-of-bounds array read;
- `cmb_random_loaded_dice()` as `a + n`, i.e. `b + 1`, violating the documented
  range `x <= b`.

Reproduced two ways:
- A release-linked driver returned index `3` for `n = 3` on **49,658 of 50,000,000**
  draws (~0.1%, matching the gap).
- A release+ASan build produced a genuine
  `AddressSanitizer: heap-buffer-overflow ... READ of size 8 in cmb_random_hyperexponential`.

Note this can also be tripped by an array that sums to *exactly* 1.0 in exact
arithmetic, because the running sum in the scan accumulates floating-point error
and may fall just short for a draw extremely close to 1.0.

**Fix (verified):** iterate only to `n-1` and let the final bucket absorb the
remainder, which is both segmentation- and rounding-safe:

```c
double q = 0.0; uint64_t ui;
for (ui = 0; ui < n - 1; ui++) { q += pa[ui]; if (x < q) break; }
return ui;                 /* always in [0, n-1] */
```

A drop-in simulation of this logic returned a maximum index of 2 with zero
out-of-range results. Tightening the `sums_to_one` tolerance is *not* sufficient
on its own, because floating-point accumulation in the scan can still fall short.

---

### N2 — `cmb_wtdsummary` variance / stddev / skewness / kurtosis normalize by sample count, not total weight (HIGH for time-weighted work, correctness) [verified]

`cmb_wtdsummary` correctly accumulates the weighted mean (`m1`) and the weighted
central moments (`m2..m4`) via the weighted Pébay formulas — I verified the add
and merge math matches the reference. But the exposed accessors
`cmb_wtdsummary_variance/_stddev/_skewness/_kurtosis` simply delegate to the
**unweighted** `cmb_datasummary_*` versions, which divide by the sample count
`count - 1` (the number of `_add` calls), not by the total weight `wsum`.

For time-weighted data the number of `_add` calls is arbitrary — it depends on how
finely the time axis happened to be segmented by events — so every moment above
the mean is wrong and segmentation-dependent.

Reproduced: the identical time-weighted distribution {value 0 for duration 1,
value 10 for duration 1} reports

```
recorded as 2 intervals : mean=5.0000  var=50.0000  stddev=7.0711
recorded as 3 intervals : mean=5.0000  var=25.0000  stddev=5.0000
```

The correct time-weighted (population) variance is 25.0 in both cases. The mean
is right; the variance changes purely with segmentation.

This is **user-visible**: `cmb_resource_print_report()` runs the utilization
time series through `cmb_timeseries_summarize` into a `cmb_wtdsummary` and prints
its `StdDev`, `Variance`, `Skewness`, and `Kurtosis` — all of which are wrong for
the time-weighted quantity. Anyone forming confidence intervals on a time-average
result (queue length, utilization, work-in-process) from these numbers gets
garbage.

**Fix:** give `cmb_wtdsummary` its own moment accessors that normalize by `wsum`.
The weighted population variance is `m2 / wsum`. For an unbiased weighted *sample*
variance, also track `Σ w_i²` and use the reliability-weight correction
`m2 / (wsum − Σw_i²/wsum)`; the higher standardized moments need the same effective
sample size in their finite-sample corrections.

---

### N3 — `cmb_random_geometric(1.0)` and `cmb_random_negative_binomial(m, 1.0)` are wrong at p = 1 (MEDIUM, correctness) [verified]

Both functions document and assert the inclusive contract `0 < p ≤ 1`. At
`p = 1.0`, `denom = -log(1.0 - p) = +inf`, so `ceil(exp_sample / inf) = 0`.

Reproduced in a release build:
- `cmb_random_geometric(1.0)` returned 0 on all 1000 trials (documented range is
  `[1, ∞)`; with p = 1 every trial succeeds, so it must return 1).
- `cmb_random_negative_binomial(2, 1.0)` returned `18446744073709551614`
  (≈ `UINT64_MAX`) — the unsigned `cmb_random_geometric(p) - 1u` underflows from
  `0 - 1`, then accumulates.

Separately, the `prev`/`denom` cache in `cmb_random_geometric` is dead code:
`prev` is never assigned, so `p != prev` is always true and `-log(1-p)` is
recomputed on every call. (Contrast `cmb_random_std_gamma`, which correctly stores
`a_prev = shape`.)

**Fix:** special-case `p == 1.0` to return 1 (geometric) / 0 failures
(negative binomial), and add `prev = p;` so the cache actually works.

---

### N4 — `cmb_dataset_fivenum_print` crashes on a single-element dataset (MEDIUM, crash on valid input) [verified]

For a dataset of one observation, `lhsz = count / 2 = 0`, and
`data_array_median(0, ...)` evaluates `v[0u/2u - 1u] = v[UINT_MAX]` — an
out-of-bounds access. `data_array_median` has no `n == 0` guard.

Reproduced under ASan:
`AddressSanitizer: SEGV ... in data_array_median (cmb_dataset.c:305)`, called from
`cmb_dataset_fivenum_print (cmb_dataset.c:355)`.

Computing a five-number summary of a one-point sample is a reasonable thing to do
(small pilot runs, degenerate trials), so this is a real reachable crash.

**Fix:** guard `n == 0` in `data_array_median` (return NaN or fire a release
assert) and handle `count < 4` explicitly in the five-number summary.

---

## Lower-severity observations

- **MXCSR not preserved across context switches by default.** The build defines
  `-DNMXCSR`, so the SSE control/status word is not saved or restored on a switch.
  The only protection is a *debug-assert-only* sanity check
  (`cmi_coroutine_registers_valid`), which is not even called in release builds.
  A coroutine that changes FP rounding or flush-to-zero/denormals-are-zero would
  then leak that state to whatever coroutine runs next. The 15 June review
  independently flagged the analogous x87 control-word gap. For a numerics-critical
  library I'd recommend preserving the SSE control word by default and documenting
  the constraint prominently if `NMXCSR` remains an opt-out.

- **`cmb_random_logistic`** can return `-inf` when `cmb_random()` yields exactly 0
  (probability 2⁻⁵³): `log(0 / (1-0))`. **`cmb_random_bernoulli`** uses `<=`, so it
  returns 1 with probability 2⁻⁵³ even at `p = 0`. Both are negligible
  statistically but are easy to make exact (draw from (0,1), use `<`).

- **ThreadSanitizer crash on the resource-contention-in-worker path — diagnosed as
  a TSan runtime artifact, not a Cimba defect.** A simulation in which multiple
  processes contend on a `cmb_resource` (coroutines blocking in the guard and being
  resumed) *while running inside a `cimba_run` pthread worker* can crash under TSan.
  The crash was isolated and root-caused by experiment:

  - The crash requires all three of: resource-guard **contention** (a process
    actually blocks and is later resumed), coroutines running **inside a pthread
    worker**, and **TSan**. Resource contention on the main thread is clean under
    TSan; a single non-contending process in a worker is clean; the buffer
    block/resume path in a worker is clean (this is what `test_cimba` exercises,
    and it passes under TSan).
  - The same workload is **correct in release** (correct results) and **completely
    clean under ASan** (zero errors).
  - Disabling only TSan's signal handler (`TSAN_OPTIONS=handle_segv=0`) makes the
    *identical* run complete correctly under full TSan instrumentation — correct
    results, **zero data races**, across many repetitions. The reported failure is
    TSan's own signal handler faulting (`ThreadSanitizer: nested bug in the same
    thread, aborting`), not a fault in Cimba.
  - The crash is **flaky and optimization-sensitive**: reliable at `-O0`, not
    reproduced at `--optimization=1` (the level the CI uses). Nondeterministic,
    signal-timing-dependent, and located in TSan's runtime — the signature of a
    signal-delivery-on-swapped-fiber-stack interaction, a known rough edge for
    hand-rolled coroutine libraries under TSan, rather than a deterministic memory
    error in the library.

  **Conclusion:** this is not a correctness or memory-safety defect. The library
  executes correctly on this path in release, under ASan, and under TSan with
  `handle_segv=0`. It does, however, expose two CI items. (1) The triggering
  combination — resource-guard contention inside `cimba_run` workers — is currently
  exercised by no test under any sanitizer (`test_cimba` uses the buffer path; the
  resource/process/condition tests run coroutines on the main thread with no
  `cimba_run`), so it is a genuine coverage gap. (2) Because the artifact is
  timing-dependent, a TSan job that does reach this path can flake. **Recommended
  fix:** add `handle_segv=0:handle_sigbus=0` to the TSan job's `TSAN_OPTIONS` — this
  preserves full data-race detection (verified: a deliberate race is still reported
  with `handle_segv=0`) and `halt_on_error=1`, while delegating any real signal
  fault to the OS (which would still fail the build, just without TSan's
  annotation; ASan+UBSan remains the authoritative memory-safety net). Then add a
  multithreaded resource-contention test that runs inside `cimba_run` workers,
  asserting on results, to close the coverage gap. Document the TSan interaction as
  a known limitation so a future recurrence is not mistaken for a regression. The
  hold-only and buffer coroutine paths are TSan-clean as-is.

- **Test teardown / leak hygiene.** Several shipped tests (e.g. `test_logger`) exit
  without calling `cmb_event_queue_terminate`, so ASan leak detection flags the
  event-queue allocation. This is benign test hygiene, but it means a clean
  `detect_leaks=1` run requires either fixing the tests or running CI with
  `detect_leaks=0`. Worth making explicit.

- **Reproducibility is entirely the user's responsibility (design note, not a
  bug).** `cimba_run` never seeds the RNG; the per-trial thread-local stream is
  whatever the trial function sets. This is the right design *if* each trial seeds
  deterministically from its trial index
  (e.g. `cmb_random_initialize(cmb_random_fmix64(master, cimba_trial_index()))`),
  in which case reproducibility holds regardless of which worker runs which trial.
  But a trial that forgets to seed inherits leftover thread-local state from
  whatever trial last ran on that worker — a result that silently depends on
  scheduling. For a high-stakes library this deserves a prominently documented
  idiom and/or a convenience helper.

---

## Verified strengths

These were confirmed by building and running, not just reading:

- **Builds clean** at `warning_level=3` (`-Wall -Wextra -Wpedantic`); **23/23
  tests pass** in release; the core data-structure, process, resource, condition,
  buffer, and queue tests are **clean under ASan+UBSan**.

- **Hash-heap event queue (`cmi_hashheap`)** is correct: tombstone deletion only
  zeroes `heap_index` (never `hash_key`), so linear-probe chains stay intact; the
  grow path rehashes and the compact path bounds tombstone accumulation; FIFO
  tie-breaking by `hash_key` is deterministic. The dequeue→copy→advance-clock
  ordering in `cmb_event_execute_next` correctly handles the volatile `heap[0]`
  working slot, and past-scheduling is caught by a release assert.

- **Coroutine context switch** is careful, correct low-level work. I traced the
  SysV and Win64 16-byte stack alignment through init, save, and restore; guard
  pages are installed via `mprotect`/`VirtualProtect`; the Windows TEB
  StackBase/StackLimit/DeallocationStack fields are swapped atomically before the
  stack pointer moves; the longjmp-recovery path re-syncs ASan/TSan fiber state and
  the OS stack bounds; and the Intel CET trade-off (the library disables CET
  process-wide because the hand-rolled switch cannot run under shadow stacks) is
  explicit and documented.

- **RNG core is textbook-correct:** sfc64 (parameters 11/3/24, counter in `d`),
  splitmix64 seeding with a 20-iteration warm-up, Lemire's nearly-divisionless
  bounded uniform, and McFarland's ziggurat normal/exponential all match their
  references.

- **One-pass moment statistics are numerically exact.** I validated mean,
  variance, skewness, and excess kurtosis — *including the parallel Pébay merge* —
  against NumPy/SciPy on 10,000 lognormal samples split across two summaries and
  merged: agreement to 10 significant figures. The Meng sequential single-update
  and the Pébay parallel-merge formulas match the literature exactly, and
  skewness/kurtosis use the standard sample-corrected estimators.

- **Process/resource engine** has comprehensive kill/cleanup: `cancel_awaiteds`
  handles all four awaitable types plus a defensive pattern-cancel of all six
  wakeup-event kinds; resource acquire implements correct no-barging semantics with
  a recheck after resume; and the interrupt-while-waiting path avoids double-free.
  The earlier reviews' F1–F7 are confirmed addressed in this snapshot.

---

## Performance note

The architecture is sound (thread-local state with `initial-exec` TLS, LTO, pool
allocators on hot paths, a 64-byte cache-line heap tag, lazy hash activation,
branch-specialized comparators), consistent with the published throughput. One
concrete cost worth measuring: process teardown calls `cmb_event_pattern_cancel`
six times, each an O(n) linear scan of the entire event queue, so a workload with
many short-lived processes and a large pending queue pays O(6·n) per exit and can
approach O(n²) overall. Tracking interrupt/resume events in the awaits list (the
two event kinds not currently tracked there) would let the pattern-cancel sweep be
removed and replaced with the existing targeted cancellation. This is an
optimization opportunity, not a correctness issue.

---

## Recommendations, in priority order

1. **Fix N1** (out-of-range index in `cmb_random_discrete_nonuniform`) — it is a
   memory-safety bug in the default release build, reachable from in-contract
   inputs. Loop to `n-1` with a catch-all final bucket.
2. **Fix N2** (weighted higher moments) — give `cmb_wtdsummary` weight-normalized
   variance/stddev/skewness/kurtosis; the current values are silently wrong and are
   printed in resource utilization reports.
3. **Fix N3 and N4** (geometric/negative-binomial at p=1; fivenum crash on n=1) —
   both small, both reachable from valid input.
4. **Decide on MXCSR**: preserve the SSE control word by default, or document the
   `NMXCSR` constraint very prominently.
5. **Handle the TSan artifact and close its coverage gap.** Set
   `handle_segv=0:handle_sigbus=0` on the TSan CI job (keeps race detection and
   `halt_on_error=1`), add a resource-contention test that runs inside `cimba_run`
   workers, and document the TSan/fiber/signal interaction as a known limitation.
   This is a CI/coverage fix, not a code fix — the path is correct in release and
   under ASan. Separately, document the per-trial seeding idiom for reproducibility.
6. Address the minor edge cases (logistic/bernoulli boundary draws) and test
   teardown hygiene as polish.

---

## Verification statement

On Linux x86-64 with GCC 13.3.0, I verified by building and executing the code that
Cimba 3.0.0-beta builds cleanly from source in release, debug, and
AddressSanitizer+UndefinedBehaviorSanitizer configurations; that the complete
shipped test suite passes (23/23); and that the core data-structure, process, and
resource tests are clean under ASan+UBSan. I independently validated the running
and parallel-merged moment statistics against NumPy/SciPy to ten significant
figures, and confirmed the prior reviews' engine defects F1–F7 are resolved in this
snapshot.

I also demonstrated, by running minimal reproduction programs, four previously
unreported defects in the numerics and statistics layer: a release-build
heap-buffer-overflow reachable through `cmb_random_discrete_nonuniform` from
in-contract inputs (N1); incorrect, segmentation-dependent time-weighted
variance/stddev/skewness/kurtosis in `cmb_wtdsummary`, surfaced in resource reports
(N2); incorrect results from `cmb_random_geometric` / `cmb_random_negative_binomial`
at p = 1.0 (N3); and a SEGV in `cmb_dataset_fivenum_print` on a one-element dataset
(N4).

Separately, I investigated and **root-caused** a ThreadSanitizer crash on the
resource-contention-inside-a-worker path. By controlled isolation I established that
it is a **TSan runtime artifact, not a Cimba defect**: the identical workload is
correct in release, clean under ASan, and completes correctly under full TSan
instrumentation once TSan's own signal handler is disabled (`handle_segv=0`), with
zero data races; the failure is TSan's signal handler faulting on a swapped fiber
stack, it is timing-dependent and optimization-sensitive (not reproduced at the
CI's `--optimization=1`), and `handle_segv=0` was verified to preserve full
race detection. The actionable items are a CI coverage gap and a `TSAN_OPTIONS`
setting, not a code change.

**Assessment:** the engine is solid and the previously gating process-interaction
defects are fixed. The remaining barrier to "solid end-to-end" for high-stakes use
is the numerics layer: N1 is a memory-safety bug and N2 silently corrupts a
user-facing statistic, so both should be treated as release blockers; N3 and N4 are
correctness/crash bugs on valid input and should follow. All four fixes are small
and localized. The ThreadSanitizer crash is not a defect; it is handled with a
`TSAN_OPTIONS` change plus a new test to close the coverage gap. As always for a
beta, continued caution is appropriate for the Windows port (not exercised here)
and for code paths beyond the test suite.
