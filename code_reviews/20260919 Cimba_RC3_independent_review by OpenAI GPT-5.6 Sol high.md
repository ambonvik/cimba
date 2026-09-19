# Independent Engineering Review of Cimba 3.0 RC3

**Date:** 2026-09-19  
**Reviewer:** OpenAI GPT-5.6 Sol  
**Repository reviewed:** `ambonvik/cimba`, current snapshot supplied by the maintainer  
**Public repository:** https://github.com/ambonvik/cimba  
**Documentation:** https://cimba.readthedocs.io/en/latest/index.html  
**Related blog:** https://ambonvik.github.io

## Executive summary

Cimba 3.0 RC3 is an unusually disciplined C implementation of a discrete-event simulation framework. The codebase shows deliberate architecture rather than incremental accretion: public `cmb_*` interfaces are separated from internal `cmi_*` machinery; the event scheduler has explicit deterministic ordering; process-oriented simulation is built on a contained coroutine layer; object lifetimes are actively tracked; and the project has a substantial verification culture around assertions, sanitizers, statistical testing, installation testing, and repeated adversarial review.

I found **no evidence of a defect in the core event-ordering algorithm, coroutine transfer design, or recently corrected random-variate algorithms that should by itself block release**. The overall state is materially stronger than a typical release candidate for a performance-oriented C library.

I did, however, identify several concrete hardening items that matter for a library intended for high-stakes analytical work:

1. **The Windows CI matrix says it tests debug and release, but both matrix legs currently build the default release configuration.** This leaves the Windows debug/assert-heavy configuration untested despite the platform-sensitive coroutine implementation.
2. **`cmb_resourcepool_release()` leaves several true API preconditions as debug-only assertions.** In a release build, a process that attempts to release more than it personally holds can silently underflow its per-process holding and corrupt pool accounting instead of failing cleanly.
3. **Several process-only resource acquisition/preemption paths do not consistently enforce “must be called from a Cimba process” at release-assert level.** Misuse from an event/dispatcher context can therefore become a null dereference rather than a controlled contract failure.
4. **Probability-vector validation for discrete non-uniform and alias sampling checks only the total sum, not individual probability validity, and `cmb_random_alias_create()` does not explicitly check `pa != NULL`.** Invalid probability vectors can be accepted and mapped to unintended distributions.
5. **The supported-toolchain statement includes Clang, but current CI exercises GCC only.** This is a verification gap rather than a code defect.

None of these findings changes my overall view that the architecture is sound. Findings 1–3 are small fixes with a high reliability payoff and should, in my view, be closed before the final 3.0.0 tag if the release is to carry the stated “solid infrastructure for important work” standard.

## Scope and methodology

This review was intentionally fresh and self-contained. I inspected the supplied repository snapshot, including:

- Public API headers under `include/`.
- Core implementation under `src/`.
- Linux and Windows x86-64 coroutine/platform code.
- Event scheduling and the hash-heap implementation.
- Process lifecycle, interruption, cancellation, resource, resource-pool and guard logic.
- Random-number generation and discrete probability interfaces.
- Data/statistics collectors at a design level.
- Meson build configuration.
- GitHub Actions CI configuration.
- Unit, stochastic, and installation-test setup.
- Tutorials, README, background documentation and changelog.
- Earlier reviews and their verification follow-ups under `code_reviews/`.

The earlier reviews were used as historical context, particularly to avoid re-reporting defects already fixed and independently verified. The present assessment does not rely on those reports for its conclusions; current source was inspected directly.

### Execution limitation for this review

The supplied snapshot was available locally, but this review environment did not contain Meson or NASM, and network-disabled package installation prevented adding them. I therefore could not independently rerun the complete Cimba build/test/sanitizer matrix in this session. I did inspect the shipped CI definitions, test programs, prior executable verification reports, and current implementation in detail. Findings in this report are consequently marked or described as **source/CI inspection findings**, not as locally reproduced runtime failures.

