# Cimba Verification Review — Closing the 3.1.1 Review Cycle

**Subject:** Cimba — multithreaded discrete event simulation library in C
**Version reviewed:** 3.1.2, `main` at commit `37870e9` ("Updated changelog with release date for v 3.1.2")
**Reviewer:** Claude Opus 5.5 (Anthropic), AI-assisted verification review requested by the maintainer
**Date:** 24 November 2026
**Scope:** Closes the cycle opened by the 14 November code review of release 3.1.1 by Claude Fable 5.2 (findings F1–F15 and L1–L9).
**Environment:** Ubuntu 24.04.5 x86-64, GCC 13.3, Clang 18.1.3, Meson 1.12.1, NASM 2.16.01, two-vCPU container. Linux paths exercised at runtime. The Windows/MSYS2 fixes (L3, L4c) were checked by inspection only; the project's CI matrix covers it at runtime. ThreadSanitizer, PGO and CUDA configurations were not exercised here.

## Method

Everything reported as verified below was checked by building the tree and executing it, not by reading diffs. Each fix was exercised by a reproducer: the set accompanying the 14 November review, and about twenty more written during this cycle for cases the fixes themselves opened up (same-instant grants in each guarded class, preemption while a grant is in flight, teardown order, tag-pool growth). Each reproducer was run against 3.1.1 to confirm it shows the defect, and against the fix to confirm it does not.

The cycle ran as a loop: the maintainer hand-coded each fix, the fix was checked here against its reproducer and the full suite, and any problem found went back for another iteration. The new regression tests (below) were each also run against a build without the corresponding fix, to confirm they fail there.

## Baseline

