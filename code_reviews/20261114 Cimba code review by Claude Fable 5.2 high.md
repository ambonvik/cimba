# Cimba Code Review — Release 3.1.1, Fresh Full Pass

Reviewer: Claude Fable 5.2 (Anthropic), 14 November 2026
Subject: `cimba-3.1.1` release tarball (library, tests, tutorials, build files, docs sources)

## Scope and method

This is an independent full pass over 3.1.1, not a follow-up to an earlier review. I read
the earlier reviews in `code_reviews/` first so as not to repeat closed items, then read the
library source with the process, event, resource-guard and trial-recovery layers in full,
and the statistics and random-variate layers more selectively, since those were covered in
depth in June–September and are exercised by the statistical test battery.

Every finding below is marked **[verified]** (I wrote a small reproducer and observed the
behaviour on this release) or **[by inspection]** (reasoned from the code, not executed).
Reproducer programs are supplied separately as `cimba-3.1.1-reproducers.tar.gz`; they are
bug reports, not proposed test-battery code.

Environment: x86-64 Linux (`vm.max_map_count` = 65530), GCC 13.3 and Clang 18.1,
Meson 1.12, NASM, 2 cores.

What I ran:

| Configuration | Result |
|---|---|
| GCC release (LTO), `meson test` | 27/27 pass, zero compiler warnings at `warning_level=3` |
| GCC debug | builds clean; used for reproducers |
| GCC debug + ASan + UBSan + LSan, `meson test` | 27/27 pass, no reports |
| Clang release, `meson test` | 27/27 pass, two unused-static warnings (see L9) |
| Static library build | library builds; **`tutorial/hello.c` fails to link** (F3) |

