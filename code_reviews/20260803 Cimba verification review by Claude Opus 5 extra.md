# Cimba Verification Review — Stale-Wakeup Family, Lifetime Safety, and Reproducibility

**Reviewer:** Claude (Anthropic), independent automated review
**Date:** 3 August 2026
**Subject:** Cimba discrete event simulation library, version 3.0.0-beta
**Snapshot:** `main` at `e3a1294`, post-fix tree following the 6 July review
**Platform exercised:** Linux x86-64, GCC 13.3.0, NASM 2.16, Meson 1.11 / Ninja
**Method:** Build-and-run verification. The tree was built in release, debug,
debug+ASan/UBSan, and ThreadSanitizer configurations; the full test suite was run
(23/23 pass); each defect was re-checked with the same reproduction programs that
demonstrated it originally, plus new probes written during the fix campaign; and
the staged install tree was re-verified. Findings marked **[verified]** were
demonstrated by running code. Windows and CUDA paths were not exercised.

---

## Purpose

This is a follow-up to the 6 July review (`20260706 Cimba code review by Claude
Fable 5.md`), which found a high-severity defect family — **stale wakeup events**
— plus four medium-severity items and a list of polish items. That review named
the stale-wakeup family as the gate for an official 3.0, on the grounds that the
affected idiom (a timeout armed alongside a blocking wait) is one the library's
own tests demonstrate, that the failures were silent under every sanitizer, and
that the consequences — spurious signals, corrupted timing, stranded waiters,
release-build aborts — strike exactly the models that would choose Cimba for
serious work.

This review confirms, by re-running the original reproductions against the current
tree, that **the entire stale-wakeup family is resolved**, that the four
medium-severity items and essentially all polish items are closed, and that the
fix campaign additionally surfaced and closed a set of defects that the 6 July
review did not reach — including a **reproducibility defect that made seeded runs
non-deterministic across machines**, which for a stochastic simulation library is
arguably more consequential than any single crash.

One item from the 6 July review remains open by the maintainer's explicit
decision, and is discussed in its own section below.

---

## Status of the 6 July findings

### R1 — stale resource-guard wakeups: spurious resumes, cascading timer corruption, stranded waiters — RESOLVED [verified]

The fix is the keyed-validity design: a grant wakeup now carries the globally
unique enqueue key of the wait it was issued for, and the handler resumes only if
the process still holds the matching awaitable.

```c
static void wakeup_event_granted(void *vp, void *arg)
{
    struct cmb_process *pp = (struct cmb_process *)vp;
    const uint64_t key = (uint64_t)arg;
    if (!cmi_process_awaiting_key(pp, key)) {
        /* Stale, just discard */
        return;
    }
    ...
}
```

Two supporting pieces landed with it: the signal set was split across two handlers
(`granted` / `cancelled`) so the `object` slot is free to carry the key without
widening the 256-bit event, and `cmb_resourceguard_wait` now re-signals the guard
when it leaves a wait it had already been dequeued from, so an abandoned grant is
handed to the next eligible waiter instead of being lost.