| Configuration | Result |
|---|---|
| GCC release, `warning_level=3` | Compiles clean, **zero warnings** |
| GCC release test suite | **27/27 pass** |
| GCC debug test suite | **27/27 pass** |
| GCC debug + ASan/UBSan/LSan test suite | **27/27 pass**, no sanitizer findings (`cimba` 328 s) |
| Clang release | Compiles clean, **zero warnings** (two in 3.1.1, see L9) |
| Clang release test suite | **27/27 pass** |
| Static library build | Builds clean, all tutorials link (F3), suite **27/27 pass** |
| M/M/1 benchmark (maintainer's measurement) | **0.594 s**, mean of ten runs, against 0.597 s for 3.0.1 in the paper |

The benchmark result matters because several fixes this cycle sit on the hot path of every resource request. One intermediate form of the F8 fix cost about 5 % on this benchmark. The final form costs nothing measurable.

## Status of the 14 November findings

All fifteen F-findings verified fixed. Of the nine low-severity items, seven are fixed in full. L4 is fixed in its correctness part, with its two performance parts declined. L7 is deferred. Both decisions are the maintainer's and are recorded here.

| ID | Finding | Status |
|---|---|---|
| F1 | Abandoning a trial segfaulted when registered objects were locals of the trial function | **Fixed.** Recovery now runs in a stack frame at a safe distance below the abandoned trial's frame (`recover_abandoned_trial`), so the cleanup no longer overwrites the registry nodes it is walking. Covered by a regression test. |
| F2 | Abandoning a trial aborted the experiment when a process was queued at a `cmb_resource` | **Fixed.** `cmb_resource_terminate` checks the empty-queue and holder invariants only outside demolition mode. Every terminate path under abandonment touches only its own object. Covered by a regression test. |
| F3 | `cimba_threads_use()` defined non-inline in `cimba.h` | **Fixed.** Now `static inline`. `testutils.c` includes `cimba.h`, so every test program now links two translation units that include it. |
| F4 | Pool acquire rolled back after a non-success signal did not signal the guard | **Fixed.** The rollback signals the guard. Covered by a regression test. |
| F5 | Sticky tiebreaker broke FIFO for direct users of `cmb_resourceguard_wait()` | **Fixed.** The tiebreaker is armed lazily before the first wait of a request and cleared only on exit, with one set of constants (`CMI_PROCESS_TB_ARMED`, `CMI_PROCESS_TB_CLEARED`) used by every class. Covered by a regression test. |
| F6 | Stale process-completion wakeup ended a later `cmb_process_wait_process()` early | **Fixed.** Completion wakeups carry the await key of the wait they belong to and are discarded on mismatch. Covered by a regression test. |
| F7 | `cmb_process_priority_set()` aborted while the target's grant was in flight | **Fixed.** The guard entry is found by its enqueue key. A process that has already left the queue is skipped with an info message. |
| F8 | A new arrival at the grant timestamp overtook the waiter that was just granted | **Fixed** in all five guarded classes (resource, pool, buffer, object queue, priority queue). An `in_flight` grant counter and a per-request `granted` flag gate the first attempt. Preemption that meets a grant in flight retries once the current instant has settled (`cmi_process_hold_prio` at `INT64_MIN`). Covered by regression tests for the resource and the buffer. |
| F9 | `PREEMPTED` from another resource during a pool acquire returned failure but kept partial holdings | **Fixed.** Partial holdings are rolled back on every non-success exit. |
| F10 | `cmb_process_destroy()` left condition wakeups and start events scheduled | **Fixed.** Terminate now cancels every event with the process as subject (`cmb_event_pattern_cancel`). Stopping a started but not yet dispatched process cancels its start event and returns `CMB_PROCESS_SUCCESS`. Both behaviour changes are in the CHANGELOG. |
| F11 | A success-valued resume or timer during a hold or a guard wait was taken as the process's own wakeup | **Fixed.** A `CMB_PROCESS_SUCCESS` arriving while the process is still queued is reported as a model error. `cmb_process_interrupt()` now rejects `CMB_PROCESS_SUCCESS` with a release assert. Both are in the CHANGELOG. |
| F12 | Terminating a held resource left a dangling holder tag | **Fixed.** The holder's holdable record is removed before the holder pointer is cleared. |
| F13 | `cmb_condition_signal()` woke equal-priority waiters in heap-array order | **Fixed.** Waiters whose demand is met are woken in priority order, then in FIFO order. Covered by a regression test. |
| F14 | Ceiling on live processes from guard-page mappings, reported as a bare assert | **Fixed** as a documented limit. The `mprotect` assert in `cmi_coroutine_context.c` now has a comment at the point of failure and a note in the function header on raising `vm.max_map_count`. The maintainer chose a terse assert over a runtime message. |
| F15 | Version metadata stale at 3.0.0; public struct layout changed under the same SONAME | **Fixed.** `VERSION` reads 3.1.2. The Linux soversion now equals the full version (`libcimba.so.3.1.2`), and the Windows DLL name carries it. |
| L1 | `cmb_event_current()` returned an uninitialized key before the first event | **Fixed.** Slot 0 is zeroed in `cmi_hashheap_initialize`. |
| L2 | Default worker count ignored CPU affinity | **Fixed** on Linux: `sched_getaffinity` with `CPU_COUNT`. The Windows processor-group limit is unchanged and immaterial below 64 logical CPUs. |
| L3 | `cmb_random_hwseed()` mixed in the TSC before testing for failure | **Fixed** on both platforms. The raw value is tested by `seed_ok()` (not 0, not all ones) before any fallback mixing. Windows checked by inspection. The fallback's `cmi_rdtsc` prototype also now returns the full 64 bits. |
| L4 | Windows port drift (no stack pool, full commit, parameter mismatch) | **(c) Fixed; (a) and (b) declined.** The Windows `cmi_coroutine_stack_free` now takes the same two parameters it is declared and called with, which was the correctness issue (checked by inspection). The stack pool and full commit affect only performance. The maintainer's position: Windows is supported for correctness, not tuned for performance; users who need speed should run under WSL. |
| L5 | Public preconditions checked only at debug level | **Fixed.** The listed checks are promoted to release asserts. |
| L6 | Slots lost from thread-local tag pools | **Fixed.** In demolition mode, `cmb_process_terminate` returns its own awaitable, holdable and waiter tags without following their pointers. `cmb_resourceguard_terminate` returns remaining observer tags. Clearing, terminating or resetting the event queue returns the waiter tags of events still queued. Pool growth over 20 000 trials went from 100, 21 and 41 chunks to 1 in each pool. Covered by a regression test. |
| L7 | Recording memory unbounded | **Deferred** to the next minor feature release (it adds API), together with a confidence-interval function. |
| L8 | `cimba_run` details | **Fixed.** At most one worker per trial is started. A nested call trips `cmb_assert_release(!cmi_recovery_armed)` instead of deadlocking. `cimba_threads_inuse()` reads atomically. |
| L9 | Small items | **Fixed.** The stale `jmp_buf` declaration is gone. Re-entering the logger from a user time formatter is guarded. `cmb_dataset_sort` drops the `const`. The tiebreaker is passed through `memcpy` rather than a union. The Clang warnings and the CHANGELOG errata are corrected. |

## Defects found during the cycle

The fix loop surfaced roughly a dozen defects in intermediate versions of the fixes. All were caught before release. They are worth recording because they show where this code is fragile:

- **The F8 gate**, replicated in five classes, accounted for most of them:
  - an uninitialized `granted` flag;
  - inverted in-flight polarity in the buffer;
  - the tiebreaker re-armed on every wait;
  - the buffer's get side consulting the wrong guard;
  - a bitwise `&` where `&&` was meant;
  - four debug asserts in the object and priority queues that did not allow for a deferred first attempt.
- **Preemption** had two: a pool preempt that cleared the caller's tiebreaker in the middle of its request, and a NULL dereference in the in-flight branch.
- **The F13 rewrite** went through an off-by-one, a comparator that compared the wrong fields, stale indices during removal, and a buffer sized in bytes where elements were meant.
- **Smaller ones:**
  - F12 at first read the holder after clearing it;
  - the first F9 merge reset a holder record that did not exist;
  - the L3 sentinel was a 32-bit literal compared against a 64-bit seed.

## Regression tests added

The maintainer's policy is to add tests for regressions that are silent and could easily slip back through some unrelated change, not one test per bug. This follows the existing `test_partials()` for Issue #5. Each new test is silent unless an assert trips, and each was confirmed to fail on a build without its fix.

| Test | File | Guards | Fails on |
|---|---|---|---|
| grant-order asserts in `test_exclusion` | `test_resource.c` | F8 | 3.1.1 |
| `test_inflight` | `test_buffer.c` | F8 (buffer) | 3.1.1 |
| `scenario_rewait` | `test_resourceguard.c` | F5 | 3.1.1 |
| `test_wake_order` | `test_condition.c` | F13 | 3.1.1 |
| `test_waiter_order` | `test_event.c`, `test_process.c` | FIFO waiter order | 3.1.1 and `5da0d55` |
| `test_stale_wakeup` | `test_process.c` | F6 | 3.1.1 |
| `test_rollback` | `test_resourcepool.c` | F4 | 3.1.1 |
| `test_abandon` | `test_cimba.c` | F1, F2, L6 | 3.1.1 (F2 assert; F1 segfault with the F2 part neutralized); `dd8c4f2` (L6) |
| `#include "cimba.h"` in `testutils.c` | all tests | F3 | 3.1.1 (multiple definition at link) |

`test_abandon` abandons every other trial with a process holding a resource, two processes queued at it, one waiting for an event, one waiting for another process, an observer registered, and four statistics objects in the trial function's frame. The other trials end with `cmb_event_queue_clear()` in the same state. It detects pool loss by comparing each tag pool's high-water mark between an early and a late trial. That catches the loss of a single tag, where the chunk count would not move for thousands of trials. The price is that the test reads two internal mempool fields.

Of the four test gaps the 14 November review identified, the first two (abandonment with live entanglements; multi-unit linking) are now closed. The third and fourth (tied timestamps; teardown order) are covered at the points where a regression would be silent. The findings left untested, such as F7, F10, F11 and F12, fail loudly if they recur. The tied-timestamp soak test suggested on 14 November was not added, which is the maintainer's call.

## CHANGELOG

The 3.1.2 entry is dated 2026-11-24 and covers every user-visible behaviour change of the cycle:

- the possible change in bitwise results from the reordering fixes;
- the new treatment of a spurious `CMB_PROCESS_SUCCESS`;
- the promoted asserts;
- FIFO waiter order;
- stop-before-start;
- terminate cancelling user events;
- the `cmb_dataset_sort` signature;
- hardened abandonment;
- the nested `cimba_run()` assert;
- affinity-aware thread count;
- the soversion.

The L3 seed change is not listed. It is internal and arguably not user-visible.

## Outside this cycle's scope

One pre-existing behaviour was observed and is recorded for a later cycle: under LeakSanitizer, calling `cmb_logger_error()` from a process running outside `cimba_run()` crashes LSan's own exit handling. This predates 3.1.1. It does not affect trials run through `cimba_run()`, which is the supported path for recoverable errors.

## Assessment

All fifteen findings from 14 November are closed, verified by execution against reproducers that show each defect on 3.1.1. Of the nine low-severity items, seven are closed in full, L4 is closed for correctness with its performance parts declined, and L7 is deferred to the next feature release. The library builds clean under GCC and Clang, passes 27/27 in release, debug, sanitizer and static configurations, and runs the reference benchmark no slower than the version published in the paper.

**Release 3.1.2 is warranted and I see no blockers.**

One observation to carry forward. Most of this cycle's defects, both those reported on 14 November and those surfacing in the fixes, lived at a single simulated instant: a grant in flight while a newcomer arrives, a timeout firing on the tick a process finishes, a cancellation reshuffling a heap just before a signal. None of these is visible in steady-state or statistical testing, and every one was found by a scenario built to put two events on the same timestamp. The in-flight gate in particular now exists in five near-identical copies. Two are guarded by regression tests, and the object queue, priority queue and pool are covered only indirectly. If that gate is touched again, adding the same-instant test for the class being changed would be cheap insurance.

---

*Verified at commit `37870e9` in GCC release, GCC debug, GCC debug + ASan/UBSan/LSan, Clang release and static-library configurations. Reproducers and negative regression runs were built against `v3.1.1`, `5da0d55` and `dd8c4f2`. The M/M/1 benchmark figure is the maintainer's own measurement.*