What I did not run or check: Windows/MinGW (inspection only), the CUDA tutorials, TSan, the
PGO configuration, the Sphinx build, and the Read the Docs site and blog (I worked from the
tarball's `docs/*.rst`). I did not re-derive the distribution-level statistics; the
library's own goodness-of-fit tests pass.

## Executive summary

The core is in good shape. The event queue, hash-heap, coroutine switch and the
key-validated wakeups introduced during the July–August campaign hold up under a fresh
read, the sanitizer builds are clean, and the published test battery passes on two
compilers.

The defects I found cluster in four places that the test battery does not reach:

1. **Trial abandonment** (`cmb_logger_error` / `cimba_trial_abandon`). Two independent ways
   for one abandoned trial to take down the whole experiment: a segfault when registered
   objects are locals of the trial function (the pattern used in the tutorials), and a
   release assert when any process is queued at a `cmb_resource`.
2. **Packaging**. A non-inline function definition in `cimba.h` breaks linking for any
   model with two source files, and for any model at all against the static library. The
   Meson project version still says 3.0.0, and 3.1.1 changed the layout of a public struct
   under an unchanged SONAME.
3. **Same-timestamp interleavings.** Several paths assume nothing happens between "a wait
   is granted" and "the waiter runs". When another event lands in that window the results
   are a stranded waiter, a false success return, an abort, or a FIFO violation. These
   need tied timestamps, so they do not show in continuous-time M/M/1 tests, but they are
   deterministic in models with integer or scheduled times.
4. **Teardown ordering.** Destroying a process or terminating a resource at an awkward
   moment leaves a pointer behind that is dereferenced later.

Counts: 4 high, 11 medium, 9 low. Two of the items (F5, and the struct change in F15) are
regressions introduced by the 3.1.1 FIFO fix, and F3 by 3.1.0.

| ID | Severity | Summary | Status |
|---|---|---|---|
| F1 | High | Abandoning a trial segfaults if registered objects are locals of the trial function | verified |
| F2 | High | Abandoning a trial aborts the experiment if a process is queued at a `cmb_resource` | verified |
| F3 | High | `cimba_threads_use()` defined non-inline in `cimba.h`: multiple-definition link errors | verified |
| F4 | High | Pool acquire rolled back after a timeout/interrupt does not signal the guard: waiters stranded with free capacity | verified |
| F5 | Medium-high | Sticky `tiebreaker` breaks FIFO for direct users of `cmb_resourceguard_wait()` (3.1.1 regression) | verified |
| F6 | Medium-high | Stale process-completion wakeup makes a later `cmb_process_wait_process()` return success early | verified |
| F7 | Medium-high | `cmb_process_priority_set()` aborts while the target's grant is in flight | verified |
| F8 | Medium | New arrival at the grant timestamp overtakes the waiter that was just granted | verified |
| F9 | Medium | `PREEMPTED` from another resource during a pool acquire returns failure but keeps partial holdings | verified |
| F10 | Medium | `cmb_process_destroy()` leaves condition wakeups and start events scheduled: use-after-free | verified (ASan) |
| F11 | Medium | Success-valued resume or timer during a hold or guard wait: early return, orphan queue entry, stranded waiter | verified |
| F12 | Medium | Terminating a held resource leaves a dangling holder tag: segfault when the holder is stopped | verified |
| F13 | Medium | `cmb_condition_signal()` wakes equal-priority waiters in heap-array order, not FIFO | verified |
| F14 | Medium | Ceiling on live processes from guard-page mappings (`vm.max_map_count`), reported as a bare assert | verified |
| F15 | Medium | Version metadata stale at 3.0.0; public struct layout changed under the same SONAME | verified / by inspection |
| L1–L9 | Low | See "Low-severity findings" | mixed |

## Strengths

These are worth stating specifically, because several of them are why the remaining
defects are confined to edges.

- **Deterministic ordering is explicit.** Event order is (time, priority, FIFO key) in one
  comparator; `-ffp-contract=off` is set project-wide; per-thread counters (process
  handles, guard keys) are only ever compared relative to each other, so a trial's result
  does not depend on which worker thread ran it or what ran before it. I checked each
  thread-local that survives across trials for this property and found no leak of
  thread history into ordering.
- **The hash-heap is carefully built.** Lazy hash activation keeps pure enqueue/dequeue
  workloads off the hash map; tombstone accounting, rebuild-in-place and rehash-on-grow
  are consistent; slot 0 as the "current event" survives growth. I looked for a probe-chain
  inconsistency on key reuse and did not find one (it is correct as long as a key is never
  enqueued twice concurrently, which the callers respect).
- **Key-validated wakeups.** Guard grants, condition wakeups, event waits and timers each
  carry a unique identity and are discarded when stale. This design is sound; findings F6
  and F11 are the two places where it was not carried all the way through.
- **The coroutine layer is sanitizer-literate**: ASan fiber-switch annotations, TSan fiber
  handles, MXCSR inheritance, CET property notes, guard pages, a per-thread stack pool.
- **Three-tier asserts** with a build matrix that tests the tiers themselves.
- **Verification culture**: review/verification pairing, statistical tests for the
  variate generators, golden-output stochastic tests, an install-tree self-containment
  check, and a written AI policy that keeps accountability with a human author.
- Throughput is as advertised in kind: the single-threaded M/M/1 benchmark pushed 10^6
  customers through in about 0.2 s of CPU time on a shared 2-core sandbox.

## High-severity findings

### F1 — Abandoning a trial segfaults when registered objects live in the trial function's frame [verified]

`cmb_dataset_initialize`, `cmb_datasummary_initialize`, `cmb_wtdsummary_initialize`,
`cmb_timeseries_initialize` and the resource `_initialize` functions link a registry node
that is embedded in the object. If the object is a local variable of the trial function
(or a member of a local struct), that node lives in the trial function's stack frame.

On abandonment, `thread_worker_func` receives the `longjmp`, and then calls
`cmi_coroutine_recovery_finalize`, the user cleanup hook, and `cmi_memregistry_cleanup`.
Those calls build their frames in exactly the stack region the trial function has just
vacated, overwriting the nodes the cleanup is about to walk.

Reproducer: a trial function with `struct sim { struct cmb_datasummary waits; struct
cmb_dataset samples; struct cmb_datasummary svc; } sim;` as a local, initialized, and one
process that calls `cmb_logger_error()`. Result on release and debug builds:

```
Thread 2 received signal SIGSEGV
#0 cmi_dlist_unlink (cmi_dlist.h:135)
#1 cmi_dlist_remove_first
#2 cmi_memregistry_cleanup (cmi_memregistry.c:78)
#3 thread_worker_func (cimba.c:335)
```

This is the declaration pattern of `tut_3_1.c` (`struct simulation sim = {};` holding five
`cmb_datasummary` members) and the one `docs/tutorial.rst` introduces ("we just declare
these structs as local variables on the stack"). `tut_4_3` avoids it by moving to the heap,
but the text gives leak avoidance as the reason, not this.

Options, with trade-offs:

- *Keep the dead frames intact during cleanup.* Before anything else in the recovery
  branch, find the lowest registered node address that lies on this thread's native stack
  below the recovery frame, and run the remaining recovery steps from a helper whose frame
  starts below that address (a sized VLA or `alloca` pad). The objects are then still
  exactly as the trial left them. Small change; relies on nothing touching that region
  first, so it must be the first thing done after the jump lands.
- *Two-phase teardown.* In `cimba_trial_abandon()`, before the jump and while all frames
  are still live, tear down the registered items whose nodes lie on the worker's native
  stack; leave coroutine-stack and heap items for after the jump. This removes the
  dependency on stack contents surviving, but runs teardown code on a coroutine stack and
  out of strict LIFO order, so the demolition-mode rule in F2 (no cross-object calls)
  becomes a hard requirement.
- *Forbid it.* Document that objects registered during a trial must not live in the trial
  function's own frame, and add a debug check in `cmi_memregistry_cleanup` that no node
  address lies between the recovery marker and the stack limit, so a violation is a named
  assert instead of a segfault. Cheapest, but it contradicts the tutorials, which would
  need changing.

Side effect to watch with the first option: the pad size is bounded by the stack depth
the trial reached; a trial function with very large locals makes the pad correspondingly
large, which is safe (the memory was already in use) but should not be allowed to exceed
the remaining stack.

### F2 — Abandoning a trial aborts the experiment when a process is queued at a `cmb_resource` [verified]

`cmb_resource_terminate` (cmb_resource.c:112) asserts, unconditionally and at release
level, that the guard queue is empty:

```c
cmb_assert_release(cmi_hashheap_count(&(rp->guard.priority_queue)) == 0u);
```

During demolition, `cmb_process_terminate` deliberately skips `cmi_process_cancel_awaiteds`,
so any process that was waiting for the resource when the trial was abandoned is still in
that queue. Reproducer: one holder, one waiter, a third process calls
`cmb_logger_error()` at t = 5:

```
1  5.0000  BAD  bad (6):  Error: model detected an inconsistency, abandoning this trial
1  5.0000  dispatcher  cmb_resource_terminate (112):  Fatal: Assert
   "cmi_hashheap_count(&(rp->guard.priority_queue)) == 0u" failed
Aborted
```

The other three trials in the experiment are lost. Buffers, object queues, priority
queues, pools and conditions have no such assert and recover correctly.

Behind the assert there is a second problem on the same path **[by inspection]**: with
`rp->holder != NULL`, `cmb_resource_terminate` calls `resource_drop_holder`, which calls
`cmb_resourceguard_signal`, which evaluates the front waiter's demand, reads `pp->name`
and `pp->awaits`, and schedules an event. In demolition the registry is LIFO, processes
are normally created after resources, so both the holder and the waiter have already been
freed at that point.

Suggested direction: relax the assert with `|| cmi_memregistry_is_demolishing`, and in
demolition mode skip the drop/record/signal step entirely (clear `holder`, terminate the
history and the guard, nothing else). More generally, adopt as a rule that a `_terminate`
running under `cmi_memregistry_is_demolishing` touches only its own object. Side effect:
none on the normal path; on the abandon path a recording resource no longer gets a final
sample, which is immaterial since the history is being discarded.

### F3 — `cimba_threads_use()` is defined, not declared, in `cimba.h` [verified]

cimba.h:278–282:

```c
CMB_MAYBE_UNUSED
uint32_t cimba_threads_use(uint32_t n_threads)
{
    return cimba_threads_set(n_threads);
}
```

There is no `static inline`, so every translation unit that includes `cimba.h` emits an
external definition, and so does `cimba.c` inside the library (`nm -D libcimba.so` shows
`T cimba_threads_use`).

- Two user source files that both include `cimba.h`:
  `multiple definition of 'cimba_threads_use'` (reproduced with `-std=c2x` and `-std=c17`).
- Static library, single file: `gcc tutorial/hello.c libcimba.a` fails the same way,
  because `cimba.c.o` is pulled in for `cimba_version`.

The shared-library, single-file case links, which is the only case CI exercises
(`linux-install` builds `hello.c`; `verify_install.sh` compiles each header standalone).
The `tut_5_x` C + CUDA pairs escape because the `.cu` files do not include `cimba.h`.

Fix: `static inline`, matching the other deprecated wrappers (`cimba_run_experiment`).
Side effect: the library stops exporting the symbol. Anything compiled against 3.1.0/3.1.1
headers carries its own copy, so nothing breaks at load time, but a foreign-function
binding that looks the symbol up by name would lose it; if that matters, declare it
`extern` in the header and keep one definition in `cimba.c` instead. Either way, add a
two-file link test and a static-library link test to `verify_install.sh`.

### F4 — Pool acquire rolled back after a non-success signal does not signal the guard [verified]

In `acquire_inner` (cmb_resourcepool.c:571–606), when the wait returns a signal other than
`SUCCESS` or `PREEMPTED`, units grabbed during this call are returned to the pool. The
branch for a caller that already held some calls `cmb_resourceguard_signal`; the branch
for a caller that held nothing (lines 584–599) decrements `in_use` and returns without
signalling.

Reproducer (capacity 5): HOG holds 2 for a long time. BIG requests 5 at t = 1 with a 5.0
timeout, takes the 3 that are free and waits. SMALL requests 1 at t = 2 and queues behind
BIG. BIG times out at t = 6 and gives the 3 back.

```
t=  6.00 BIG    acquire returned -5, holds 0, pool in use 2 of 5
t=1000.00 HOG   released 2
t=1000.00 SMALL ACQUIRED 1
```

SMALL sits for 994 time units with three units free, and would sit forever if nothing
else released. In a model with reneging or timeouts on pool requests this silently
inflates waiting times.

Fix: signal the guard after the rollback in that branch as well; a single shared exit
path for "give back what this call took, then signal" would keep the two branches from
drifting again. Side effect: the signal schedules a wakeup at the current time, so event
sequences (and therefore random-number consumption order) change for models that hit this
path; reference outputs that exercise it will need regenerating.

## Medium-severity findings

### F5 — Sticky `tiebreaker` for direct users of `cmb_resourceguard_wait()` [verified]

The 3.1.1 fix for Issue #5 has `cmb_resourceguard_wait` set `pp->tiebreaker` on the first
wait of a request (cmb_resourceguard.c:148–151) and relies on each caller to reset it to
zero when the request completes. Every internal caller does. But `cmb_resourceguard_wait`
is public and documented as the building block for user-defined resources, its header
comment says nothing about the field, and `test_resourceguard.c`'s own "canonical guard
client" (`token_acquire`) never resets it.

Effect: a process that has waited once at any user-built guard keeps its first-ever key
as tiebreaker for the rest of its life, so it queues ahead of every equal-priority process
that arrived before it. Reproducer with a one-token pool: P1 waits at t = 1, is served,
re-requests at t = 8.5; P3 has been waiting since t = 6.

```
t=  6.00 P3  requests token
t=  8.50 P1  requests token again
t= 12.00 P1  GOT token (2nd time)      <-- ahead of P3
t= 13.00 P3  GOT token
```

Under sustained load this can starve P3 indefinitely. In a debug build, the same process
later calling `cmb_resource_acquire` trips `cmb_assert_debug(caller->tiebreaker == 0u)`.

Direction: move ownership of the field out of the callers. For example, give the guard
API an explicit "request" scope (begin / wait / end) that the built-in classes use, and
have plain `cmb_resourceguard_wait` treat each call as its own request. Side effect: if
plain `wait` resets the tiebreaker itself, the built-in looping callers must use the
scoped form, or Issue #5 returns.

### F6 — Stale process-completion wakeup resumes an unrelated `cmb_process_wait_process()` [verified]

`wakeup_event_process` (cmb_process.c:587–615) validates staleness by removing "the first
`PROCESS` awaitable, whichever it is". The wakeup carries the signal in its object slot,
not the identity of the process that finished. The comment in
`cmi_process_cancel_awaiteds` lists process wakeups among those validated at dispatch, but
this validation only checks that *some* process wait is active.

Reproducer: W sets a 10.0 timer and waits for A; A (higher priority) finishes at exactly
t = 10. A's exit schedules W's completion wakeup; W's timer, already in the queue, fires
first and W returns `TIMEOUT`. W then waits for B, which runs until t = 100.

```
t= 10.00 W  wait_process(A) returned -5
t= 10.00 W  wait_process(B) returned 0   (B status RUNNING)
```

W believes B has finished 90 time units early. The same hole is reachable through an
interrupt or preemption arriving in that window.

Direction: follow the event-wait pattern, i.e. one handler per outcome (finished /
stopped) so the object slot can carry the awaited process's `handle`, and validate against
the handle recorded on the awaitable. Use the handle rather than the pointer so a new
process allocated at the same address cannot match. Side effect: `cmb_process_destroy`
must sweep both handlers.

### F7 — `cmb_process_priority_set()` aborts while the target's grant is in flight [verified]

`cmb_resourceguard_signal` dequeues the waiter and schedules its wakeup; the waiter's
`RESOURCE` awaitable, with its `guard_key`, stays until it resumes. In that window
`cmb_process_priority_set` (cmb_process.c:289–300) calls `cmi_hashheap_drank(hp, key)` on
a key that is no longer in the heap:

```
10.000  S  cmi_hashheap_drank (675):  Fatal: Assert "idx != 0u" failed
```

Reproducer: H releases at t = 10; a supervisor whose hold also ends at t = 10 raises the
waiter's priority. `cmi_process_cancel_awaiteds` already knows about this state and
handles it; `priority_set` does not.

Fix: test `cmi_hashheap_is_enqueued` first and skip the reshuffle if not. Side effect: the
already-scheduled grant wakeup keeps the old priority; reprioritizing that pending event
too would be more exact but is optional.

### F8 — A new arrival at the grant timestamp overtakes the granted waiter [verified]

Same window as F7. Once the front waiter has been dequeued, the guard queue can be empty
while the resource is still free. `cmb_resource_acquire`'s `may_grab` test (line 251)
checks only "queue empty"; the pool, buffer and queue classes take whatever is available
without checking the queue at all.

```
t=  1.00 W1  arrives, requests R
t= 10.00 H   released
t= 10.00 X   arrives, requests R
t= 10.00 X   ACQUIRED R                <-- W1 has waited since t = 1
t= 11.00 W1  ACQUIRED R
```

Reproduced for `cmb_resource`, `cmb_resourcepool` and `cmb_objectqueue`;
`cmb_buffer` and `cmb_priorityqueue` have the same structure [by inspection]. The waiter
is not lost (it re-queues with its tiebreaker), but queue discipline is violated by one
position each time a release and an arrival coincide.

Options: (a) count grants in flight on the guard and have first-attempt takers treat a
non-zero count like a non-empty queue; (b) hand the resource over at grant time rather
than at resume time. Option (a) is the smaller change, but the counter must be decremented
on every exit — resume, stale-grant discard, stop, `cancel_awaiteds` — and a missed
decrement would make every later arrival wait at a free resource with an empty queue,
which is worse than the present defect. A debug invariant "counter equals the number of
pending grant events for this guard" (via `cmb_event_pattern_count`) would guard that.
Option (b) removes the window altogether for `cmb_resource` but does not generalize
cleanly to predicate-based guards.

### F9 — `PREEMPTED` from another resource during a pool acquire [verified]

`acquire_inner` treats `sig == CMB_PROCESS_PREEMPTED` as "my record in *this* pool was
taken", returns immediately and logs "returning empty-handed" (lines 564–570). The signal
can equally come from a preemption on a different pool or on a `cmb_resource`.

Reproducer: VICTIM holds 2 of A, requests 5 of B, takes the 3 available and waits. MUGGER
preempts A.

```
t=  5.00 VICTIM  acquire(B,5) returned -1; now holds A=0 B=3; B in use 5
```

The call reports failure but leaves three units of B held. A model that treats a failed
acquire as "nothing held" leaks that capacity until the process exits.

Fix: on `PREEMPTED`, check whether the caller still has a record in this pool; if so,
roll back as for other signals (and signal, per F4).

### F10 — `cmb_process_destroy()` leaves two kinds of event that dereference the process [verified]

The sweep at cmb_process.c:215–224 cancels resource, guard, event, interrupt, process and
resume wakeups. It omits:

- `wakeup_event_condition` (static in cmb_condition.c). Signal a condition, then in the
  same event stop, terminate and destroy the woken process. ASan:
  `heap-use-after-free ... cmi_process_awaiting_key ... wakeup_event_condition`.
- `start_event`. Start a process, then terminate and destroy it before the dispatcher
  runs it. ASan: `heap-use-after-free ... cmi_coroutine_start ... start_event`; without
  ASan, a segfault.

Related **[by inspection]**: `cmb_process_stop()` on a started-but-not-yet-dispatched
process logs "not running" and returns, leaving the start event in place, so the process
starts anyway.

The simplest robust form is one sweep on subject:
`cmb_event_pattern_cancel(CMB_ANY_ACTION, pp, CMB_ANY_OBJECT)`. That also retires the
deferred consolidation of the per-handler sweeps. Side effect: it also cancels
user-scheduled events whose subject is this process. Since the process is being freed
that is arguably correct, but it is a behaviour change and belongs in the changelog.

### F11 — Success-valued resume or timer during a hold or a guard wait [verified]

`cmb_process_hold` and `cmb_resourceguard_wait` infer "my own wakeup fired" from
`sig == CMB_PROCESS_SUCCESS`. Nothing prevents another source from delivering that value:
`cmb_process_resume(pp, CMB_PROCESS_SUCCESS)` is the idiom `tut_3_1.c` uses for yielded
processes, and `cmb_process_timer_add` accepts any signal.

- Hold: with a 5.0 timer carrying `SUCCESS`, `cmb_process_hold(10.0)` returns `SUCCESS`
  at t = 5 and leaves its own timer armed; that timer later wakes the process out of
  whatever it is doing next.
- Guard wait: P and W2 queue for R; another process calls
  `cmb_process_resume(P, CMB_PROCESS_SUCCESS)`. P's wait returns with its entry still
  enqueued. Debug builds assert at cmb_resourceguard.c:197. Release builds leave an orphan
  entry; when it later reaches the front it absorbs a signal and is discarded as stale:

```
t= 25.00 P  released R
t=1000.00   R is FREE, 1 entries still queued at its guard     <-- W2 never served
```

Direction: make both waits identify their own wakeup by state, not by value. After the
yield, `hold` cancels its timer if that timer is still among the process's awaitables;
`cmb_resourceguard_wait` cancels its entry if the key is still enqueued, whatever the
signal. Side effect: a hold woken early with `SUCCESS` still returns `SUCCESS`; if early
return should be distinguishable, that needs a new return value, which is an API change.
Documenting that `SUCCESS` is reserved for `cmb_process_yield()` targets would also help.

### F12 — Terminating a resource that is still held [verified]

`cmb_resource_terminate` calls `resource_drop_holder` for a remaining holder but does not
remove the holdable tag from that process's `resources` list. `cmi_holdable_terminate`
then sets `drop = NULL`. When the process is later stopped, `cmi_process_drop_resources`
calls through the tag:

```
resource gone, now stopping the process
Segmentation fault
```

This is the natural result of ending a run with `cmb_event_queue_clear()` while processes
are mid-service and then tearing down resources before processes. `cmb_resourcepool_terminate`
drops its holders heap without touching the holders' tags at all [by inspection].

Two further consequences on the same path [by inspection]: if the event queue has already
been terminated, `cmb_time()` is 0, so the final `record_sample` records a negative
duration (and trips a debug assert in `cmb_timeseries_add`).

Fix: have terminate remove its tag from each remaining holder, except in demolition mode
(F2). Alternatively assert on it and document the required teardown order; at present
neither is done.

### F13 — `cmb_condition_signal()` wake order among equal priorities [verified]

The first pass walks the heap array and schedules a wakeup per satisfied waiter. Event
keys are therefore assigned in array order, which is heap order, not queue order.
Five waiters W1..W5 arrive in sequence; W2's wait is cancelled (moving the last entry into
its slot); then the condition is signalled:

```
t= 20.00 W1 WOKEN   t= 20.00 W4 WOKEN   t= 20.00 W3 WOKEN   t= 20.00 W5 WOKEN
```

The order is deterministic, so reproducibility holds, but it is not the documented queue
order, and the first process to run usually takes whatever the condition was guarding.

Fix: collect the satisfied tags, order them with the guard's own comparator, then
schedule. Side effect: wake order changes, so condition-based reference outputs change.
(While there: the per-call `malloc`/`free` of `tmp` could use the same thread-local
scratch approach as the pattern-cancel buffers.)

### F14 — Ceiling on live processes from guard-page mappings [verified]

Each coroutine stack is carved from the heap and its guard page `mprotect`ed, which splits
the mapping: two kernel VMAs per stack, measured exactly (60 034 mappings at 30 000
processes). On a kernel with the long-standing default `vm.max_map_count = 65530` the
`mprotect` fails at about 32 700 stacks:

```
cmi_coroutine_stack_alloc (276):  Fatal: Assert "r == 0" failed
```

The limit is per OS process while the stack pools are per worker thread and never shrink,
so the budget is shared: 8 workers × 5 000 processes fails the same way (reproduced), and
on a 64-thread machine the ceiling is roughly 500 simultaneously live processes per trial.
Arch (since April 2024), Fedora (since 39) and Ubuntu (since 24.04) ship a raised default
of 1 048 576, so this will not show on a current desktop, nor presumably on the Ubuntu 24.04 CI runners
(I did not check their setting). The
kernel's own default is still 65530, and distributions that do not override it, which as
far as I know includes the RHEL family and Debian, are where cluster users tend to be.

Suggestions: report `errno == ENOMEM` from `mprotect` with a message naming the sysctl;
document the limit next to the stack-size discussion; consider a build or run-time option
to drop guard pages in favour of a canary word checked at context switch in debug builds.
Side effect of the last: without guard pages a stack overflow becomes silent corruption,
so it should be opt-in.

### F15 — Version metadata and ABI [verified / by inspection]

- `meson.build` line 4 still has `version : '3.0.0'`. The build therefore produces
  `libcimba.so.3.0.0`, a pkg-config file with `Version: 3.0.0`, and Sphinx/Doxygen
  configuration with `release = '3.0.0'`, while `cimba_version()` returns 3.1.1
  [verified]. A single source of truth (generate the header macros from Meson, or have
  Meson read the header) would prevent recurrence.
- `soversion` is fixed at `'1'`. 3.1.1 added the `tiebreaker` field in the middle of the
  public `struct cmb_process`, which user code embeds as a base class. An executable
  built against 3.1.0 headers binds to a 3.1.1 `libcimba.so.1` without complaint and
  then disagrees with the library about every field after `priority` [by inspection, from
  the changelog and header]. Source compatibility is intact, so this is consistent with
  SemVer at the API level, but it should be stated that there is no binary compatibility
  between releases, or the soversion should move whenever a public struct changes. This
  matters more once the library is packaged by third parties.

## Low-severity findings

**L1 — `cmb_event_current()` before the first event [verified].** Documented to return 0;
it returns `heap[0].hash_key`, which is uninitialized since the 3.0.3 removal of the
hash-heap `memset`. With `MALLOC_PERTURB_=165` it returns `0x5a5a5a5a5a5a5a5a`. Zero the
one tag at slot 0 in `cmi_hashheap_initialize`.

**L2 — Default worker count ignores CPU affinity and cgroup limits [verified].**
`cmi_cpu_cores()` uses `get_nprocs()`. Under `taskset -c 0`, `cimba_threads_num()` still
reports 2 while `nproc` reports 1. On a cluster node with a partial allocation this
oversubscribes heavily. `sched_getaffinity` + `CPU_COUNT` is the usual remedy. On Windows,
`GetSystemInfo` counts only the current processor group, so machines with more than 64
logical CPUs are under-counted [by inspection].

**L3 — `cmb_random_hwseed()` fallback logic [by inspection].** `seed ^= __rdtsc()` is
applied before the `seed == 0u` tests, so if RDSEED exhausts its retries and returns 0,
the seed is the bare TSC and neither the RDRAND nor the clock fallback is reached. The
same holds for the all-ones microcode bug the comment mentions. Test the raw RDSEED value
for 0 and all-ones before mixing.

**L4 — Windows port drift [by inspection].** (a) No stack pool: every process
initialize/terminate is a `VirtualAlloc` + `VirtualProtect` + `VirtualFree`, the syscall
pattern the Linux pool was built to avoid. (b) Stacks are `MEM_COMMIT`ted in full, so
commit charge is 132 KB per process whether touched or not. (c)
`cmi_coroutine_stack_free` takes one parameter on Windows but is declared and called with
two from `cmi_coroutine.c`; this works under the Win64 calling convention but is formally
undefined and would be flagged by LTO type checking.

**L5 — Public preconditions checked only at debug level.** Against the project's own
"release assert = public precondition" rule: `cmb_process_interrupt` (`pp`, `sig != 0`),
`cmb_process_resume` (`pp`), `cmb_process_timers_clear` (`pp`), `cmb_process_exit` from
the dispatcher (NULL dereference in release), `cmb_condition_destroy` and
`cmb_objectqueue_destroy` (terminate-first check). `cmb_condition_wait` dereferences
`caller` inside a debug assert before the guard's release assert can report a call from
the dispatcher, so a debug build segfaults where a release build gives the message.

**L6 — Slots lost from thread-local pools [by inspection].** `cmb_event_queue_clear()`
discards events without returning their waiter tags; abandoned trials do not return
awaitable, holdable or waiter tags; `cmb_resourceguard_terminate` does not return
observer tags. All are bounded per trial and reclaimed at thread exit, so this is slow
growth over a long experiment, not a leak LSan will report.

**L7 — Recording memory is unbounded.** Every state change of a recording resource
appends 24 bytes to its time series. For long runs this dominates memory. A summary-only
recording mode (a running `cmb_wtdsummary`) would give the utilization figures at O(1)
memory.

**L8 — `cimba_run` details.** It starts all configured workers even when there are fewer
trials, and runs the user thread-init hook on each (relevant when the hook creates CUDA
streams). A nested call from inside a trial self-deadlocks on the experiment mutex rather
than asserting. `cimba_threads_inuse()` reads its counter without the atomic load its
writers use.

**L9 — Small items.**
- `cmb_logger.c` still declares `extern CMB_THREAD_LOCAL jmp_buf cmi_worker_recovery` and
  includes `<setjmp.h>`; the symbol no longer exists.
- An assert that fires inside a user time formatter while the logger mutex is held
  re-enters the logger and deadlocks instead of aborting.
- `cmb_dataset_sort` takes a `const` dataset and sorts it in place.
- The guard passes the tiebreaker through a `double` parameter via a union; it is
  bit-exact as built, but a `uint64_t` enqueue variant would not depend on that.
- Clang release build: `pacf_tolerance` (cmb_dataset.c:869) and `sum_holder_items`
  (cmb_resourcepool.c:409) are used only in debug asserts.
- CHANGELOG: "3.0,4", "cmb_random_fmix24", and 3.0.3 dated before 3.0.2.

## Tests and CI

The battery is strong on steady-state behaviour and on the statistics. Every verified
finding above sits in one of four gaps:

1. **No test abandons a trial with anything queued, held, or stack-allocated.** F1 and F2
   would each have been caught by a single such trial in `test_cimba`.
2. **No link test beyond one file against the shared library.** F3.
3. **No tied-timestamp testing.** F6, F7, F8, F10, F11 and F13 all need two events at one
   instant. A soak test in the style of the existing guard soak, but drawing times from a
   small integer set and mixing timers, interrupts, stops, priority changes and resumes,
   would reach most of them. Useful invariants to check at quiescence: no waiter queued at
   a guard whose demand is satisfiable; every guard entry belongs to a process with a
   matching `RESOURCE` awaitable and vice versa; per-priority service order equals arrival
   order.
4. **No teardown-order tests** (F12): terminate resources before processes, and after the
   event queue.

Smaller CI points: if the runners have the raised `vm.max_map_count`, they hide F14; a
job that sets it to 65530 and creates 40 000 processes would make the limit visible. An ABI check (for
example `abidiff` against the previous release) would have flagged the struct change in
F15.

## Suggested order of work

1. F3 — one-line fix; it blocks multi-file and static-library users outright.
2. F1 and F2 — together they decide whether trial abandonment can be relied on.
3. F4 — silent wrong waiting times in pool models with timeouts.
4. F5 — restores the guard API contract that 3.1.1 changed.
5. F6, F7, F10, F11 — the remaining wakeup-identity and lifetime holes; the fixes are
   local and share a theme.
6. F8, F9, F12, F13 — ordering and accounting at ties and at teardown.
7. F14, F15 and the low items.

Several of these fixes change the order of same-time events, and therefore the
random-number consumption order of any model that reaches them. Results stay
statistically equivalent but will not be bit-identical to 3.1.1; the stochastic reference
files will need regenerating, and the changelog should say so.

## Verification statement

All findings marked [verified] were reproduced on the unmodified 3.1.1 tarball with the
library built by its own Meson files, using only the public headers plus
`cmi_resourcebase.h` where a custom guard client was needed. Findings marked [by
inspection] were not executed and should be confirmed before acting on them. I made no
changes to the library. Windows, CUDA, TSan and PGO configurations were not exercised, and
the statistics and random-variate code was read selectively, so the absence of findings
there reflects less attention on my part, not a clean bill.