Re-running the original reproduction (`repro_a_stale_grant.c`: holder releases at
the exact instant a waiter's timeout fires, with a second waiter queued behind):
the waiter now returns `CMB_PROCESS_TIMEOUT` and its subsequent 10-unit and
20-unit holds run their **full duration** (previously the first returned
instantly with `SUCCESS` and the second was cut short by the orphaned timer), and
the second waiter **acquires the freed resource and finishes** (previously it
remained `RUNNING`, queued forever at a free resource). Clean in both release and
debug+ASan/UBSan builds.

### R2 — `cmb_process_wait_event` leaves both registrations behind; release-assert abort — RESOLVED [verified]

Both halves are fixed. `cmb_process_wait_event` now deregisters from the event's
waiter list and drops its EVENT awaitable on **every** exit path, not only the
success path; and `cmi_event_remove_waiter` is now tolerant of an event that has
already left the queue, returning `false` instead of release-asserting.

`repro_b_event_wait.c` part 1: the waiter times out at t=5 and its subsequent
100-unit hold now runs to completion (previously it was cut short at t=10 when the
event fired and the stale registration resumed it). Part 2, the abort case: a
process stopped at the same timestamp its awaited event fires now completes
without incident (previously `SIGABRT` in the **default release build**).

### R3 — `cmb_process_wait_process` leaves its waiter tag behind — RESOLVED [verified]

Same treatment: the wait deregisters from the awaited process's waiter list on any
exit, and `cmi_process_remove_waiter` no longer asserts a non-empty list — an
empty list is the normal state on the success path, because `wake_process_waiters`
has already drained it to deliver the wakeup.

`repro_c_wait_process.c`: the waiter times out at t=5 and its 100-unit hold runs
full length (previously truncated at t=10 when the awaited process finished).

### R4 — `cmb_logger_error` outside a worker terminates with exit status 0 — RESOLVED [verified]

The unarmed-recovery branch now calls `exit(EXIT_FAILURE)` rather than falling
through to `pthread_exit`, so an error in single-threaded use reports failure to
the shell.

### R5 — exported-symbol namespace pollution — RESOLVED [verified]

`nm -D` on the built library now shows exactly **one** defined global outside the
`cmb_` / `cmi_` / `cimba_` namespaces (`cmg_atexit_armed`, carrying an explanatory
comment), down from ten. The wakeup handlers that were previously `extern` for the
benefit of cross-module pattern-cancel calls are now either `static` or properly
prefixed.

### R6 — empty-queue release asserts in front of graceful not-found paths — RESOLVED [verified]

The count asserts are gone from `cmb_event_cancel`, `cmb_event_reschedule`, and
`cmb_event_reprioritize`, so cancelling a stale handle now takes the documented
`is_enqueued`-returns-false path instead of aborting. The one remaining instance
is in `cmi_event_add_waiter`, which legitimately requires the event to exist.

### R7 — inconsistent wakeup handlers for guard waits — RESOLVED [verified]

`wakeup_event_condition` no longer strips the first RESOURCE awaitable by NULL
wildcard before resuming; it validates by key like its resource siblings and
leaves removal to `cmb_resourceguard_wait`. This closes the latent path by which a
stale condition wakeup could orphan a *different* guard's queue entry.

### Polish items (a)–(i)

- **(b) self-preemption** — `cmb_assert_release(victim != cmb_process_current())`. Fixed.
- **(c) redundant `remove_awaitable`** after `timer_cancel` in `hold`. Fixed.
- **(d) stale `timer_add` comment**. Fixed.
- **(e) `wakeup_event_time` hard-asserting `RUNNING`** — now check-and-warn, aligned with its siblings. Fixed.
- **(f) `cmb_resource_terminate` with waiters queued** — now `cmb_assert_release(count == 0)`. Fixed.
- **(g) `cmb_resource_release` holder check** — promoted to `cmb_assert_release`. Fixed.
- **(h), (i)** — documentation notes; not separately verified here.
- **(a) test-suite leak hygiene** — see "Open item" below.

---

## Additional defects found and fixed during the campaign

These were not in the 6 July review. Several are more serious than the polish
items above, and two are result-biasing in the sense that matters for simulation
work.

### Non-reproducible results across environments — RESOLVED [verified]

`cmb_resourcepool` used the **process pointer address** as the hash key for its
holders heap. Because the hash-heap comparator falls back to `hash_key` as its
final tiebreaker, this made preemption victim selection depend on memory layout:
among equal-priority holders, which process got preempted was decided by where
`malloc` had placed it. The result was that a seeded run was deterministic on one
machine but **could not be reproduced on another** — the symptom that first
surfaced as a CI failure whose seed ran clean locally.

For a library whose entire value proposition includes "same seed, same result,"
this is a foundational defect, and it also masked the accounting bug below by
making it appear and disappear between environments.

The fix adds a `handle` field to `struct cmb_process`, assigned from a
thread-local monotonic counter at initialize and cleared at terminate, and
replaces all seven pointer-key sites in `cmb_resourcepool.c`. A dedicated
comparator (`holder_queue_check`) now orders holders by ascending priority, then
by descending acquisition timestamp, then by descending handle — a valid strict
weak ordering, total because handles are unique, and independent of address
layout. Verified: three consecutive runs of `test_resourcepool` with a fixed seed
produce byte-identical output, and the CI seed that failed on the runner now runs
clean here.

A useful side effect: because handles are monotonic and never reused, a recycled
process address cannot alias a stale pool record.

### Pool preemption did not mirror binary-resource preemption — RESOLVED [verified]

`cmb_resource_preempt` cancelled the victim's awaiteds before interrupting it;
`cmb_resourcepool`'s preempt loop did not. A process that was simultaneously a
*holder* and a *queued waiter* could therefore be preempted out of its holdings
while a grant wakeup was already scheduled for it — and because that grant carried
the lower FIFO key, it fired first, returning `SUCCESS` from a wait the process had
been preempted out of. The model's accounting and the pool's then disagreed
permanently.

This was reproducible from a seed once the reproducibility fix landed, and is
exactly the class of bug the 6 July review warned about: silent, result-biasing,
and invisible to a golden-file test unless the timestamps collide. Both call sites
now cancel the victim's awaiteds, and both preempt on strictly-lower priority
(previously the binary resource used `>=`, permitting peer-priority thrashing).
Verified: 200 consecutive `test_resourcepool` runs with random seeds, zero
failures.

### Use-after-free on process destruction — RESOLVED [verified]

The keyed-validity checks introduced by the R1–R3 fixes dereference the target
process, which created a new lifetime requirement: no wakeup event may outlive the
process object it names. Three probes written during the campaign each
demonstrated a heap-use-after-free in the default debug build —
`wakeup_event_granted`, `wakeup_event_process`, and `wakeup_event_occurred` each
firing after `cmb_process_destroy` had freed its target.

The tree now carries `cmi_resource_cancel_wakeups`, `cmi_resourceguard_cancel_wakeups`,
and `cmi_event_cancel_wakeups`, invoked from the destruction path, so the
sweeps that were removed as redundant for *staleness* are retained where they are
required for *lifetime*. All three probes now run clean under ASan.

### Dangling registrations on `cmb_process_terminate` — RESOLVED [verified]

`terminate` previously tore down `pp->awaits`, `pp->waiters`, and `pp->resources`
with raw list teardown, discarding the entries without deregistering from the
structures that held back-references. A process terminated while queued left a
dangling entry in a guard's hash-heap (crashing the next `release`), and one
terminated while holding a resource never dropped it, stranding every waiter
behind it. Both were **pre-existing** and independent of the stale-wakeup work.

`terminate` now deregisters properly, wakes any process waiters, and drops held
resources through the virtual `drop` method (which re-signals the guard). It also
carries a new release precondition that the process is not `RUNNING`, making
stop-before-terminate a checked contract rather than an assumption. Verified via
the corresponding probes, run through the stop-then-terminate path the new
precondition requires.

### Declared-but-undefined public API — RESOLVED [verified]

Three functions were declared and documented in public headers with no definition
anywhere in the library: `cmb_condition_cancel`, `cmb_condition_remove` (defined
under `cmi_` names, so unreachable to any consumer), and `cmb_dataset_merge` (no
definition at all). Any program calling them failed at link time.

All three now export. `cmb_dataset_merge` was implemented with a temp-and-swap
strategy that handles every aliasing case uniformly — including `merge(t, t, t)`,
which an in-place append cannot do without violating `cmi_memcpy`'s `restrict`
contract — and was verified against a 26-check matrix covering all five aliasing
combinations, empty sources, and post-merge usability, clean under ASan with leak
detection enabled. The two condition helpers were also carrying a type-confused
cast (`(struct cmb_resourceguard *)cvp` on a struct whose guard is its *second*
member); this is fixed, and both were exercised for the first time.

A structural follow-up landed with this. The header probes in
`test/tools/verify_install.sh` cannot catch this class on their own — a
declaration with no definition compiles perfectly, and the install-guide
`hello.c` calls only `cimba_version()`. The script now adds a step that forces
the linker to resolve **every** public function declared in the installed
headers, by taking each address in a static initializer. Confirmed green in CI
against the installed tree:

```
verify_install: checking every declared public function resolves
  all 203 declared public functions resolve against the installed library
```

The step was also validated negatively before landing: injecting a phantom
declaration into a copy of the headers produces the expected undefined-reference
failure. A declared-but-undefined public function can therefore no longer reach a
release. That is the kind of fix that prevents the class, not just the instance.

### Sundry

- The pool's `reprioritize_holder` no longer clobbers the acquisition timestamp with `0.0` on every priority change.
- Two `cmb_logger_info` calls were missing their `FILE *` argument — invisible in release (the macro compiles out) but a format-string defect in debug.
- The `linux-asan` and `linux-ubsan` CI jobs each carried options for the sanitizer they do not build; empirically confirmed dead (`nm -D` shows no `__ubsan` symbols in the address-only build).

---

## Regression and sanitizer status [verified]

- **Builds:** release, debug, debug+ASan/UBSan, and ThreadSanitizer all compile **warning-free** at `warning_level=3`.
- **Test suite:** 23/23 pass in release.
- **ASan+UBSan:** the twelve core unit tests pass clean.
- **ThreadSanitizer:** `test_process` and `test_resourcepool` pass clean.
- **Leak detection:** every test binary reachable in this environment is now leak-clean, including `test_condition` and `test_process`, which leaked at the time of the 6 July review.
- **Install tree:** every public header compiles standalone, out-of-tree, in **C11** mode; `pkg-config` file generates correctly.
- **Determinism:** fixed-seed runs are byte-identical across repeated executions.
- **Stress:** 200 randomly-seeded `test_resourcepool` runs, zero failures.

The multithreaded end-to-end test (`test_cimba`) was confirmed progressing
correctly through 120+ trials under ASan but was not run to completion in this
single-vCPU container; CI covers that configuration with a timeout multiplier.

---

## Open item

**Test-suite leak hygiene under the trial-abandonment path.** Polish item (a) from
the 6 July review is closed for the test harnesses themselves — all of them are
now leak-clean. What remains is a **library** matter that the leak-checking work
brought to light.

When a trial calls `cmb_logger_error` from inside a process coroutine, recovery
`longjmp`s back to the worker loop, abandoning everything the trial allocated. The
worker's recovery branch documents this explicitly, and `test_cimba` deliberately
exercises it. In a long campaign this leaks without bound: at a realistic failure
rate over thousands of trials each holding a few megabytes of history, the failure
mode is exhaustion partway through an overnight run — precisely the workload this
library targets. Measured on the current CI test, 14 abandoned trials leaked
421 MB.

LeakSanitizer is currently **disabled** in CI to keep the pipeline green while this
is designed. The agreed direction is a per-trial registry of library-allocated
objects, torn down on abandonment, with model code remaining responsible only for
its own allocations — a separation that keeps coroutine stacks and internal
structures out of the model author's concern. The design is understood; the work
is an audit of every `_terminate` precondition that assumes orderly shutdown, since
an abandoned trial violates them by definition.

This is the one item I would still hold an official 3.0 for. It is a bounded piece
of work, well understood, and does not affect result correctness — only long-run
memory behaviour.

### Non-blocking observations

- `cmi_coroutine.h` is installed (correctly, since `cmb_process.h` includes it) but is not self-contained: it uses `size_t` and `bool` without including `<stddef.h>` / `<stdbool.h>`. Harmless in practice, since it is only reached through a public header that provides them, but it would fail a direct include.
- A single `cmb_process_destroy` now performs on the order of ten full scans of the event queue, each allocating a temporary array sized to the whole heap. A thread-local scratch buffer is planned; consolidating the sweeps into one pass is a larger follow-up.
- `CMB_PROCESS_TERMINATED` has been removed, and terminate-with-waiters is now a checked precondition instead. Consistent, and the stronger contract is the better choice.

---

## Assessment

Every defect reported in the 6 July review is resolved, verified by re-running the
programs that originally demonstrated them. The stale-wakeup family — which that
review named as the release gate — is closed at its root rather than patched at
each symptom: completion wakeups now validate against the wait they were issued
for, and the lifetime requirement that validation created has been met with
targeted teardown sweeps.

The campaign also closed several defects the 6 July review did not reach. Two of
them matter more than their line counts suggest. The pointer-keyed pool heap made
seeded runs non-reproducible across machines, which undermines the basic contract
of a stochastic simulation library and had been silently biasing preemption
victim selection. The missing preempt mirror in the pool produced permanently
inconsistent resource accounting in models that combine holding and queueing —
silent, result-biasing, and invisible to golden-file testing. Both are now fixed
and both are now reproducible-from-seed, which is itself the more durable
improvement.

**Assessment:** I see no outstanding correctness or memory-safety barrier in the
exercised paths to relying on this snapshot for high-stakes work. The engine, the
resource layer, the numerics, the packaging, and the CI matrix are in good order,
and the fix quality throughout this campaign has been high — several fixes closed
the class rather than the instance, the install link-check being the clearest
example. The single remaining release-blocker is the trial-abandonment leak, which
affects long-run memory behaviour rather than results, and whose design is
settled. With that in place, and with the tied-timestamp regression matrix landed
to pin the behaviours verified here, I see no structural obstacle between this
snapshot and a trustworthy 3.0.

As always, continued caution is appropriate for the Windows port and the CUDA
tutorials, neither of which was exercised in this review, and for code paths
beyond the test suite.

---

*This is a point-in-time review of the specified snapshot by Claude (Anthropic).
It reflects build-and-test evidence on Linux x86-64 plus source inspection. It is
not a certification or guarantee of fitness for any purpose.*
