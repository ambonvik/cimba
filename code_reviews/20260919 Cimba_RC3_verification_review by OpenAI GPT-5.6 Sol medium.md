# Verification Review of Cimba 3.0 RC3

**Date:** 2026-09-19  
**Reviewer:** OpenAI GPT-5.6 Sol  
**Verification target:** Findings F1–F6 from *Independent Engineering Review of Cimba 3.0 RC3* dated 2026-09-19  
**Snapshot reviewed:** freshly supplied `cimba-main` snapshot, Cimba 3.0.0-RC3  

## Executive summary

This verification pass rechecked the six findings from the independent RC3 review against the newly supplied repository snapshot. All six original findings are now **closed**.

| Finding | Original topic | Verification status |
|---|---|---|
| F1 | Windows debug/release CI matrix | **CLOSED** |
| F2 | Resource-pool over-release accounting corruption | **CLOSED** |
| F3 | Missing process-context enforcement on resource APIs | **CLOSED** |
| F4 | Probability-vector validation | **CLOSED** |
| F5 | Clang absent from CI | **CLOSED** |
| F6 | Overstated hardware-seed wording | **CLOSED** |

The minor documentation issue discovered during the previous verification pass, which referred to a non-existent `cmb_random_splitmix64()` public API, is also **closed**: the documentation now correctly names `cmb_random_fmix64()`.

The supplied CI result for commit `b5af13d` is green across the expanded matrix, including GCC debug/release, Clang debug/release, Linux install verification, ASan, UBSan, TSan, PGO, and Windows MinGW debug/release.

**Verification conclusion:** the findings from the independent 2026-09-19 review have been satisfactorily addressed. I found no remaining item from that review that should hold the 3.0.0 release.

---

## Scope and method

This is a focused verification review, not a third full independent audit. I compared the current implementation and CI configuration directly against each finding and recommended remediation in the original report, and also rechecked the follow-up GCC/Clang portability fixes that arose while closing F5.

The review covered, in particular:

- `.github/workflows/ci.yml`
- `src/cmb_resource.c`
- `src/cmb_resourcepool.c`
- `src/cmb_random.c`
- `include/cmb_random.h`
- `include/cimba.h`
- `test/tools/test_stochastic.py`
- `test/test_event.c`
- relevant documentation wording

### Execution evidence

I did not independently rerun the complete CI matrix in this review environment. The source snapshot was inspected directly, and the supplied GitHub Actions result shows the current workflow completing successfully for all configured jobs. This distinction is worth preserving: the code changes are verified by source inspection here; the runtime/build evidence comes from the supplied CI run.

---

## Original finding verification

### F1 — Windows CI debug/release matrix did not actually select the matrix build type

**Original severity:** Medium  
**Status:** **CLOSED**

The Windows MinGW matrix still declares:

```yaml
matrix:
  buildtype: [debug, release]
```

and now explicitly passes that value to Meson:

```yaml
meson setup build --buildtype=${{ matrix.buildtype }} -Denable_docs=false
```

This appears in `.github/workflows/ci.yml` around lines 185–206 of the supplied snapshot.

The original defect — both Windows jobs silently using the project's default release build type — is no longer present.

The supplied CI result also shows both matrix entries completing successfully:

- `windows-mingw-build (debug)`
- `windows-mingw-build (release)`

**Verification conclusion:** fully addressed.

---

### F2 — `cmb_resourcepool_release()` could corrupt per-process accounting after an invalid over-release

**Original severity:** Medium  
**Status:** **CLOSED**

The current implementation validates both process context and per-process ownership before mutating state:

```c
struct cmb_process *pp = cmb_process_current();
cmb_assert_release(pp != NULL);
...
struct pool_item *pi = (struct pool_item *)cmi_hashheap_item(hhp, key);
cmb_assert_release(pi->holder == pp);
...
cmb_assert_release(pi->amount >= rel_amount);
cmb_assert_release(pi->amount <= rpp->in_use);
```

The important original failure mode was a caller releasing more than its own holding while the pool as a whole still held enough. In that case `pi->amount -= rel_amount` could underflow in a release build because the per-holder check was debug-only.

That check is now a release assertion before any decrement (`src/cmb_resourcepool.c`, approximately lines 639–650).

