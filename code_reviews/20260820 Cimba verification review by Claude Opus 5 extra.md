# Cimba Verification Review — Release Candidate 1

**Reviewer:** Claude (Anthropic), independent automated review
**Date:** 20 August 2026
**Subject:** Cimba discrete event simulation library, version 3.0.0-RC1
**Snapshot:** `main` at `e9094f2`
**Platform exercised:** Linux x86-64, GCC 13.3.0, NASM 2.16, Meson / Ninja
**Method:** Build-and-run verification. Four build configurations were compiled and
exercised (release, debug, debug + ASan/UBSan/LeakSan, and ThreadSanitizer); the
full test suite was run; the defects from the 6 July review were re-checked against
the programs that originally demonstrated them; the staged install tree was
re-verified; and two new probes were written for this review to exercise the
trial-abandonment machinery introduced since August. Findings marked **[verified]**
were demonstrated by running code. Windows and CUDA paths were not exercised.

---

## Purpose

This closes the review cycle that began on 6 July. That review found a
high-severity defect family — **stale wakeup events** — and named it the gate for
an official 3.0. The 3 August verification confirmed that family resolved and
found the fix campaign had surfaced several further defects, notably a
**reproducibility failure** that made seeded runs non-deterministic across
machines. It left exactly one open item: **memory leaked when a trial abandons
itself** via `cmb_logger_error`, which in a long campaign leaks without bound.

That item is now closed, and this review verifies it along with everything before
it. **I find no outstanding correctness, memory-safety, or reproducibility barrier
to declaring 3.0.0.**

---

## The August blocker: trial-abandonment leaks — RESOLVED [verified]

The 3 August review measured 14 abandoned trials leaking 421 MB, and recorded the
agreed direction: a per-trial registry so the library reclaims its own objects,
with model code responsible only for its own allocations. That design is now
built, and the separation of duties is drawn precisely:

- **`cmi_memregistry`** — a thread-local registry of live `cmb_` objects, backed by a new intrusive doubly-linked list (`cmi_dlist.h`). On abandonment the worker's recovery branch runs the registered destructors in LIFO order.
- **`cimba_trial_cleanup_set()`** — a public hook the model uses to release its *own* allocations, invoked on the abandonment path before the registry teardown.

The resulting contract is a single sentence: **the registry covers `cmb_` objects;
everything else — direct `malloc`, and any `cmi_` internals a model chooses to use
— belongs to the model and is released through the cleanup hook.** `tut_4_3` is
the worked example, tearing down its own context and its own `cmi_hashheap`
alongside the library's objects.

Several design decisions in the registry are worth recording, because each closes
a class of error rather than an instance:

- **Registration happens at the ownership boundary only** — types with a public `create`/`destroy`. Embedded components are torn down by their owners, so no object is registered twice.
- **Derived classes retarget the base's registry entry** rather than adding one. One entry, one destructor call, at any inheritance depth.
- **The invariant is `cookie set ⟺ registered`**, enforced at both `initialize` and `terminate`, which makes re-initialization and double-termination harmless by construction rather than by per-function care.

Verified with a purpose-built soak: 900 trials, a resource, a dataset and four
processes per trial, abandoning from inside a coroutine, with a user cleanup hook
registered. **300 trials abandoned, all recovered, zero leaks and zero
AddressSanitizer findings under `detect_leaks=1`.** The library's own
`test_cimba` and the `tut_4_3` tutorial are likewise leak-clean.

**LeakSanitizer is now enabled in CI** (`detect_leaks=1` in the `linux-asan` job),
which is the durable form of this result: the condition is now enforced on every
push rather than checked by hand.

---

## Status of the 6 July findings [verified]

All items from that review remain resolved. Spot-checked against the current tree:

- **R1–R3, the stale-wakeup family.** Re-verified with a fresh tied-timestamp probe written for this review, combining all three cases: a holder releasing at the exact instant a waiter's timeout fires, with a second waiter queued behind. The waiter's subsequent holds run their full duration (no spurious resume), and the second waiter acquires and completes (not stranded). Clean under ASan+UBSan.
- **R4** — error exit status is `EXIT_FAILURE`.
- **R5** — `nm -D` shows exactly one defined global outside the `cmb_`/`cmi_`/`cimba_` namespaces (`cmg_atexit_armed`, deliberately commented).
- **R6, R7** and polish items (b)–(i) — confirmed in place.

---

## Defects found and fixed since 3 August

The tutorial suite was audited under sanitizers for the first time during this
period. That audit found **library** bugs, not only tutorial bugs, and two of them
are worth recording because of how they presented.

### Stale ASan poison on recycled coroutine stacks — RESOLVED [verified]

The coroutine stack pool recycles stacks without freeing them, so
AddressSanitizer's shadow retained poison from the previous coroutine's frames.
The next process's frame landed on stale poison and produced an unclassifiable
"Unknown-crash". Dating from the June stack-pool work, invisible until a
high-churn tutorial was run under ASan. Fixed by unpoisoning the usable region
when a stack enters the pool, with a debug assertion on the reuse path that
proves the pool hands out clean memory.

### Stale ASan shadow on the thread stack after abandonment — RESOLVED [verified]