That limitation matters less than it otherwise would because the repository contains unusually extensive prior build-and-run verification, including sanitizer and statistical campaigns. It still means this review should not be represented as an additional independent execution of those campaigns.

## Overall assessment

### Design

**Assessment: Strong**

The dominant architectural quality is consistency. Cimba has a coherent simulation worldview and the implementation follows it throughout:

- Trials are the unit of pthread-level parallelism.
- Processes are stackful coroutines within a trial thread.
- The event queue is thread-local and deterministic.
- Process-interaction primitives are layered on reusable holdable/resource-guard mechanisms.
- Public and internal namespaces are visually and conceptually distinct.
- Error recovery has an explicit object-registry cleanup model.
- Performance-sensitive primitives such as hash-heaps and memory pools are specialized and contained.

The separation between *parallel trials* and *concurrent simulated processes* is particularly good. It avoids conflating wall-clock concurrency with simulated concurrency, which is a common source of conceptual and implementation errors in simulation frameworks.

The decision to keep the project under a consistent single design vision is visible in the code: naming, lifecycle patterns, assertion use, object composition, and queue semantics are unusually uniform across modules.

### Coding style

**Assessment: Strong**

The code is readable for systems-level C. Functions tend to have one clear job, comments usually explain *why* rather than merely restating statements, and internal invariants are extensively encoded as assertions.

Positive characteristics include:

- Consistent naming (`cmb_*`, `cmi_*`, `cmg_*`).
- Explicit lifecycle functions.
- Good use of `const` in interfaces and local reasoning.
- Centralized memory wrappers.
- Centralized logger/assert behavior.
- Careful integer-width selection.
- Avoidance of clever macro metaprogramming beyond what the project genuinely needs.
- Comments around ABI-sensitive coroutine code that are substantially better than average assembly-adjacent documentation.

The main style-related risk is not readability but **contract-level consistency**: a few public API preconditions that are release-critical are still guarded only by debug assertions. In a codebase that explicitly uses “release assertions check user preconditions” as a design principle, those exceptions stand out.

### Correctness and reliability

**Assessment: Strong core, a few RC hardening items remain**

The strongest reliability feature is that correctness is treated as an engineering property rather than an assumption. The repository combines:

- Design-by-contract assertions.
- Unit tests by module.
- Fixed-seed regression tests.
- Free-running statistical quality tests.
- ASan/LeakSan, UBSan and TSan CI jobs.
- Installation/header-closure verification.
- Debug and release Linux testing.
- Explicit cleanup after abandoned trials.
- Repeated adversarial review and follow-up verification.

The event scheduler is particularly reassuring. Events are ordered by simulation time, then priority, then stable key/FIFO order. The ordering rule is explicit in code rather than being an accidental property of heap layout or pointer values.

The error-recovery design is also stronger than usual for C simulation software. Abandoned trials are returned to the worker's main stack, registered Cimba objects are torn down in LIFO order, and the event queue is reset before the next trial. Earlier reviews exposed real flaws in this area; the current code reflects substantial hardening.

The remaining findings below are mostly about making the *failure behavior* as reliable as the nominal behavior.

### Maintainability

**Assessment: Good, with platform-sensitive hotspots clearly identifiable**

The code is maintainable for its domain, but not simple. The areas requiring specialist care are obvious:

- Hand-written x86-64 context switching.
- Windows TEB manipulation.
- `setjmp`/`longjmp` recovery across coroutine use.
- TLS lifecycle and per-thread caches.
- Intrusive containers and memory registries.
- Custom random-variate implementations.

That is acceptable complexity because it is concentrated rather than smeared across the codebase.

The best long-term maintainability investment is therefore not a rewrite but continued **executable specification around these hotspots**: regression tests, ABI/platform CI, sanitizer jobs, and short architecture notes documenting invariants.

### Performance

**Assessment: Strong and deliberately engineered**

The performance design is credible and internally consistent:

- Trial-level parallelism uses a simple atomic work index with naturally good load balancing.
- Event queues are per-thread, avoiding synchronization in the simulation kernel.
- The hash-heap combines priority access with handle lookup/cancellation.
- Queue storage is contiguous and cache-conscious.
- Coroutine stacks are recycled per thread.
- TLS is intentionally optimized on Linux.
- RNG hot paths use specialized algorithms and generated ziggurat tables.
- Logging and assertion layers can be compiled down in optimized builds.
- A PGO workflow is present and verifies that profile data were actually consumed.

The code generally avoids premature abstraction in hot paths. The performance optimizations are understandable and localized enough that they do not appear to have made the architecture brittle.

## Detailed findings

### F1 — Windows CI debug/release matrix does not actually select the matrix build type

**Severity: Medium**  
**Category: Reliability / CI / platform coverage**  
**Evidence: `.github/workflows/ci.yml:160-187`**

The workflow declares:

```yaml
matrix:
  buildtype: [debug, release]
```

but configures both jobs with:

```sh
meson setup build
```

and never passes `${{ matrix.buildtype }}`. The project default is `buildtype=release`, so both Windows matrix jobs build the same release configuration.

This matters because Cimba's Windows path contains some of the most platform-sensitive code in the project: hand-written context switching, TEB stack fields, stack-protector/CET decisions, and recovery behavior. The debug build also enables the dense invariant checking that is one of Cimba's principal verification mechanisms.

**Recommended fix**

Use the matrix value explicitly, for example:

```yaml
meson setup build --buildtype=${{ matrix.buildtype }} -Denable_docs=false
```

A further improvement would be to make the build directory include the matrix value (`build-${{ matrix.buildtype }}`), although each matrix leg already runs in a fresh runner.

**Recommended regression guard**

Print `meson configure build` or another explicit buildtype check in CI so a future workflow edit cannot silently collapse the matrix again.

### F2 — `cmb_resourcepool_release()` can silently corrupt per-process accounting in a release build after an invalid release request

**Severity: Medium**  
**Category: Correctness / API contract enforcement**  
**Evidence: `src/cmb_resourcepool.c:628-668`**

The public contract states that a process must not release more than it currently holds. The implementation checks only:

```c
cmb_assert_release(rpp->in_use >= rel_amount);
cmb_assert_release(rel_amount <= rpp->capacity);
```

The actual per-process check is debug-only:

```c
cmb_assert_debug(pi->amount >= rel_amount);
```

Consider a pool where process A holds 1 unit and process B holds 9 units. If A erroneously calls `cmb_resourcepool_release(pool, 2)`, then the pool-level `in_use >= rel_amount` precondition passes. In a release build, the per-holder assertion is absent and:

```c
pi->amount -= rel_amount;
```

underflows the unsigned holder count. The global `in_use` value is also decremented, leaving internal state inconsistent rather than terminating at the violated contract.

For a high-reliability library, this is exactly the class of user-model error that should be trapped deterministically at the API boundary.

**Recommended fix**

Promote the true preconditions to release assertions before mutation:

```c
cmb_assert_release(rbp->cookie == CMI_INITIALIZED);
cmb_assert_release(pp != NULL);
...
cmb_assert_release(pi != NULL);
cmb_assert_release(pi->holder == pp);
cmb_assert_release(pi->amount >= rel_amount);
```

The existing lower-level `cmi_hashheap_item()` already asserts that the holder key exists, but an explicit public-layer contract is clearer and gives a better diagnostic.

**Recommended regression test**

Add a subprocess/expected-assert test in a release-style assertion build where two processes hold different shares and one attempts to release more than its own holding while the total pool still contains enough. The expected behavior should be a controlled release-assert failure, never arithmetic underflow.

### F3 — Process-only resource paths do not consistently enforce process context before dereference

**Severity: Medium-Low**  
**Category: Reliability / API contract enforcement**  
**Evidence: `src/cmb_resource.c:236-276`, `318-336`; analogous resource-pool paths**

