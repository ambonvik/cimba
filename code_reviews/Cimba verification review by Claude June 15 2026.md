# Cimba Code Review — Verification Follow-up

**Subject:** Cimba — multithreaded discrete event simulation library in C
**Version reviewed:** 3.0.0 (post-beta `main` snapshot, the "incremental changes on the way to the official 3.0.0 release" described in the changelog), zip provided by the maintainer, June 2026
**Reviewer:** Claude (Anthropic), AI-assisted code review requested by the maintainer
**Date:** June 15, 2026
**Review environment:** Ubuntu 24.04 x86-64, GCC 13.3, Meson 1.11, Ninja 1.13, NASM 2.16. Linux paths exercised at runtime; Windows port reviewed by inspection only.
**Relationship to prior review:** This is a verification pass against the prior review dated June 10, 2026 (commit `57546f8`), which reported five verified defects (F1–F5), two high-confidence inspection findings (F6–F7), eight medium findings (M1–M8), and a list of low-severity/polish and documentation items.

## Scope and method

The goal of this pass was narrower than the original review: confirm whether the previously reported defects have been resolved, judge the quality of the fixes, and flag any regressions or new issues introduced alongside them. It also re-examined the code paths that were newly added since the prior snapshot (`cmb_priorityqueue`, `cmb_process_yield`/`cmb_process_resume`, the process timer API, the worker-recovery machinery, and the install packaging).

As before, this was not purely a reading exercise. The library was built from source in debug, release, and AddressSanitizer+UndefinedBehaviorSanitizer configurations; the full shipped test suite was executed (**23/23 pass** in debug, up from 22, including the long-running stochastic M/G/1 test); the process-interaction tests were re-run clean under ASan+UBSan; the library was installed to a staged tree and a downstream consumer was compiled and linked against the installed headers and shared object only; and **five independent minimal reproduction programs were written and run** against the debug, release, and ASan builds to confirm the previously demonstrated defects are gone. Findings below marked **[verified]** were demonstrated by running code; **[by inspection]** follow from reading the source.

## Executive summary

Every defect from the prior review has been addressed, and the fixes are, with few exceptions, the *right* fixes rather than the minimal ones. The two high-severity correctness bugs in the process-interaction layer (F1, F2) are resolved with a clean, single key scheme and a properly engineered cross-stack recovery path; the binary-resource mutual-exclusion bug (F3) is fixed with the same recheck-loop discipline the other primitives already used; the segfaulting dispatch mode (F4) is a correct one-line fix; and the out-of-tree packaging failure (F5) is fixed *and* guarded by a new CI job that compiles a downstream program against an installed tree. The two inspection findings (F6, F7) are corrected. All eight medium findings (M1–M8) and the entire low-severity/polish list (a–j) are resolved, as is the documentation-drift list.

Most importantly, the fixes are backed by **regression tests that check invariants rather than golden output** — precisely the gap the prior review identified. There is now a tied-timestamp mutual-exclusion test that asserts single-holder with `cmb_assert_always` (live in release), a randomized contention/reprioritization/stop "soak" test that drives the F1 paths under stress, a hash-map churn test that bounds tombstones over 50,000 iterations (M1), and an install-and-compile smoke test in CI (F5).

The remaining items are minor and none are correctness defects: a still-unshipped C23 compatibility shim for downstream consumers, no public API to select a coroutine stack size, a theoretical integer-overflow edge in `cmb_random_dice` for near-full-width ranges, a `cmb_process_resume` precondition worth hardening in the spirit of M3, and `-Wunused` noise confined to the tutorials and tests. The engineering foundations praised in the prior review are intact and have been extended consistently.

**Assessment:** the specific defects that previously blocked the "reliable for high-stakes simulation work" claim — abort on priority change, lost-wakeup deadlock on stop, silent cross-trial corruption on in-model error, silent double-grant of a binary resource, and an unusable installed library — are all resolved and regression-tested. On Linux x86-64, this snapshot clears the bar the prior review set. The Windows-specific fix (F7) is correct by inspection but, as before, awaits confirmation on Windows hardware.

## Verification of prior findings

### High-severity verified defects