**Verification conclusion:** the identified state-corruption path is closed.

---

### F3 — Process-only resource paths did not consistently enforce process context

**Original severity:** Medium-Low  
**Status:** **CLOSED**

This was the only original finding left partially open in the preceding verification pass. The remaining `cmb_resource_preempt()` path is now fixed.

`cmb_resource_acquire()` contains:

```c
struct cmb_process *pp = cmb_process_current();
cmb_assert_release(pp != NULL);
```

before the process pointer can be used (`src/cmb_resource.c`, approximately lines 241–242).

`cmb_resource_preempt()` now likewise contains:

```c
struct cmb_process *pp = cmb_process_current();
cmb_assert_release(pp != NULL);
const int64_t myprio = pp->priority;
```

(`src/cmb_resource.c`, approximately lines 326–328).

The resource-pool acquisition/preemption common path also performs the equivalent release-level check:

```c
struct cmb_process *caller = cmb_process_current();
cmb_assert_release(caller != NULL);
```

before deriving a holder key or accessing process state.

Thus a process-only resource API called from dispatcher/event context now fails through Cimba's intended contract mechanism rather than incidentally dereferencing a null current-process pointer.

**Verification conclusion:** fully addressed.

---

### F4 — Probability-vector validation accepted invalid component probabilities

**Original severity:** Medium-Low  
**Status:** **CLOSED**

The former aggregate-only sum check has been replaced by `valid_probability_vector()`:

```c
static const double sum_tolerance = 1.0e-12;
static bool valid_probability_vector(const uint64_t n, const double pa[n])
{
    cmb_assert_debug(n > 0u);
    cmb_assert_debug(pa != NULL);

    double sum = 0.0;
    for (uint64_t ui = 0u; ui < n; ui++) {
        const double p = pa[ui];
        if (!isfinite(p) || p < 0.0 || p > 1.0) {
            return false;
        }
        sum += p;
    }

    return (fabs(sum - 1.0) <= sum_tolerance) ? true : false;
}
```

Both public entry points then enforce the pointer and vector validity at release level:

```c
cmb_assert_release(pa != NULL);
cmb_assert_release(valid_probability_vector(n, pa));
```

This rejects the problematic classes identified in the original report: negative entries, values above one, NaN, infinity, compensating malformed vectors such as `{-0.5, 1.5}`, and null `pa` for alias-table creation.

The sum tolerance has also been tightened from the earlier `1e-3` behavior to `1e-12`.

**Verification conclusion:** fully addressed.

---

### F5 — Clang was supported but absent from CI

**Original severity:** Low  
**Status:** **CLOSED**

Linux CI is now an explicit compiler/build matrix:

```yaml
matrix:
  compiler: [gcc, clang]
  build:
    - type: debug
      lto: false
    - type: release
      lto: true
```

with compiler selection through:

```yaml
env:
  CC: ${{ matrix.compiler }}
```

and Meson-managed LTO:

```yaml
meson setup build \
  --buildtype=${{ matrix.build.type }} \
  -Db_lto=${{ matrix.build.lto }} \
  -Denable_docs=false
```

The supplied CI run shows all four combinations green:

- GCC debug, no LTO
- GCC release, LTO
- Clang debug, no LTO
- Clang release, LTO

This exceeds the minimum recommendation in the original review and has already demonstrated practical value by exposing portability assumptions that the previous GCC-only matrix did not reveal.

**Verification conclusion:** fully addressed.

---

### F6 — Hardware-seed documentation overstated uniqueness/randomness guarantees

**Original severity:** Low (documentation)  
**Status:** **CLOSED**

The original wording promised a “unique, truly random” seed from `cmb_random_hwseed()`. The current public documentation is appropriately narrower:

```text
call `cmb_random_hwseed()` to get one based on hardware entropy.
It will provide each trial with a high-entropy seed using
the best available hardware entropy source for the platform.
```

The reproducible workflow then correctly recommends one master seed followed by deterministic per-trial derivation using:

```c
cmb_random_fmix64(master_seed, trial_counter)
```

The wording now distinguishes high-entropy seeding from deterministic reproducibility without claiming mathematical uniqueness or “true randomness” for arbitrary hardware-seed calls.