`cmb_resource_release()` correctly enforces:

```c
cmb_assert_release(pp != NULL);
```

but `cmb_resource_acquire()` does not. It obtains:

```c
struct cmb_process *pp = cmb_process_current();
```

and, if the resource is immediately available, passes that pointer to `resource_grab()`, which dereferences it. Calling `cmb_resource_acquire()` from an event handler/dispatcher context can therefore become a null dereference instead of a Cimba contract failure.

`cmb_resource_preempt()` is more direct: it obtains `pp` and immediately reads `pp->priority` without a release assertion.

The resource-pool acquisition/preemption path has the same semantic requirement. `holder_key()` contains only debug assertions before using the current process.

These are invalid call contexts, but the library's stated reliability model relies on release assertions to catch invalid public API use. A deterministic diagnostic is much preferable to a segmentation fault whose exact behavior may differ by build and platform.

**Recommended fix**

Add `cmb_assert_release(cmb_process_current() != NULL)` (or equivalent using the already fetched pointer) at the public entry points that require process context.

**Recommended regression test**

Invoke these APIs from a scheduled event callback with an available resource/pool and verify that release builds fail through the intended Cimba assertion mechanism rather than through a native access violation.

### F4 — Probability-vector validation accepts invalid component probabilities

**Severity: Medium-Low**  
**Category: Statistical correctness / input validation**  
**Evidence: `src/cmb_random.c:1191-1210`, `1252-1263`**

`sums_to_one()` validates only the aggregate sum:

```c
for (...) {
    sum += p[ui];
}
return fabs(sum - 1.0) <= 1.0e-3;
```

It does not verify that every probability is finite and lies in `[0, 1]`.

A vector such as `{-0.5, 1.5}` therefore passes the sum test even though it is not a probability mass function. `cmb_random_discrete_nonuniform()` then produces a distribution unrelated to the stated input semantics, and the alias builder clamps/scales intermediate values in ways that can conceal the invalid input instead of rejecting it.

In addition, `cmb_random_alias_create()` calls `sums_to_one(n, pa)` without its own explicit `pa != NULL` assertion, unlike `cmb_random_discrete_nonuniform()`.

This is primarily a contract-hardening issue: valid callers are unaffected. For research software, however, rejecting malformed probability inputs early is important because silent statistical misuse can be harder to detect than a crash.

**Recommended fix**

Replace or extend `sums_to_one()` with a validator that checks:

- `pa != NULL` at the public entry point.
- Every `p[i]` is finite.
- Every `p[i] >= 0.0` (and optionally `<= 1.0`).
- The sum is acceptably close to one.

For large vectors, consider compensated summation or a tolerance scaled to vector length/machine epsilon rather than a fixed `1e-3`, unless the intentionally permissive tolerance is part of the API contract.

**Recommended tests**

Add release-assert tests for negative entries, NaN, infinity, all-zero vectors, compensating negative/greater-than-one entries, and null `pa` for alias creation.

### F5 — Clang is a supported compiler but is not currently represented in CI

**Severity: Low**  
**Category: Portability / maintainability**  
**Evidence: `src/cmi_config.h:24-31, 58-74`; `.github/workflows/ci.yml`**

The configuration header explicitly supports GCC and Clang. Current Linux CI uses the runner's default GCC toolchain, while Windows installs MinGW GCC. There is no Clang build job.

This is not evidence that Clang is broken. It is simply an unsupported verification claim: a supported compiler should ideally have at least one continuously exercised build.

This is more important for Cimba than for an ordinary portable C library because the code uses compiler attributes, GNU builtins, LTO flags, TLS-model attributes, sanitizer integration and ABI-sensitive coroutine machinery.

**Recommended fix**

Add at least one Linux Clang job, preferably debug plus release. A single Clang ASan/UBSan job would add additional compiler diversity at modest CI cost.

### F6 — “unique, truly random” hardware-seed wording is stronger than the implementation can guarantee

