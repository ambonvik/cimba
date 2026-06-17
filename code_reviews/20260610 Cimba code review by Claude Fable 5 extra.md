# Cimba Code Review

**Subject:** Cimba — multithreaded discrete event simulation library in C
**Version reviewed:** 3.0.0-beta, `main` branch snapshot (zip commit `57546f8fd9cb96b2db3058192c09b926b0be5fae`, June 2026)
**Reviewer:** Claude (Anthropic), AI-assisted code review requested by the maintainer
**Date:** June 10, 2026
**Review environment:** Ubuntu 24.04 x86-64, GCC 13.3, Meson 1.11, NASM 2.16. Linux paths exercised at runtime; Windows port reviewed by inspection only.

## Scope and method

This review covers design, coding style, reliability/maintainability, and performance of the core library: the runtime (`cimba.c`), coroutines and both context-switch ports (C and NASM assembly), the hash-heap, memory pool, event queue, process machinery, resources/guards/pools/buffers/queues/conditions, the logger and assertion framework, the random number module, the statistics modules, the build system, the CI configuration, and the test suite. The tutorials, CUDA examples, codegen programs, and prose documentation were spot-checked only.

The review was not purely a reading exercise. The library was built from source in debug and release configurations, the full test suite was executed (all 22 tests pass, including the long-running stochastic M/G/1 verification), the unit tests were re-run under AddressSanitizer + UndefinedBehaviorSanitizer (clean, consistent with the project's CI claims), and six small reproduction programs were written against the debug build to confirm suspected defects. Every finding marked **[verified]** below was demonstrated by running code; findings marked **[by inspection]** follow from reading the source and should be confirmed by the maintainer.

## Executive summary

Cimba is genuinely well-engineered in its foundations. The assertion discipline, the sanitizer-clean CI on every push, the hand-rolled context switch with correct ASan/TSan fiber annotations, the McFarland ziggurat samplers, the Pébay one-pass moment statistics, and the consistent create/initialize/terminate/destroy lifecycle convention are all well above typical open-source quality, and the performance architecture (thread-local everything, mempool allocators, contiguous hash-heap) is sound.

However, the review found **five verified defects, two of them high-severity correctness bugs in the process-interaction layer**, plus two further high-likelihood defects found by inspection. The common thread is that the verified bugs all live in code paths the test suite never exercises: changing the priority of a process that is waiting for a resource (aborts the program), stopping a process that is waiting for a resource (silently loses a wakeup and deadlocks the next waiter), reporting an error from inside a simulated process (corrupts the worker thread for subsequent trials), the documented multi-trial-function dispatch mode (segfaults immediately), and tied-timestamp acquisition of a binary resource (grants it to two holders at once). In addition, the installed public headers do not compile for out-of-tree consumers because they include private headers that are never installed.

None of these diminish the quality of the core data structures, which held up well under scrutiny. But for a library that advertises suitability for high-stakes simulation work, the process-interaction edge cases are exactly where models exercise the library hardest, and they need to be fixed and regression-tested before that claim is safe. Concrete fixes are suggested for every finding.

## Strengths

**Assertion and lifecycle discipline.** The three-tier assert system (`cmb_assert_debug` / `cmb_assert_release` / `cmb_assert_always`, controlled by `NDEBUG` and `NASSERT`) is consistently applied as precondition/invariant/postcondition documentation throughout. The uniform `create → initialize → terminate → destroy` object protocol, with the create/initialize split explicitly enabling composition-based inheritance, makes the codebase predictable to navigate. Cookie values (`CMI_INITIALIZED` / `CMI_UNINITIALIZED`) trap use of uninitialized objects cheaply.

**Sanitizer integration done properly.** Running ASan, UBSan, and TSan in CI on every push is uncommon; doing it for a *fiber* library is rarer still, because it requires explicit `__sanitizer_start_switch_fiber` / `__tsan_switch_to_fiber` annotations around every context switch. Cimba has these, including the subtle case of discarding the fake stack of a finished coroutine. This was confirmed locally: the unit suite is clean under ASan+UBSan.

**The context switch itself.** The Linux and Windows assembly is small, readable, and correct for what it does. The Windows version correctly swaps the TIB `StackBase`/`StackLimit` (which is what Windows 11's stack-security checks actually consult), saves XMM6–15 per the Win64 ABI, and the trampoline correctly intercepts a returning coroutine function and routes its return value into the exit function. Guard pages below each coroutine stack convert overflow into a hard fault rather than silent corruption.

**Random number module.** sfc64 as the base generator is a defensible, fast choice; seeding via splitmix64 with warm-up draws is textbook-correct; the McFarland improved-ziggurat implementations of the exponential and normal distributions are sophisticated and the algebra of the table-driven sampling (verified for the moment formulas and tail handling) checks out. The breadth of distributions, with per-distribution documentation and assert-guarded parameter domains, is a real asset for simulation users. The one-pass moment updates in `cmb_datasummary_add` are a compact reformulation of the Pébay update; the algebra was checked symbolically during this review and is exactly equivalent to the canonical form — nice work.

**Hash-heap design.** Co-locating the binary heap and its open-addressing index in one page-aligned allocation, with lazy hash-map activation so that pure schedule/execute workloads never pay for the map at all, is a thoughtful performance design. The heap sift operations, removal-with-replacement (compare before overwrite), and grow/rehash logic were checked carefully and are correct.

**Honest engineering culture.** The code comments explain *why*, the changelog is candid about beta status, the benchmark methodology is published, and the documentation explicitly discusses failure modes (e.g., spurious wakeups on conditions, the observer-cycle warning on guards). That culture is the right substrate for fixing what follows.

## Verified defects

### F1 — Resource-guard hash keys are inconsistent: priority changes abort, stop/cancel silently fail (HIGH) [verified]

`cmb_resourceguard_wait()` enqueues a waiting process into the guard's hash-heap using a **sequence number** as the key (`src/cmb_resourceguard.c:138`, `++enqueue_seq`). But three other functions look the entry up using the **process address**:

- `cmb_resourceguard_cancel()` — `src/cmb_resourceguard.c:257`
- `cmb_resourceguard_remove()` — `src/cmb_resourceguard.c:283` (called from `cmi_process_cancel_awaiteds`, i.e., the stop/interrupt/exit cleanup path)
- `cmb_process_priority_set()` — `src/cmb_process.c:175` (the comment there still says "Resource guard hashkeys are process addresses", which appears to be a stale design the `wait` side moved away from)

The keys never match. Two distinct failure modes were demonstrated:

*Repro 1 (`repro1_prio.c`):* a process changes the priority of another process that is queued on a `cmb_resource`. Result: release-assert abort in `cmi_hashheap_drank` ("Assert \"idx != 0u\" failed") — the whole program dies on a documented, supported operation. Note this is a *release* assert, so production builds abort too; with `-DNASSERT` it becomes undefined behavior instead.

*Repro 2 (`repro2_stop.c`):* `cmb_process_stop()` is called on a process queued on a resource. `cmb_resourceguard_remove` silently returns false, leaving a stale queue entry. When the resource is later released, the guard dequeues the stale entry, schedules a wakeup for the dead process, and `wakeup_event_resource` silently skips it — **consuming the wakeup**. The live waiter behind it is never resumed: a lost wakeup, observed as a model-level deadlock. No assert fires even in debug builds. If the user had destroyed the stopped process, the guard would also evaluate the demand function against freed memory.

`cmb_condition` delegates its cancel/remove to the same functions, so conditions inherit the same defect.

**Fix:** pick one key scheme. The simplest repair preserving FIFO tie-breaking: keep the sequence-number key, and store it where the cleanup paths can find it — the awaitable tag already carries the guard pointer in `ptr`; add the entry key (there is an unused `padding` field in `struct cmi_process_awaitable`), and have `cancel_awaiteds`/`priority_set` use it. Alternatively, key by process address again and derive FIFO order from the `rank_d64` entry-time field (which is already stored). Either way, add regression tests for: priority change while waiting, stop while waiting, interrupt while waiting, and cancel — against `cmb_resource`, `cmb_resourcepool`, `cmb_buffer`, and `cmb_condition`.

### F2 — `cmb_logger_error()` from inside a simulated process corrupts the worker thread for subsequent trials (HIGH) [verified]

The trial-failure recovery (`longjmp` from `cmi_logger_error`, `src/cmb_logger.c:251`, to the `setjmp` in `worker_thread_func`) works when the error is raised from the trial function itself — which is the only case the test suite exercises (`test_cimba.c:235`). When the error is raised from *inside a simulated process* — the realistic case, a model detecting an inconsistency mid-run — the longjmp leaps from the coroutine's stack back to the worker thread's stack, but `coroutine_current` is never reset to `coroutine_main`.

*Repro 6 (`repro6_error.c`):* trial 0's process calls `cmb_logger_error`; trial 1 then runs on the same worker. Observed: (a) `cmb_process_current()` returns a dangling pointer in trial 1's dispatcher, and the trial 1 log lines are mislabeled with the dead process's name; (b) the first context transfer of trial 1 aborts on the stack-validity debug assert, because the dispatcher's thread-stack RSP gets saved into the stale coroutine struct. In release builds the asserts are gone and trial 1 proceeds with a corrupted `current`/`caller` chain — silent state corruption in the very feature meant to contain failures.

**Fix:** in the recovery branch of `worker_thread_func` (or at the start of each trial), reset `coroutine_current = coroutine_main` and the sanitizer fiber bookkeeping; consider also tearing down the failed trial's coroutine registry entries there rather than only at thread exit. Add a test that raises `cmb_logger_error` from inside a process and runs a subsequent trial on the same worker. Two related notes: glibc's `_FORTIFY_SOURCE` `__longjmp_chk` can spuriously abort cross-stack longjmps depending on address layout, worth a comment or a `siglongjmp`-free design; and the documentation should state explicitly that recovery does not unwind coroutine stacks (no resource release, like the memory-leak caveat already documented).

### F3 — A binary `cmb_resource` can be granted to two processes at once (HIGH for models with simultaneous events) [verified]

`cmb_resource_acquire()` has a fast path that grabs the resource whenever `rp->holder == NULL` (`src/cmb_resource.c:201`) — without checking whether the guard queue is non-empty — and after a successful `cmb_resourceguard_wait()` it calls `resource_grab()` unconditionally (`src/cmb_resource.c:216`), without re-checking availability.

Release works by scheduling a wakeup *event* for the chosen waiter at the current timestamp. Any event already scheduled at that same timestamp executes between the release and the wakeup (FIFO key order). If that intervening event calls `acquire()`, it sees `holder == NULL` and takes the fast path; the woken waiter then arrives with `CMB_PROCESS_SUCCESS` and grabs the resource on top of it.

*Repro 5 (`repro5_double.c`):* observed output — "2 holder(s) inside" a binary resource at t=10. In a debug build the inconsistency is caught later by a *debug* assert at release time; in the default release build (`-DNDEBUG`) that assert is compiled out, so the model silently runs with two holders: mutual exclusion violated, holder accounting and utilization statistics corrupted.

Note the asymmetry: `cmb_buffer`, `cmb_objectqueue`, and `cmb_resourcepool` all use a re-check loop around their waits and are immune to this; only the binary resource trusts the wakeup signal.

**Fix:** make the fast path require the guard queue to be empty (`cmi_hashheap_is_empty(&rp->guard)` — otherwise queue-jumping is possible even without the double-grant), and wrap the wait in the same recheck loop the other primitives use (on wakeup, if `holder != NULL`, wait again). Add a regression test with tied timestamps.

### F4 — The documented `cimba_run(..., NULL)` multi-trial-function mode segfaults (HIGH for that feature, trivially fixed) [verified]

Per the `cimba.h` documentation, passing `your_trial_func == NULL` makes the runner take the trial function from "the first member of each trial struct". The implementation (`src/cimba.c:187`) instead computes

```c
cimba_trial_func *trial_func = (cimba_trial_func *)(((char *)trial) + cmg_trial_struct_sz);
```

— that is, it takes the **address one past the end of the struct** (the *next* trial's first byte, or past the array for the last trial) and **casts the address itself to a function pointer** without dereferencing. *Repro 4 (`repro4_nullfunc.c`)* segfaults immediately. The feature can never have worked; neither call in `test_cimba.c` exercises the NULL path.

**Fix:** `cimba_trial_func *trial_func = *(cimba_trial_func **)trial;` plus a test using two different trial functions.

### F5 — Installed public headers are not self-contained: out-of-tree builds fail (HIGH for packaging) [verified]

`include/meson.build` installs the seventeen `cmb_*.h`/`cimba.h` headers, but ten of them `#include` private headers that live in `src/` and are never installed: `cmi_coroutine.h`, `cmi_slist.h` (which pulls `cmi_mempool.h` → `cmi_memutils.h`), `cmi_resourcebase.h`, `cmi_holdable.h`, `cmi_hashheap.h`, `cmi_config.h`. A consumer of an installed Cimba cannot compile `#include "cimba.h"` at all — verified by attempting exactly that during this review (it fails on `cmi_coroutine.h: No such file or directory`). Everything in-tree works only because the test/tutorial targets add `src/` to the include path.

Separately, `cmb_logger.h` uses `va_list` without including `<stdarg.h>`, so it is not self-contained even with the private headers available — also verified by a failed compile.

**Fix options:** (a) install the `cmi_*.h` headers the public API needs (cheapest, but exposes internals); (b) restructure so public headers only forward-declare and the structs that users embed move into public headers (more work, cleaner); at minimum add `<stdarg.h>` to `cmb_logger.h`. Add a CI step that compiles a one-line `#include "cimba.h"` program against a staged `meson install` tree — that single check would have caught this class permanently. A related portability note: the public headers use `[[noreturn]]`, `[[maybe_unused]]`, and `__FILE_NAME__`, which bind all *consumers* (not just the library build) to C23-mode GCC/Clang; a small compat shim (`#if` fallbacks to `_Noreturn` / `__FILE__`) would let MSVC and older-standard consumers link against the C ABI as the README suggests they can.

## High-likelihood defects found by inspection

### F6 — `guard_queue_check` is not a valid strict weak ordering (HIGH likelihood, MEDIUM-HIGH impact) [by inspection]

`src/cmb_resourceguard.c:77`:

```c
if (a->rank_i64 > b->rank_i64) { return true; }
if (a->rank_d64 < b->rank_d64) { return true; }
if (a->hash_key  < b->hash_key) { return true; }
return false;
```

Unlike the correctly written `default_compare` (hash-heap) and `holder_queue_check` (resource pool), this comparator is missing the early `return false` branches for `a->rank_i64 < b->rank_i64` and `a->rank_d64 > b->rank_d64`. Consequences: a lower-priority process with an earlier arrival time compares as "before" a higher-priority later arrival; worse, for many pairs both `cmp(a,b)` and `cmp(b,a)` are true, so the heap invariant itself is ill-defined and the resulting order depends on insertion history. With *equal priorities* the comparator degenerates correctly to FIFO, which is why the shipped tests (and a first naive repro attempt in this review) pass — heads-up that incidental heap placement can mask this. Any model that actually mixes priorities in a waiting line gets an unreliable queue discipline, silently.

**Fix:**

```c
if (a->rank_i64 != b->rank_i64) return a->rank_i64 > b->rank_i64;
if (a->rank_d64 != b->rank_d64) return a->rank_d64 < b->rank_d64;
return a->hash_key < b->hash_key;
```

plus a property test that shuffles mixed-priority/mixed-time waiters and checks dequeue order.

### F7 — Windows TEB `DeallocationStack` offset is written in decimal: `[gs:1478]` should be `[gs:0x1478]` (MEDIUM-HIGH) [by inspection]

`src/port/x86-64/windows/cmi_coroutine_context.asm:47,158,179`. NASM treats `1478` as decimal (= `0x5C6`). The x64 TEB's `DeallocationStack` is at offset **0x1478** (cf. boost.context's `0x1478` in its MS PE fiber code). Offset 0x5C6 falls inside the TEB's `GdiTebBatch` area, and it isn't even 8-byte aligned. The other two offsets (`gs:8`, `gs:16` for StackBase/StackLimit) are correct because they coincide in decimal/hex.

Practical effect: the context switch reads and writes a consistent-but-wrong TEB slot, so coroutine switching still works for console programs (the same wrong slot round-trips), which is why CI passes — but (a) the real `DeallocationStack` is never updated, so any Windows runtime facility that consults it sees the wrong stack, and (b) every context switch scribbles 8 bytes into the GDI batching area, which is live state for any thread that touches GDI — and the README explicitly suggests driving graphics from Cimba models. **Fix:** `0x1478` in all three places; verify on Windows with a GUI-adjacent smoke test. (Same pattern check recommended for `cmi_coroutine_stackraw` consumers.)

## Medium-severity findings

**M1 — Hash-map tombstones are never reclaimed except on growth** (`src/cmi_hashheap.c`, remove/dequeue paths). Lazy deletion leaves `hash_key != 0, heap_index == 0` tombstones; insertion reuses a tombstone only when probing happens to land on one, and the map is rebuilt only when the *heap* fills. In steady-state churn — which is exactly what timeout-style models produce, since every successful acquire cancels its timeout — truly-empty slots monotonically decay toward zero, after which every miss lookup (`cmb_event_is_scheduled`, cancel of an already-fired event) degrades to a full O(2N) wrap-around scan and hit lookups grow long probe chains. The O(1) hash claim quietly erodes over a long run. **Fix:** track a tombstone count and rehash in place when tombstones exceed, say, the live count; this is a small addition to the existing `hash_rehash` machinery. Given the changelog says recent speed wins came from this hash map, a churn benchmark (schedule+cancel mix over hours of simulated time) is worth adding.

**M2 — `cmb_resource_preempt` cancels the *preemptor's* awaitables, not the victim's** (`src/cmb_resource.c:292`, `cmi_process_cancel_awaiteds(pp)` where `pp` is the caller). A preempting process that has, e.g., a deadline timer set loses it silently — and `cancel_awaiteds` ends with `cmb_event_pattern_cancel(CMB_ANY_ACTION, pp, CMB_ANY_OBJECT)`, which cancels *every* pending event whose subject is the caller. The victim, meanwhile, is cleaned up only via the resume-signal discipline. This looks like a `pp`/`victim` mix-up; given F1, even `victim` would currently fail to leave the guard queue correctly. **Fix:** decide the intended semantics, name the target explicitly, and test preemption while both parties hold timers.

**M3 — Interrupting a process that has already finished aborts in release builds.** `wakeup_event_interrupt` only debug-asserts `status == RUNNING`; in release the call flows into `cmi_coroutine_transfer`, whose *release* assert on the target status fires. The sibling wakeup handlers (`wakeup_event_event`, `wakeup_event_process`, `wakeup_event_resource`) all check-and-warn on dead targets instead. Interrupting a process that happens to exit at the same timestamp is a perfectly ordinary race in a model and shouldn't be fatal. **Fix:** make `wakeup_event_interrupt` use the same check-and-warn pattern.

**M4 — Logger configuration is thread-local but documented as global.** `cmi_logger_mask` and the time-formatter pointer are `CMB_THREAD_LOCAL`, so `cmb_logger_flags_off(...)` or `cmb_logger_timeformatter_set(...)` called from `main` before `cimba_run` has no effect inside worker threads. This was observed directly in repro 6: INFO lines printed from the worker despite `cmb_logger_flags_off(CMB_LOGGER_INFO)` in `main`. Neither the header nor the tutorial mentions this. **Fix:** either make the mask a process-global (atomic load is cheap enough at log call sites), or seed each worker's TLS from a global template at thread start, or document loudly that logging must be configured per-trial.

**M5 — `cimba_trials_remaining()` underflows.** Each worker increments `cmg_next_trial_idx` once past the end before exiting, so after completion `cmg_total_trials - nxt` wraps to a huge `uint64_t`. **Fix:** clamp (`nxt >= total ? 0 : total - nxt`).

**M6 — Wrong `format(printf, ...)` index on `cmb_logger_vfprintf`** (`include/cmb_logger.h:127`): argument 3 is `func`, the format string is argument 5. As written, compilers silently format-check the wrong parameter, so user format-string mistakes in logging calls go undiagnosed. Should be `format(printf, 5, 0)` (and the `cmi_logger_*` variadic wrappers deserve `format(printf, 4, 5)` attributes of their own; their `char *fmtstr` parameters should also be `const`).

**M7 — Unchecked system-call results in the runtime spine.** `pthread_create` in `cimba_run` (`src/cimba.c:246`) is unchecked — under thread-count or memory pressure a failed spawn leads to joining an uninitialized handle. `mprotect` in `cmi_coroutine_stack_free` is unchecked, and that call also re-protects the guard page with `PROT_READ|PROT_WRITE|PROT_EXEC` — the `PROT_EXEC` is unnecessary, violates W^X expectations, and will be refused on hardened kernels, after which `free()` of a still-PROT_NONE page misbehaves. **Fix:** assert/handle the return values; drop `PROT_EXEC`.

**M8 — Coroutine/registry thread cleanup assumes heap origin.** `cmi_coroutine_thread_cleanup` calls `cmi_free(cp)` on every still-registered coroutine. That is correct for `cmb_process_create`-allocated processes (the coroutine is the first member, same address), but the create/initialize split explicitly invites embedding processes in larger structs or on the stack/arena — repros in this review did exactly that — and any such process leaked into thread exit (e.g., after an F2-style failed trial) would be `free()`d at the wrong address. **Fix:** either document that initialized processes must be heap-allocated via the provided create functions, or store an "owned" flag set by `*_create`.

## Low-severity and polish items

These are individually small; listed compactly for triage. (a) `cmi_hashheap_hash_print` iterates `ui = 1 .. 2*heap_size` inclusive (`cmi_hashheap.c:892`): skips slot 0 and reads one past the end — harmless today only because allocations are page-rounded. (b) `cmi_hashheap_clear` sizes the memset with `heap_size + 2` (`cmi_hashheap.c:158`) where `initialize`/`grow` use `+ 1`; currently saved by page rounding, but a fragile inconsistency. (c) `1u << hp->heap_exp_cur` is a 32-bit shift assigned to a 64-bit size in `initialize`/`grow`; use `UINT64_C(1) <<`. (d) In `cmb_logger_vfprintf` the final newline uses `r += fprintf(...)` instead of `r = ...`, double-counting the previous chunk in the return value. (e) `cmi_offset_of` is a null-pointer-deref `offsetof`; use `<stddef.h>` `offsetof`. (f) `cmi_calloc` takes `unsigned n` while callers pass 64-bit counts. (g) The NASM objects carry no GNU property notes, so linking them disables CET (IBT/shadow stack) for any host application — which is also the only reason the `pop r9; jmp r9` "CET-friendly" return and the un-annotated indirect jumps are safe; worth an explicit statement of the security posture (the Windows build already passes `-fcf-protection=none` deliberately). (h) `cmi_rdseed` retries unboundedly; AMD errata make a bounded retry with `rdrand`/clock fallback more robust. (i) `cmb_random_dice` goes through `double`, which is biased/lossy for ranges beyond 2^53 and can overflow `b - a + 1`; Lemire's bounded-integer method would be exact. (j) The benchmark comment in `meson.build` says `-Dbenchopt=true` but the option is a combo (`generate`/`use`); and `-fno-omit-framepointer` (missing hyphen) in `benchopt_gen_flags` is silently dropped by `get_supported_arguments`.

Documentation drift worth a sweep: the changelog references `cimba_set_thread_hooks()` (actual: `cimba_thread_hooks_set()`); `cimba.h` references `cimba_workers_num()` (actual: `cimba_threads_num()`); `cmb_random()` is documented as `[0, 1]` but produces `[0, 1)`; `cmb_random_std_normal` says "standard deviation 0"; the Linux trampoline comment block describes a `cmi_coroutine` field layout that doesn't match the struct (the code doesn't use it, but it will mislead the next porter); `cmi_coroutine.h` suggests "10 kB could be enough" for stacks, but any non-page-multiple size release-asserts in `cmi_aligned_alloc` (the public process API is safe because it hard-codes 64/128 KB — but then offers no way to choose a stack size at all, which is its own gap for deep models); "stattus buts" typo and an inverted comment in `cmi_coroutine_registers_valid`; `holder_queue_check`'s comment says `rank_d64` where the code uses `rank_i64`; `cmb_process_name_set`'s comment promises a return value the function doesn't have. Finally, `cmi_process_cancel_awaiteds` ending with a wildcard `cmb_event_pattern_cancel(ANY, pp, ANY)` means an interrupt silently cancels *user-scheduled* events whose subject is that process — defensible, but it should be documented as part of interrupt semantics, as should the deadlock hazard inherent in the resource pool's documented greedy partial-acquisition strategy (two processes each holding partial amounts wait forever; SimPy-style all-or-nothing is the safer default for naive users).

## Performance

The performance architecture is convincing: thread-local state with `initial-exec` TLS, LTO, pool allocators on hot paths, a cache-friendly 64-byte heap tag, lazy hash activation, and branch-specialized comparators. The published 32M events/s single-core figure is plausible given this design (not re-benchmarked here). Two concrete improvement opportunities: first, M1 above is the only finding that threatens the headline performance over long runs. Second, the Linux context switch saves and restores RFLAGS with `pushfq`/`popfq` (and, when `NMXCSR` is unset, MXCSR); since `cmi_coroutine_context_switch` is called as an ordinary C function, the ABI already treats flags as clobbered, so the `pushfq`/`popfq` pair is pure overhead on the hottest path in the library — boost.context omits it for this reason. (Keeping MXCSR/x87-CW handling optional, as done via `NMXCSR`, is reasonable; note the x87 control word is technically callee-saved too and currently never saved even with `NMXCSR` unset.)

## Tests and CI

What exists is good: per-module unit tests with golden-file comparison, an assert-tripping harness that verifies `NDEBUG`/`NASSERT` semantics across build types, a statistical end-to-end M/G/1 test against theory, sanitizers in CI with appropriate timeout multipliers, and a Windows MinGW job. All 22 tests pass locally in debug, and the unit suite is ASan/UBSan-clean.

The gap is coverage of interaction edges, and it maps one-to-one onto the verified defects: `priority_set` is only ever tested on a running process changing itself, never on a queued waiter (F1); stop/interrupt are tested, but not against a process inside a guard queue (F1); `cmb_logger_error` is only raised from the trial function, never from a process (F2); both `cimba_run` calls pass a non-NULL function (F4); no test creates tied-timestamp contention on a binary resource (F3); and nothing compiles against an installed tree (F5). Golden-file tests also can't catch *silent* failures like the lost wakeup — the run completes and prints plausible output. Recommended additions, in order of value: an install-and-compile smoke test in CI; a "torture" model that randomly mixes hold/acquire/timeout/interrupt/stop/priority-change across all primitives and checks invariants (single-holder, conservation of pool units, no waiter starved with the resource free) rather than output text; a property test for each heap comparator; and a long-churn benchmark guarding against M1-style decay.

## Prioritized recommendations

1. Fix F1 (guard key scheme) and F6 (comparator) together, with the four regression tests listed under F1 — these gate the "reliable for high-stakes work" claim most directly.
2. Fix F3 (binary resource recheck loop + queue-empty fast path) and F2 (reset `coroutine_current` on recovery).
3. Fix F4 (one-line dereference) and F5 (header packaging + CI install test).
4. Fix the Windows `0x1478` offset (F7) and verify on Windows.
5. Work through M1–M8, then the polish/doc list.

## Verification statement

This review verified, by building and executing the code on Linux x86-64: the project builds cleanly from source in debug and release configurations with GCC 13.3; the complete shipped test suite passes (22/22); and the unit tests run clean under AddressSanitizer and UndefinedBehaviorSanitizer, consistent with the project's public CI. The core data structures (hash-heap, memory pool, coroutine context switch on Linux, statistics, RNG) withstood detailed inspection well and several subtle pieces (sanitizer fiber annotations, one-pass moment algebra, ziggurat tables) were checked and found correct.

The review also demonstrated, with six minimal reproduction programs run against the debug build, the defects F1–F5 described above, of which F1 (resource-guard key mismatch: abort on priority change, lost wakeup on stop) and F2 (error-recovery corruption across trials) are high-severity correctness bugs in supported, documented API paths, and F5 makes the installed library unusable out-of-tree. Findings F6 and F7 were established by code inspection with high confidence and await maintainer confirmation.

Assessment: Cimba's engineering foundations are strong and its reliability tooling is well ahead of its peer group, but in its current state (3.0.0-beta, commit `57546f8`) the process-interaction layer has verified defects that can abort, deadlock, or silently corrupt simulations that use priorities, preemption, process termination, or in-model error reporting. I would not yet rely on this snapshot for high-stakes simulation work; with the prioritized fixes above and the corresponding regression tests, there is no structural reason it cannot meet that bar. The reproduction programs have been provided to the maintainer alongside this document.

*This is a point-in-time review of the specified snapshot by Claude (Anthropic). It reflects build-and-test evidence on Linux x86-64 plus source inspection; Windows behavior, CUDA paths, tutorials, and prose documentation were not exhaustively reviewed. It is not a certification or guarantee of fitness for any purpose.*