**Verification conclusion:** fully addressed.

---

## Previous incidental documentation item

### D1 — Documentation named a non-existent `cmb_random_splitmix64()` public API

**Status:** **CLOSED**

The previous verification pass found a documentation reference to `cmb_random_splitmix64()`, which is an internal initialization concept rather than a public Cimba API.

The current `include/cimba.h` correctly says:

```text
use `cmb_random_fmix64()` with that master seed and a running trial counter
```

and the accompanying example calls:

```c
experiment[trl_idx].seed = cmb_random_fmix64(master_seed, trl_idx);
```

The public documentation and actual API are now consistent.

---

## Follow-up portability fixes remain in place

The compiler-diversity work performed while closing F5 uncovered several additional issues. I rechecked that the resulting fixes remain present in the supplied snapshot.

### Meson owns LTO configuration

The GCC/Clang release jobs use Meson's `b_lto` option instead of manually injecting compile-only `-flto=auto` flags. This keeps compiler and linker configuration coordinated and avoids the Clang bitcode/link failure that initially appeared.

### Logger line numbers are normalized only for golden-output comparison

`test/tools/test_stochastic.py` now has a narrowly scoped logger-prefix regular expression and a separate `normalise_for_comparison()` pass. Real source line numbers remain in actual logs and stored reference files, while GCC/Clang differences in `__LINE__` for multi-line macro invocations do not create false stochastic-test failures.

This is the right separation of concerns: diagnostic output remains useful to humans while the regression harness ignores compiler-dependent metadata that is not simulation semantics.

### RNG strict-aliasing cleanup

The ziggurat hot path no longer reads a `uint64_t` object through an incompatible `int64_t *`.

For the normal generator the bit representation is transferred with `memcpy()`:

```c
const uint64_t bits = cmb_random_sfc64();
int64_t i_cand_x;
memcpy(&i_cand_x, &bits, sizeof i_cand_x);
```

For the non-negative exponential path the sign bit is masked before the well-defined signed conversion:

```c
const int64_t i_cand_x = (int64_t)(bits & INT64_MAX);
```

The source also contains an explicit two's-complement hardware sanity assertion.

### Shared-PRNG draw order is explicitly sequenced

`test_event` no longer passes two RNG-consuming expressions as separate arguments of one C function call. The random event time and priority are first evaluated into temporaries:

```c
const double t = cmb_time() + cmb_random_exponential(10.0);
const int64_t p = cmb_random_dice(1, 5);
```

and then passed to the scheduler. This removes the unspecified C argument-evaluation order that had allowed GCC and Clang to consume the shared PRNG stream differently.

The fact that GCC and Clang now agree on the stochastic reference output is useful confirmation that this ambiguity has been removed.

---

## CI verification status

The supplied GitHub Actions result for commit `b5af13d` reports **Success** and shows the following jobs green:

- Linux GCC debug
- Linux GCC release + LTO
- Linux Clang debug
- Linux Clang release + LTO
- Linux install verification
- Linux ASan
- Linux UBSan
- Linux TSan
- Linux PGO
- Windows MinGW debug
- Windows MinGW release

That is a strong RC verification matrix for a low-level C simulation library. In particular, the combination of two optimizing compilers, release/debug modes, sanitizers, PGO, installation testing, and a separate Windows ABI/runtime path covers substantially different failure modes rather than merely repeating the same configuration.

---

## Final assessment

All six findings from the independent 2026-09-19 RC3 review are now closed:

1. Windows debug/release CI selection is correct.
2. Resource-pool over-release is trapped before state mutation in release builds.
3. Process-only resource/resource-pool paths enforce process context before dereference.
4. Probability vectors are validated component-by-component and at release level.
5. GCC and Clang are both continuously exercised in debug and release configurations.
6. Hardware-seed documentation no longer promises guarantees stronger than the implementation can make.

The one minor documentation issue discovered during verification is also fixed, and the portability fixes exposed by the new Clang lane remain present.

**Final verification conclusion:** the remediation work satisfactorily closes the findings from the independent RC3 review. Given the inspected source changes and the supplied all-green CI run, I see no unresolved item from that review that should block the Cimba 3.0.0 release.