`__builtin_longjmp` is not intercepted by AddressSanitizer, so the frames
abandoned by a trial bailout were never unpoisoned — unlike libc's `longjmp`,
which ASan wraps for exactly this purpose. The next trial reused those addresses
and tripped over the residue. This presented as environment flakiness ("only in
CLion", "only sometimes") because it required a later frame to land precisely on
a stale redzone. Fixed by unpoisoning the abandoned span in the coroutine
recovery path, using the landing frame as the boundary.

Both bugs share a shape worth naming: **memory reused without telling the
sanitizer**. Neither was reachable except through the abandonment path, and
neither could have been found by the existing test suite.

### Other items

- Two thread-local scratch buffers (`cmb_event` and `cmi_hashheap` pattern-cancel) are now released at thread exit, removing a per-thread leak introduced with the buffers themselves.
- `cmb_logger_error` no longer tears down the event queue before transferring control. The rule is now uniform: **the logger reports and jumps; it never cleans up**, because anything it destroys is something the registry can no longer reach.
- `tut_5_1`: a VTKHDF dataspace declared at full grid dimensions while the buffer held the decimated map (a genuine heap-buffer-overflow, caught by ASan), and a `uint32_t` index into a 3.6-billion-element grid that was within 16% of silently wrapping.
- The CUDA tutorials were being compiled for `sm_75` because an `add_project_arguments` call did not reach targets declared `native : true`. A latent build-configuration defect that produced a working binary targeting the wrong hardware.

---

## Regression and sanitizer status [verified]

- **Builds:** release, debug, debug+ASan/UBSan, and ThreadSanitizer all compile **warning-free** at `warning_level=3`. Two `-Wunused-result` warnings remain in `tut_4_2`/`tut_4_3` (ignored `system()` return from gnuplot invocations) — tutorial code, cosmetic.
- **Test suite:** **24/24 pass** in release.
- **ASan+UBSan+LeakSan:** all fourteen unit-test binaries exercised pass clean with `detect_leaks=1`, zero leak records.
- **ThreadSanitizer:** `process`, `resourcepool`, `event`, `condition` pass clean.
- **Abandonment soak:** 900 trials, 300 abandoned from inside coroutines, leak-clean under ASan.
- **Tied-timestamp probe:** the R1–R3 family, no spurious wakeups, no stranded waiters.
- **Install tree:** every public header compiles standalone out-of-tree; **all 205 declared public functions resolve** against the installed library; `pkg-config` generates; the install-guide program builds and runs.
- **Determinism:** three consecutive fixed-seed runs of `test_resourcepool` produce byte-identical output.
- **Stress:** 200 randomly-seeded `test_resourcepool` runs, zero failures.

---

## Observations, none blocking

- **The `cmi_` namespace is unsupported, not inaccessible.** Models may use it — the tutorials do — but it carries no registry coverage and no stability guarantee across minor releases. Worth stating explicitly in the documentation if it is not already, since the tutorials demonstrate the practice.
- **Unzeroed allocations are the sharpest edge of the strict lifecycle.** A derived struct allocated with a bare `malloc` and passed to a parent `initialize` hands random bytes to code that inspects them. This produced a genuine Heisenbug during the sweep — invisible under both ASan and `MALLOC_PERTURB_`, because both change what recycled memory contains or when it is handed back. The migration note should call this out as loudly as the missing-`terminate` case; it fails intermittently and points nowhere useful.
- **The CUDA tutorials are not covered by CI** and were not exercised here. They require a matching toolkit and driver, and the architecture flag is a hardcoded value that nothing verifies. A post-build `cuobjdump --list-elf` assertion would close that gap the way the install link-check closed the phantom-function gap.
- **Consider adding a high-churn scenario to the sanitized CI job.** Both ASan bugs above were only reachable through rapid process creation and destruction combined with trial abandonment — a pattern the current tests do not produce. `tut_4_3`'s process-per-arrival shape, at reduced trial count, would cover it.

---

## Assessment

Every defect from the 6 July review is resolved, and the single item left open on
3 August is closed and enforced in CI. The trial-abandonment machinery is not
merely present but verified under load: 300 abandonments in one run, leak-clean,
with the library reclaiming its own objects and a documented, demonstrated
boundary for the model's.

The quality of the work in this period has been high, and consistently of the kind
that closes classes rather than instances. The registry's ownership-boundary rule,
the cookie invariant, the derived-class retarget, the install link-check, and the
debug assertions that prove the ASan fixes are doing real work are all examples of
choosing the structural fix over the local one. Two of the bugs found in this
period had been latent for months and presented as environment flakiness; they
were run down rather than worked around, which is the harder and better choice.

**Assessment:** I see no outstanding correctness, memory-safety, or reproducibility
barrier to declaring this snapshot 3.0.0. The engine, the resource layer, the
numerics, the error-recovery machinery, the packaging, and the CI matrix are in
good order for high-stakes work on the exercised platform.

As always, continued caution is appropriate for the Windows port and the CUDA
tutorials, neither of which was exercised in this review, and for code paths beyond
the test suite.

---

*This is a point-in-time review of the specified snapshot by Claude (Anthropic).
It reflects build-and-test evidence on Linux x86-64 plus source inspection. It is
not a certification or guarantee of fitness for any purpose.*