**F1 — Resource-guard key inconsistency (abort on priority change; lost wakeup on stop). FIXED. [verified]**
The key scheme is now unified. `cmb_resourceguard_wait` keys each waiter by an enqueue sequence number and records it on the awaitable as `awp->guard_key`; the cleanup paths (`cmb_process_priority_set`, `cmi_process_cancel_awaiteds`) recover that exact key via the new `cmi_process_guard_key(pp, guard)` rather than looking up by process address. The `cmb_resourceguard_remove`/`cancel` helpers were refactored around a `cmi_resourceguard_remove_key` that takes the key directly. Two independent repros confirm both failure modes are gone:
- *Priority change while queued* — a process changes a queued waiter's priority; the run completes normally (previously a release-assert abort in `cmi_hashheap_drank`).
- *Stop while queued* — `cmb_process_stop` is called on a waiter with a second waiter behind it; the second waiter is correctly woken on release (previously a lost wakeup and model-level deadlock).
Both repros are clean under ASan. Regression coverage now includes a dedicated stop/priority-change soak test in `test_resourceguard.c` whose teardown would trap any stranded queue entry.

**F2 — `cmb_logger_error()` from inside a process corrupts the worker for later trials. FIXED. [verified]**
The recovery branch of `worker_thread_func` now calls `cmi_coroutine_reset_to_main()` and `cmi_event_queue_reset()` before the next trial. The new `cmi_coroutine_reset_to_main()` resets `coroutine_current` to the main coroutine, re-syncs the ASan/TSan fiber bookkeeping (announcing the switch with a NULL save so the abandoned coroutine's fake stack is dropped), and reinstalls the OS stack bounds the non-local jump skipped. The cross-stack jump itself is now made robust: on Windows the recovery jump uses `__builtin_setjmp`/`__builtin_longjmp` to avoid SEH unwinding (`RtlUnwindEx` → `STATUS_BAD_STACK`), while Linux deliberately keeps libc `setjmp`/`longjmp` so ASan's interposition still fires — the rationale is documented at the macro definition. My repro (error raised from inside a process, then a second trial on the same single worker) shows the failed trial counted correctly and the next trial running under its own identity, clean in debug, release, and ASan+UBSan.

**F3 — A binary `cmb_resource` granted to two holders at a tied timestamp. FIXED. [verified]**
`cmb_resource_acquire` no longer trusts the wakeup signal. The fast path now requires the guard queue to be empty (`cmi_hashheap_is_empty`) so a fresh caller cannot jump the line, and after a successful wait the function loops and re-checks `holder == NULL` before grabbing — the same recheck discipline the buffer, object-queue, and resource-pool already used. A new `test_resource.c` scenario reproduces the exact holder/waiter/"sniper" tied-timestamp race and asserts single-occupancy with `cmb_assert_always`, so the invariant is checked even in release builds where the original symptom was silent.

**F4 — Documented `cimba_run(..., NULL)` multi-trial-function mode segfaults. FIXED. [verified]**
Now `cimba_trial_func *trial_func = *(cimba_trial_func **)trial;` — the function pointer is read from the first struct member as documented, instead of casting the one-past-the-struct address to a function pointer. My repro with two distinct per-trial functions dispatches each correctly.

**F5 — Installed public headers not self-contained. FIXED. [verified]**
The private `cmi_*.h` headers the public API transitively needs are now installed alongside the `cmb_*.h`/`cimba.h` set, and `<stdarg.h>` was added to `cmb_logger.h`. I installed to a staging prefix and confirmed that all 17 public headers compile standalone (default dialect and `-std=c2x`) and that a downstream `#include "cimba.h"` program compiles *and links* against the installed tree only. This is now permanently guarded by a new `linux-install` CI job that follows the install guide, compiles `tutorial/hello.c` with the bare documented `gcc … -lcimba` command, and runs a thorough `test/tools/verify_install.sh` header-closure check.

### Inspection findings

**F6 — `guard_queue_check` was not a valid strict weak ordering. FIXED. [verified]**
The comparator now early-returns on the strict `!=` comparisons (`rank_i64`, then `rank_d64`, then `hash_key`), making it a proper strict weak ordering. The same correct shape is mirrored in the new `cmb_priorityqueue` comparator and the resource-pool `holder_queue_check`. A repro that queues five mixed-priority waiters on one resource confirms they are served strictly by priority with FIFO within a priority class.

**F7 — Windows TEB `DeallocationStack` offset written in decimal. FIXED. [by inspection]**
All three `DeallocationStack` accesses now use the correct hex `0x1478` (and `StackBase`/`StackLimit` are explicit `0x8`/`0x10`). The decimal `1478` (= `0x5C6`, inside `GdiTebBatch`) is gone. Correct by inspection; behavior on Windows hardware was not exercised here.

### Medium-severity findings

All resolved:

- **M1 (hash-map tombstones never reclaimed):** a `tombstones` counter plus an in-place `hashheap_compact` triggered at a 0.75 (live+tombstone) load factor now bound the tombstone population. Lookups terminate on a genuinely empty slot with a wrap-around backstop. A new `test_hashheap_churn` runs 50,000 retire/insert cycles at a fixed live count and asserts the heap never grows, compaction fires, and `heap_count + tombstones` stays bounded.
- **M2 (`preempt` cancelled the wrong process's awaitables):** `cmb_resource_preempt` now cancels the **victim's** awaitables.
- **M3 (interrupting a finished process aborts in release):** `wakeup_event_interrupt` now check-and-warns on a non-running target, matching the sibling wakeup handlers.
- **M4 (logger config thread-local but documented global):** `cmi_logger_mask` and the time-formatter pointer are now file-scope globals accessed atomically; I confirmed that `cmb_logger_flags_off(CMB_LOGGER_INFO)` in `main` suppresses INFO output from worker threads.
- **M5 (`cimba_trials_remaining` underflow):** now clamped (`nxt >= total ? 0 : total - nxt`).
- **M6 (wrong `format(printf,…)` index):** `cmb_logger_vfprintf` is `format(printf, 5, 0)` and the `cmi_logger_*` wrappers carry their own attributes; the return-value accumulation bug (item d) is fixed too.
- **M7 (unchecked syscalls; `PROT_EXEC` on guard page):** `pthread_create`/`pthread_join` and both `mprotect` calls are now checked, and the guard-page re-protect dropped `PROT_EXEC` (now `PROT_READ|PROT_WRITE`), restoring W^X.
- **M8 (thread cleanup assumes heap origin):** processes carry a `heap_allocated` flag set by `cmb_process_create`, and `cmi_coroutine_thread_cleanup` only `free()`s when it is set, so embedded/stack/arena-allocated processes are no longer freed at the wrong address.

### Low-severity / polish (a–j) and documentation

All ten polish items are fixed: hash-print loop bounds (a), `clear` memset sizing consistency (b), 64-bit shift `UINT64_C(1)` (c), logger return accumulation (d), `offsetof` via `<stddef.h>` (e), `cmi_calloc` `size_t` count (f), an explicit CET posture (a `.note.gnu.property` declaring no IBT/SHSTK plus commentary) (g), bounded `rdseed`/`rdrand` retries with a tid+clock+`rdtsc` fallback that also XORs in `rdtsc` against the AMD microcode-`0xFFFFFFFF` bug (h), `cmb_random_dice` routed through Lemire's exact `cmb_random_discrete_uniform` (i), and the benchmark option comment/`-fno-omit-frame-pointer` typo (j). The documentation-drift sweep is done: `cimba_thread_hooks_set`, `cimba_threads_num`, `cmb_random()` documented as `[0, 1)`, `cmb_random_std_normal` "deviation 1", the corrected `cmb_process_name_set` comment, the "stattus buts" typo, the `holder_queue_check` comment, and an explicit resource-pool partial-acquisition deadlock note. The previously-flagged blanket interrupt cancellation was resolved *in code*: `cmi_process_cancel_awaiteds` now cancels only the specific internal wakeup event types per subject, not arbitrary user-scheduled events.

## New observations and residual items

None are correctness defects; listed for the maintainer's triage.

1. **C23 binding on consumers (carried over from F5's portability note).** The public headers still use `[[maybe_unused]]`, `[[noreturn]]`, and `__FILE_NAME__`, which require C23-mode GCC/Clang of every *consumer*, not just the library build. The compatibility shim the prior review suggested (fallbacks to `_Noreturn`/`__FILE__`) was not added; MSVC and older-standard consumers still cannot include the headers even though they could link the C ABI. Low cost, would widen the audience.
2. **No way to choose a coroutine stack size.** `cmb_process_initialize` hard-codes `CMI_COROUTINE_DEFAULT_STACKSIZE` (64/128 KB). Deep models have no public knob; the create/initialize split makes adding an optional sized initializer straightforward.
3. **`cmb_random_dice` range arithmetic.** Routing through Lemire fixed the bias/precision problem, but `b - a` is still computed in signed `int64_t` and can overflow for a near-full-width range (e.g. `a` near `INT64_MIN`, `b` near `INT64_MAX`) before the unsigned span is formed. The `a < b` assert does not guard this. Theoretical, but cheap to assert against or compute in unsigned.
4. **`cmb_process_resume` precondition.** `resume_event` debug-asserts the target is `RUNNING`, then enters `cmi_coroutine_resume`, whose *release* assert would fire if the target had already finished (e.g. resumed twice, or resumed after a same-timestamp stop). This is the same class as M3; resume is an explicit user action so the risk is lower, but the check-and-warn pattern would make it non-fatal.
5. **`-Wunused` noise at `warning_level=3`.** The core library (`src/`) compiles warning-clean, but several tutorials and tests emit `unused parameter`/`unused variable`/ignored-`system()` warnings. Cosmetic and outside the shipped library, but easy to silence with `cmb_unused()`/`(void)` and worth doing since the project advertises a high warning level.

## Performance

The performance architecture is unchanged and still sound; the M1 tombstone fix removes the one finding from the prior review that threatened headline throughput over long, churn-heavy runs, and now has a regression test bounding it.

The prior review's other suggestion — dropping the `pushfq`/`popfq` pair from the Linux hot-path context switch, since the C ABI already treats RFLAGS as clobbered across a call — **has been implemented**, confirmed by inspection of `src/port/x86-64/linux/cmi_coroutine_context.asm`; the maintainer reports a ~5% speed improvement from it. The `save_context`/`load_context` macros save and restore only the SysV callee-saved general-purpose registers (RBP, RBX, R12–R15) plus, optionally, MXCSR under `%ifndef NMXCSR`; there is no RFLAGS save/restore. The one flag the ABI does constrain — the direction flag — is handled cheaply with a single `cld` on restore rather than a full `pushfq`/`popfq`. This is the boost.context-style approach the prior review pointed to, and the register layout matches the `cmi_coroutine_stack_valid` slot accounting in `cmi_coroutine_context.c`. One small residual the prior review noted still stands: even with `NMXCSR` unset, the x87 FPU control word (`fnstcw`/`fldcw`) is not saved; this is harmless for the SSE-based code typical on x86-64 but is technically callee-saved, so a model that deliberately changes the x87 rounding/precision control word across a coroutine boundary would not have it preserved.

No independent re-benchmarking was performed in this pass; the ~5% figure is the maintainer's measurement.

## Tests and CI

The test suite grew from 22 to 23 and, more importantly, gained the invariant-style coverage the prior review asked for: a tied-timestamp single-holder assertion (F3), a randomized contention/reprioritization/stop soak that drives the F1 paths and relies on guard teardown to trap stranded entries, a 50,000-iteration hash-map churn test bounding tombstones (M1), and — in CI — an end-to-end install-and-compile job plus a header-closure script (F5). The ASan/UBSan/TSan jobs and the Windows MinGW job remain. I re-ran the process-interaction tests (`resource`, `resourceguard`, `resourcepool`, `buffer`, `condition`, `priorityqueue`, `process`) clean under ASan+UBSan locally.

Two coverage gaps the prior review noted are now closed by the soak and churn tests. A property test over each heap comparator (shuffled inputs, asserting antisymmetry and a stable total order) would still be a worthwhile addition given how central strict-weak-ordering correctness proved to be (F6), and a long-churn *benchmark* (as opposed to the functional churn test) would guard the O(1) claim under wall-clock measurement.

## Verification statement

This follow-up verified, by building and executing the code on Linux x86-64: the project builds cleanly from source in debug, release, and ASan+UBSan configurations with GCC 13.3; the complete shipped test suite passes (23/23 in debug); the process-interaction tests pass under AddressSanitizer + UndefinedBehaviorSanitizer; an installed copy of the library is self-contained and a downstream program compiles and links against it; and five independent minimal reproduction programs confirm that the previously demonstrated defects F1 (priority-change abort and stop lost-wakeup), F2 (cross-trial corruption on in-model error), F3 (binary double-grant), F4 (NULL trial-function dispatch), and F6 (mixed-priority queue order) are resolved, with the F1/F2 repros also clean under sanitizers. Findings F5 and F7, M1–M8, the polish list (a–j), and the documentation items were confirmed by inspection, several of them corroborated by the new regression tests and the live install check; F7 (Windows) is correct by inspection and was not exercised on Windows hardware.

Assessment: the verified, high-severity process-interaction defects that the prior review held as gating the project's "critical infrastructure / high-stakes simulation" ambition have been fixed correctly and, crucially, locked in with invariant-checking regression tests rather than output comparisons. On the platform exercised here (Linux x86-64), I no longer see a structural reason this snapshot cannot be relied upon for that class of work. The residual items above are enhancements and hardening, not blockers. Continued caution is appropriate for the Windows port until F7 and the recovery-jump path are confirmed on Windows hardware, and — as always for a beta — for code paths exercised by real models beyond the test suite.

*This is a point-in-time verification review of the maintainer-supplied `main` snapshot by Claude (Anthropic). It reflects build-and-test evidence on Linux x86-64 plus source inspection; Windows behavior, CUDA paths, tutorials, and prose documentation were not exhaustively reviewed. It is not a certification or guarantee of fitness for any purpose. The five reproduction programs have been provided to the maintainer alongside this document.*