**Severity: Low (documentation)**  
**Category: Reproducibility / documentation precision**  
**Evidence: `include/cimba.h:120-130`; hardware-seed implementation**

The public API documentation says `cmb_random_hwseed()` “will provide each trial with a unique, truly random seed.” That is too strong as a formal guarantee.

Even a high-quality hardware entropy source cannot mathematically guarantee uniqueness across arbitrary calls, and the implementation intentionally contains fallback construction using thread id, clocks and cycle count on machines where hardware entropy instructions are unavailable. That fallback can be perfectly reasonable for simulation seeding without being “truly random.”

**Recommended wording**

Something like:

> “returns a high-entropy seed, using hardware entropy where available; independently generated seeds are intended to be practically distinct but uniqueness is not guaranteed.”

The existing recommended pattern for reproducible experiments—one master seed plus deterministic per-trial derivation—is the stronger and more important guarantee and should remain emphasized.

## Strengths worth preserving

### 1. Deterministic event semantics

The event ordering rule is explicit: time, priority, then stable key. That is exactly what a simulation kernel should do. Determinism should never depend on pointer identity, allocator behavior, incidental heap structure or thread scheduling.

### 2. Mature handling of cancellation and stale wakeups

The current process/event interaction code reflects lessons learned from earlier review cycles. Awaitables carry enough identity to distinguish stale wakeups, cancellation targets specific internal events, and reactivation paths recheck state instead of assuming that a wakeup implies the resource is still available.

These are subtle areas where many simulation libraries accumulate race-like logical bugs even without OS-level data races.

### 3. Explicit lifecycle and abandoned-trial recovery

The create/initialize/terminate/destroy discipline is demanding for users, but it buys something important: Cimba can reason about ownership and recover a worker thread after an abandoned trial instead of leaking process stacks and simulation objects indefinitely.

The intrusive LIFO memory registry is a pragmatic C solution. It is simple enough to audit and efficient enough to leave enabled.

### 4. Verification culture

The repository's strongest quality signal is not any single test but the layering of checks:

- unit tests,
- debug assertions,
- release assertions,
- stochastic regression tests,
- free-running quality tests,
- ASan/LeakSan,
- UBSan,
- TSan,
- downstream install/header verification,
- PGO verification,
- and repeated independent review followed by explicit verification passes.

The earlier review history is especially valuable because it demonstrates that the test suite has evolved in response to real defects instead of merely accumulating happy-path cases.

### 5. Statistical testing has become substantially stronger

The recent addition of exact/distribution-sensitive tests is an important improvement. Moment checks alone are insufficient for random-variate implementations; previous review history demonstrated that directly. The current approach of combining fixed-seed regression with hardware-seeded high-threshold quality tests is well chosen.

### 6. Performance optimizations are localized and inspectable

Cimba uses low-level techniques—assembly context switching, stack recycling, custom pools, TLS tuning, contiguous hash-heaps, ziggurat sampling and PGO—but most of them live behind narrow interfaces. That is preferable to letting performance concerns infect every layer of model-facing code.

### 7. Documentation explains architecture, not just APIs

The background documentation is valuable because it explains why the code looks the way it does: stackful coroutines, event scheduling, process semantics, object lifecycle, error recovery, multithreaded trials and the interaction with CET. That kind of documentation materially lowers future maintenance risk.

## Test and CI assessment

The current CI design is strong in breadth:

- Linux debug and release.
- Linux installation verification.
- Linux ASan/LeakSan.
- Linux UBSan.
- Linux TSan.
- Linux PGO generate/use verification.
- Windows MinGW jobs.

The main correction needed is F1: the Windows matrix presently duplicates release rather than testing debug and release.

After that is fixed, the most useful incremental addition would be compiler diversity (F5) rather than more copies of the same GCC configuration.

For a 3.0 release intended for serious analytical use, I would also consider making the following release checklist explicit, even if it remains a maintainer document rather than CI automation:

- Linux GCC debug/release green.
- Linux Clang debug/release green.
- ASan/LeakSan green.
- UBSan green.
- TSan green or any known tool-runtime limitation explicitly documented.
- Windows MinGW debug/release green.
- Stochastic fixed-seed regression green.
- Free-running statistical quality suite green.
- Installed-header/downstream build green.
- No unverified changes to coroutine assembly after the final full matrix run.

## Maintainability recommendations

### Keep “release assertions = public preconditions” as a strict rule

The code is already close to this discipline. Findings F2 and F3 are valuable precisely because they are exceptions to an otherwise strong pattern. I recommend treating it as a formal code-review rule:

> Any public API misuse that could otherwise cause memory corruption, arithmetic wraparound, invalid statistical results or null dereference must be guarded by a release-level assertion before state mutation or dereference.

Debug assertions should remain for expensive invariants and internal consistency checks.

### Document subsystem invariants in short developer notes

The user documentation is strong. Future maintainers would benefit from a few concise developer-facing invariant documents for:

- Event/hash-heap key semantics and ordering.
- Coroutine ownership and thread affinity.
- Trial-abandon recovery sequence.
- Memory-registry assumptions.
- Resource-guard wakeup identity rules.
- RNG reproducibility guarantees and non-guarantees.

These need not become a large contributor handbook. A few pages would be enough to preserve the reasoning that currently lives partly in code comments and review history.

### Preserve the review/verification pairing

The strongest pattern in `code_reviews/` is not “AI review”; it is **review followed by explicit verification**. That distinction matters. Static review is good at finding suspicious paths; small executable reproducers are what establish that a defect is real and that its fix is correct.

For future changes in the coroutine, resource, event, and RNG subsystems, this paired approach should continue.

## Release assessment

### Critical findings

None found.

### High findings

None found.

### Medium findings

- **F1:** Windows CI matrix does not actually build both debug and release.
- **F2:** `cmb_resourcepool_release()` can underflow a caller's holding in release builds after an invalid but plausible release request because the key precondition is debug-only.

### Medium-low findings

- **F3:** Process-only resource entry points do not consistently enforce process context before dereference.
- **F4:** Probability-array validation accepts malformed component probabilities and alias creation lacks an explicit null check.

### Low findings

- **F5:** Clang is supported but absent from CI.
- **F6:** Hardware-seed documentation overstates uniqueness/randomness guarantees.

## Recommended pre-3.0 actions

I recommend closing **F1, F2 and F3 before the final 3.0.0 tag**. They are small, local changes and directly strengthen the claim that release builds fail cleanly on model-contract violations rather than corrupting state or crashing incidentally.

I would also fix **F4** before 3.0 if practical because malformed probability vectors are exactly the sort of input error that can silently contaminate research results. F5 and F6 are suitable for the same release but are not release blockers.

After those fixes and a green full CI run, I would not identify a remaining architectural reason to hold the 3.0 release on the basis of this review.

## Final conclusion

Cimba 3.0 RC3 is a serious piece of simulation infrastructure. Its most impressive quality is not raw performance but the combination of performance with explicit contracts, deterministic behavior, unusually deep verification, and willingness to revise low-level designs when adversarial testing finds weaknesses.

The review history shows that the project has already survived findings in precisely the areas that tend to fail in discrete-event simulation systems: event identity, stale wakeups, resource ordering, abandoned-trial recovery, numerical edge cases, reproducibility and random-variate quality. The present source is visibly stronger because of that process.

The remaining findings in this review are narrow rather than structural. Fixing the Windows build-matrix error and promoting the identified resource/resource-pool conditions to release-level contracts would materially improve the final RC without changing the architecture. Tightening probability-vector validation would further align the implementation with the standard expected of research infrastructure.

**Overall assessment:** Cimba 3.0 RC3 is close to release-quality infrastructure for serious simulation work. I recommend a short final hardening pass for F1–F4, followed by the complete CI/test matrix, and then proceeding to 3.0.0 if that verification is clean.
