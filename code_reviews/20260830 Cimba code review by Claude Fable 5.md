# Cimba Code Review — Release Candidate 2, Fresh Full Pass

**Subject:** Cimba — multithreaded discrete event simulation library in C
**Version reviewed:** 3.0.0-RC2, `main` branch snapshot (commit `3cea210`, 30 August 2026)
**Reviewer:** Claude (Anthropic), AI-assisted code review requested by the maintainer
**Date:** 30 August 2026
**Review environment:** Ubuntu 24.04 x86-64, GCC 13.3, Meson 1.12, NASM 2.16, single-vCPU container. Linux paths exercised at runtime; Windows/MSYS2, ThreadSanitizer and CUDA configurations not exercised here (CI covers the first two).

## Scope and method

This is the tenth review in the series and the first full review of RC2. Under `src/` and `include/`, RC2 is identical to the `35a5dcf` snapshot closed out by the 29 August verification review, except for the version macro in `cimba.h`; the RC2 diff otherwise touches only README, docs, CHANGELOG, benchmarks, and `tutorial/meson.build`. All previously reported defects therefore remain *fixed by source identity* and are not re-litigated.

The 29 August review ended with an explicit hand-off: the Poisson, binomial, ziggurat and histogram work is new code, verified during development but never given an independent adversarial pass, and such a pass "would not come back empty." That hand-off defined this review's core mandate: **treat `cmb_random` and `cmb_dataset` as new code**, verify the statistical layer against independently computed ground truth, and additionally review the RC2-specific material — the updated benchmarks, the published performance claims, and the front-page documentation.

The review was build-and-run based. The tree was built in release and debug+ASan/UBSan configurations and both test suites were run to completion. An independent chi-square goodness-of-fit battery was written against exact PMFs computed with `lgamma` (sharing no code or tables with the library) and run at 20 M samples per case across every dispatch path of the new integer-valued generators. Every defect reported below as **[verified]** was demonstrated by executing a reproduction program against the release build; **[by inspection]** findings follow from reading the source, with the mechanism checked arithmetically.

The hand-off's prediction was correct: the pass did not come back empty. But the headline result deserves equal billing — the new variate-generation algorithms themselves are statistically clean. The defects found are at the edges: state management, API contract, underflow corners, and display.

## Baseline verification

- Release build: compiles clean at `warning_level=3`; **24/24 tests pass**.
- Debug + ASan/UBSan: **24/24 tests pass**, including `test_cimba` to completion (462.9 s, consistent with the 458–467 s of previous cycles) and `stochastic` (12.6 s). No sanitizer findings.
- ThreadSanitizer, Windows/MSYS2, CUDA: not exercised in this container.

## Statistical verification of the new random-variate layer [verified]

Independent chi-square goodness-of-fit against exact `lgamma`-computed PMFs, 20 M samples per case, cells with expected count < 20 lumped into the tails. The cases were chosen to cover every dispatch path: Poisson chop-down and PTRD on both sides of the r = 17 switch; binomial chop-down, the p > 0.5 fold, BTRD at moderate np, and a wide-npq case (n = 10⁶, p = 0.03, σ ≈ 170) that forces traffic through BTRD's squeeze and Stirling final-test paths (steps 3.2–3.4) rather than the |k−m| ≤ 15 iterative path; geometric at moderate and high p; and the negative binomial Poisson–gamma mixture at m = 1, 5 and 40.

| Case | χ² | df | z |
|---|---|---|---|
| Poisson r=5 (chop-down) | 18.4 | 20 | −0.25 |
| Poisson r=16.9 (chop-down edge) | 22.2 | 38 | −1.81 |
| Poisson r=17.1 (PTRD edge) | 34.8 | 38 | −0.37 |
| Poisson r=100 (PTRD) | 78.0 | 91 | −0.97 |
| Poisson r=5000 (PTRD) | 560.6 | 588 | −0.80 |
| Binomial n=50 p=0.3 (chop-down) | 53.0 | 30 | +2.97 → see note |
| Binomial n=50 p=0.7 (fold + chop-down) | 32.1 | 30 | +0.28 |
| Binomial n=100 p=0.5 (BTRD) | 36.8 | 47 | −1.06 |
| Binomial n=1000 p=0.04 (BTRD) | 62.6 | 57 | +0.53 |
| Binomial n=10⁶ p=0.03 (BTRD, wide npq) | 1309.8 | 1343 | −0.64 |
| Binomial n=300 p=0.9 (fold + BTRD) | 52.5 | 48 | +0.46 |
| Geometric p=0.25 | 46.6 | 44 | +0.28 |
| Geometric p=0.9 | 6.4 | 6 | +0.12 |
| NegBin m=5 p=0.3 | 75.2 | 60 | +1.38 |
| NegBin m=1 p=0.4 | 16.7 | 26 | −1.28 |
| NegBin m=40 p=0.6 | 75.8 | 62 | +1.24 |

The single value above 2σ (chop-down binomial, z = +2.97) failed to replicate: six fresh seeds gave z between −1.00 and +1.08, and a 100 M-sample run gave z = +0.09. With 16 tests, one excursion near 3σ is within expectation. **Every dispatch path of the new integer-valued generators matches its exact distribution.**

The BTRD and PTRD transcriptions were also checked structurally against Hörmann's 1992/1993 papers — setup constants, decomposition, squeeze coefficients (ρ, t), and the Stirling-corrected final test all match, including the reuse of v in the v ≥ vr branch that the transformed-rejection design depends on. The `cmb_wtdsummary` weighted-moment update and merge formulas were independently re-derived from Pébay et al. (2016) and are correct in all m1–m4 terms, in both the single-observation and two-set forms.

## New verified defects

### R1 — `cmb_random_flip()` bit cache survives re-seeding: per-seed trial reproducibility broken (MEDIUM-HIGH) [verified]

`cmb_random_flip` (`src/cmb_random.c:587`) caches 64 bits of entropy in thread-local statics (`bits`, `bitpos`, lines 589–590) and hands them out one per call. Neither `cmb_random_initialize` nor `cmb_random_terminate` resets this cache.

Under the library's own documented model — `cimba.h:110–147` instructs the trial function to call `cmb_random_initialize(trl->seed)` at the start of each trial, and `cimba_run` executes trials on pooled worker threads — a trial that consumes a number of flips that is not a multiple of 64 leaves stale bits behind. The *next* trial on that thread consumes those stale bits (derived from the *previous* trial's seed) before touching its own stream. The same trial seed then produces different results depending on which trials ran before it on that worker and how many flips they drew — i.e., on scheduling.

*Repro (provided, `flip_repro.c`), single thread:*

```
seed 2222 on fresh thread state:      00110001
seed 2222 after prior trial's flips:  00110111
=> DIFFERENT (reproducibility broken)
```

This silently defeats the reproducibility guarantee that this project has treated as a first-class property — the `handle` field was added to `struct cmb_process` in this very cycle specifically to keep seeds reproducible across environments. The results remain statistically valid; they are just not reproducible per seed, which is arguably worse than an honest failure because nothing looks wrong until someone tries to reproduce a published run. `tut_2_1` and `test_resourcepool` both use `flip` in model logic, so the pattern is already in circulation.

I checked whether this is a family: it is not. The thread-local parameter caches in `std_gamma`, `geometric`, the chop-downs, BTRD and PTRD store only values deterministically derived from the arguments, so they are reproducibility-neutral. `flip` is the only site that caches *entropy* across `initialize`.

**Fix:** hoist the two statics to file scope and set `bitpos = 0` in both `cmb_random_initialize` and `cmb_random_terminate`. **Side effects:** none for programs that initialize once per thread before drawing (`bitpos` starts at 0 anyway); programs that re-seed mid-thread will see flip sequences change — that is the correction itself. Worth one sentence in the `flip` doc comment stating that the cache belongs to the seed epoch. No performance cost.

### R2 — `cmb_dataset_PACF` aborts on its documented `acf = NULL` convenience form (MEDIUM) [verified]

The public contract (`include/cmb_dataset.h:288–289`) reads: *"@param acf Array of ACF's if already calculated, size n + 1, otherwise NULL"*. The implementation contains the matching branch — `if (acf == NULL) { allocate; cmb_dataset_ACF(...); }` — but `cmb_assert_release(acf != NULL)` at `src/cmb_dataset.c:880` sits above it, making the branch dead code. The assert is compiled into the library, so the documented usage aborts in **both** debug and release builds; only under `NASSERT` does the function behave as documented.

*Repro (provided, `pacf_null.c`):* a 1000-point dataset, `cmb_dataset_PACF(d, 10, pacf, NULL)` → `Fatal: Assert "acf != NULL" failed ... cmb_dataset_PACF (880)`, exit 134, release build.

The sibling `cmb_dataset_correlogram_print` implements the identical NULL-means-compute contract correctly, which is presumably where the PACF branch was copied from; the stray assert (note: the *same* assert at line 811 in `cmb_dataset_ACF` is correct — there `acf` is the mandatory output array) then defeats it. The suite never calls PACF with NULL, so nothing caught it.

**Fix:** delete the assert at line 880. **Side effects:** none for existing non-NULL callers; NULL callers now take the allocate-and-compute path, whose ordering is already safe (the empty/short-data warning returns occur *before* the allocation, so no leak on the early paths, and `free_acf` releases it on the normal path).

### R3 — `cmb_random_std_beta` returns silent NaN for small shape parameters (MEDIUM) [verified]

For `shape < 1`, `cmb_random_std_gamma` applies the boost `mult = pow(f, 1.0/shape)` with `f = 1 − U ∈ (0, 1]` (`src/cmb_random.c`, gamma section). For small shapes this multiplier underflows to exactly 0.0 with probability `P = exp(−745·shape)` — past the bottom of the subnormal range. Measured: `std_gamma(0.005)` returns exactly 0.0 in 2.41 % of draws, agreeing with the predicted e^(−3.725) = 2.41 % to all printed digits.

A zero gamma variate is defensible on the documented `[0, ∞)` support. But `cmb_random_std_beta` (`include/cmb_random.h:481`) computes `x / (x + y)`, and when **both** gammas underflow this is 0/0 = NaN:

```
Beta(0.005, 0.005): 613 NaNs in 1000000 draws (0.06%)   [matches 0.0241² = 5.8e-4]
Beta(0.01, 0.01):     5 NaNs in 20000000 draws
```

Because `std_beta` is a `static inline` in the public header, the containing `cmb_assert_debug((r >= 0.0) && (r <= 1.0))` is governed by the *user's* compile flags: a default user build aborts mid-simulation; an `-DNDEBUG` build silently hands NaN to the model. The documented domain is `a > 0, b > 0` with no lower limit, and near-zero shapes are legitimate (near-Haldane priors, heavily bimodal proportions). `cmb_random_beta`, `cmb_random_PERT_mod` and `cmb_random_PERT` inherit the defect.

Notably, `cmb_random_F_dist` and `cmb_random_std_t_dist` already guard their equivalent chi-squared-underflow with retry loops — `std_beta` is the one member of the family without a guard.

**Fix (option a, recommended):** retry — `do { x = std_gamma(a); y = std_gamma(b); } while (x == 0.0 && y == 0.0);`. Exactly unbiased for a = b by symmetry; for a ≠ b the conditioning error lives entirely inside an event whose correct split is anyway unresolvable at double precision. Expected iterations 1/(1 − P_a·P_b): imperceptible above shape ≈ 0.005, ≈ 1.3 at shape 0.001. Matches the house style of the F/t guards. **Fix (option b):** return `(x == 0.0 && y == 0.0) ? (double)cmb_random_bernoulli(a/(a+b)) : r;` — the correct limiting distribution of Beta(a,b) as a,b → 0, at the cost of consuming an extra uniform on that path. Either is fine; (a) is smaller. **Side effects:** none outside the both-underflow event; single-zero cases continue to return exact 0 or 1, which the documented `[0, 1]` support permits.

### R4 — `cmb_wtdsummary_merge` NaN-poisons on two empty sources: the A1 fix was not mirrored (MEDIUM-LOW) [verified]

The 24 August review's A1 found that `cmb_datasummary_merge` NaN-poisoned when both sources were empty; the fix (a `count > 0` guard with a zero-moment else branch, now at `src/cmb_datasummary.c:143–166`) is in place and verified. The same defect exists, unfixed, in `cmb_wtdsummary_merge` (`src/cmb_wtdsummary.c:215`): with both sources empty, `ws = w1 + w2 = 0` and `d21_w = d21 / ws = 0/0 = NaN`, poisoning m1–m4.

*Repro (provided, `wtd_empty.c`):*

```
merged empty+empty: count=0 mean=-nan m2=-nan
```

The scenario is the natural one for this function's stated purpose ("merging across pthreads"): per-thread weighted summaries where some workers happened to process nothing. Mitigating factors, also verified: a subsequent `add` to the poisoned target fully recovers (the count-0 branch rewrites all moments), one-empty-one-nonempty merges are correct in both orders, and `print` on the empty result is guarded by its count checks — the NaN escapes only through the direct accessors (`mean`, `variance`, …) on a target that stays empty.

**Fix:** mirror the sibling — wrap the moment computation in `if (tws.ds.count > 0u)` with a zeroing else branch (including `wsum`), exactly as `cmb_datasummary_merge` does, and give min/max the same treatment as the sibling's else branch. **Side effects:** none for non-empty merges; empty-merge targets become zero-moment summaries consistent with `cmb_datasummary`. A companion test in the `test_empties` family (which currently covers `datasummary_merge` but not `wtdsummary_merge` — how this one survived the A3 sweep) closes it permanently.

## Secondary findings

### A1 — BTRD casts an unbounded double to `int64_t` before range-checking: formal UB (LOW) [by inspection]

`cmi_random_binomial_btrd` step 3.0 (`src/cmb_random.c:826–829`) computes `k = (int64_t)floor(g*u + c)` *before* the `(k < 0) || (k > dn)` check. With `u = +0.5` exactly — reachable, since `u = (1.0 − cmb_random()) − 0.5` (line 817) yields +0.5 whenever the uniform returns 0.0, probability 2⁻⁵³ per candidate — `us = 0`, `g = 2a/0 = +∞`, and the cast of +∞ to `int64_t` is undefined behavior (C 6.3.1.4). Sufficiently tiny nonzero `us` similarly produces finite values ≥ 2⁶³ with the same UB cast. On x86-64 (cvttsd2si → INT64_MIN) and aarch64 (fcvtzs saturates) the result happens to be rejected by the range check, so the *distribution* is unaffected in practice — but it is formal UB that LTO is entitled to exploit, and a UBSan float-cast-overflow abort waiting for a long-enough run. The Poisson twin guards exactly this (`us <= 0.0` at step 3.0); the guard did not make it into the binomial.

**Fix:** range-check in floating point first — `const double dk = floor(g*u + c); if (dk < 0.0 || dk > dn) continue; const int64_t k = (int64_t)dk;` — or add PTRD's `us <= 0.0` guard. **Side effects:** none on the distribution (the affected candidates are rejected either way); removes the UB and the sanitizer hazard.

### A2 — `cmb_random_pareto` returns +Inf when the uniform hits 0 (LOW) [by inspection]

`include/cmb_random.h:598`: `mode / pow(cmb_random(), 1.0/shape)`. `cmb_random()` is `[0, 1)` and returns exactly 0.0 with probability 2⁻⁵³, giving `mode/0 = +Inf`, which sails through the `x >= mode` debug assert and into the model. **Fix:** `pow(1.0 − cmb_random(), 1.0/shape)` — the same one-uniform cost, domain `(0, 1]`, and `x = mode` exactly (permitted by the documented `[mode, ∞)`) replaces +Inf. This is the same idiom the gamma boost and BTRD already use.

### A3 — Autoscaled histogram can put ordinary data in the ±∞ overflow bins (LOW, display) [verified]

`cmi_dataset_histogram_autoscale` (`src/cmb_dataset.c:683–707`) computes `ncand = ceil((max − ll)/w) + 1` bins needed for coverage but then caps `nb = fmin(nb, ncand)` at the *requested* count (line 701). When `floor(min/w)` pushes the lower edge down by nearly a full bucket and the requested count binds, `hl = ll + nb·w < max` and the top of the data lands in the `[hl, Infinity)` overflow bin. Demonstrated with data uniform on [0.5, 10.5] and 10 requested bins: w = 1, ll = 0, hl = 10, and ~5 % of perfectly ordinary data prints under `[10.00, Infinity)`, while the first and last covered buckets show half-height bars — a uniform distribution rendered with what looks like edge effects and an outlier bucket. This is the same *class* of display artifact (correct data made to look wrong) that the RC1→RC2 histogram work fixed three instances of.

**Fix:** since coverage costs at most two extra bins (ncand ≤ requested + 2 for any w ≥ rng/nb), let coverage win: use `nb = ncand` whenever `ncand` exceeds the request, or equivalently drop the cap. **Side effects:** autoscaled histograms may print up to two more rows than requested; any golden references containing autoscaled histograms would need regeneration — which, per the 29 August harness note, means reading that diff rather than reflex-regenerating.

### A4 — RC2 README markdown regression on the front page (LOW) [verified]

The RC2 benchmark-number edit (`README.md:81–83`) left a stray bullet marker mid-sentence:

```
    _Cimba runs more than twice as fast (41.8M
    events/sec) on a single CPU core as SimPy does when using all 64 logical cores
  * (16M events/sec combined)._
```

The `* ` opens a new list item inside the sentence and splits the `_…_` italic span, so the project's front page renders a broken bullet reading "(16M events/sec combined)._". One-line fix.

### A5 — Broken literature link in `docs/welcome.rst` (LOW) [verified]

Line 96: `<https://informs-sim .org/wsc15papers/004.pdf>` — embedded space in the host name; the rendered link is dead. Pre-existing (not introduced by RC2), but it is the one citation backing the PDES comparison paragraph that RC2's new numbers feed into.

### A6 — Documentation nits in `cmb_random.h` (LOW) [verified]

- `cmb_random_binomial`'s "See also" (line 775) links to the *geometric* Wikipedia article — copy-paste from the entry above (where line 763 is correct).
- The alias-sampling docs reference `cmb_random_alias_draw()` twice (lines 922, 962); the function is `cmb_random_alias_sample()`.
- `cmb_random_poisson` docs say `r < 1e18`; the assert enforces `r <= 1.0e18`.

### A7 — PACF aliasing: an unguarded footgun, modeled by the test suite (LOW) [by inspection]

`test_data.c:846` calls `cmb_dataset_PACF(dsp, 20, acf, acf)` — the same array as both output and ACF input. It is harmless *there* (empty dataset, early warning-return), but on real data the Durbin–Levinson recursion writes `pacf[j]` and later reads `acf[k−j]` from the same storage, silently producing garbage. The self-copy hazard of this shape was closed last cycle for `cmb_dataset_copy` (A2, `tgt != src` assert). **Fix:** `cmb_assert_release(pacf != acf)` in `cmb_dataset_PACF`, and pass a distinct array at `test_data.c:846`. **Side effects:** any user currently aliasing gets a diagnostic instead of wrong numbers.

### A8 — `cmb_random_discrete_uniform` validates its argument at debug level only (LOW) [by inspection]

Alone among the public generators, it uses `cmb_assert_debug(s > 0u)` for argument validation where every sibling uses `cmb_assert_release`. In a release build, `s = 0` silently returns 0 — a value outside the (empty) documented range. **Fix:** promote to `cmb_assert_release` for consistency. **Side effect:** `s = 0` becomes an abort in release, which is a behavior change, but for an input the documentation already declares invalid (`n > 0`).

## Benchmarks and published claims

The RC2 benchmark refresh was reviewed for fairness and internal consistency, since the numbers now headline the README, the docs and the release notes.

**Methodology — sound.** Both sides implement the same M/M/1 (λ = 0.9, μ = 1.0, 10⁶ customers/trial) in the same process-interaction style: exponential interarrival hold → timestamped object into an unbounded store → exponential service hold → system-time accumulation. Both engines are charged the same nominal 4 events/object, so the events/sec chart is a pure wall-time ratio and the constant cancels — the speedup claims are insensitive to the event-accounting convention. The multi-core comparison (Cimba thread-pool trials vs. `multiprocessing.Pool` over identical trials) measures exactly the replication-throughput use case the library targets. External `time` measurement includes process/interpreter startup on both sides; at 0.095 s the Cimba single-core figure absorbs proportionally more startup than SimPy's 5.5 s absorbs, so the reported ratio is, if anything, conservative.

**Numbers — internally consistent.** From the spreadsheet's raw times: single-core 5.52 s / 0.0955 s = 57.8×; multi 24.95 s / 0.441 s = 56.6×; 4 M events / 0.0955 s = 41.9 M ev/s; 400 M / 0.441 s = 908 M ev/s; 908/32 = 28.4 M ev/s/core; 28.4/41.9 = 67.8 % scaling efficiency. Every derived figure in the README, `welcome.rst` and CHANGELOG (30–60×, 41.8 M, 28 M, 67 %, "15–30 % faster than RC1") checks out against the raw data, with the published range chosen conservatively below the measured 57×.

**Three transparency items worth fixing before the claims travel further:**

1. **The build configuration behind the numbers is not stated.** The spreadsheet's flags column shows the Cimba side built with the full `benchopt` stack — `-O3 -fprofile-use -DNDEBUG -DNLOGINFO -DNASSERT -DNMXCSR` — i.e., PGO with all asserts, logging and MXCSR handling removed. That is a supported, documented configuration and a fair thing to benchmark, but a reader following the default build instructions gets `release` (asserts retained) and materially lower numbers. One caption line — "Cimba built with `-Dbenchopt=use` (PGO, `NASSERT`); SimPy on CPython 3.x" — makes the claim reproducible and pre-empts the obvious reviewer objection if these charts end up in the JOSS/SIMULATION submission.
2. **The C benchmark leans on private API.** `MM1_single.c`/`MM1_multi.c` include `"cmi_mempool.h"` and allocate message objects from the internal pool. The SimPy side is the *natural* Python implementation (bare floats in a Store); the natural C counterpart would `malloc`/`free` per object. The pool is legitimate — but it is not what the public API offers users, and in a ~95 ns/object budget the allocator choice is not negligible. Either port the benchmark to public API only, or note the pool use where the benchmark is described. (If a public fixed-size pool is considered useful enough to benchmark with, that is an argument for promoting it to `cmb_` in some future minor release.)
3. **The printed model output invites a wrong first impression.** Both benchmarks run from an empty queue with no warmup deletion, so the printed "Average system time ≈ 9.x (expected 10.0)" systematically undershoots — initialization bias, identical on both sides and irrelevant to the speed ratio, but the library's target audience will notice a point estimate sitting visibly below its label across runs. A comment line ("no warmup deletion; initialization bias expected") keeps the benchmark honest on its own terms.

Minor: `MM1_single.py` imports `time`, `multiprocessing` and `statistics` without using them.

## Strengths

The overall picture from the earlier reviews stands, and this pass adds to it rather than subtracting:

- **The new variate-generation layer is not just fast but demonstrably correct.** Sixteen exact-distribution tests across every dispatch path came back clean at 20 M samples each, and the implementations are faithful, well-referenced transcriptions of the primary literature — with local improvements (PTRD's `us <= 0` guard, the saturating geometric cast, the `log1p`/`exp(n·log1p(−p))` cancellation avoidance) that show the numerics were thought through, not just copied. The one place a guard is missing (A1) is the exception that proves the pattern.
- **The weighted-moments layer is mathematically right.** Both the incremental update and the merge match Pébay et al. term for term, including the sign-sensitive m3/m4 cross-terms that are the classic transcription hazard.
- **The engine remains stable under adversarial load**: byte-identical to the twice-verified snapshot, re-confirmed by a clean full-suite ASan/UBSan run including the 463 s end-to-end test.
- **The benchmark comparison is fundamentally fair** — same model, same worldview, same event accounting, conservative rounding of the published range — which is more than can be said for most cross-language simulation benchmarks in circulation.
- The empty-object hardening from last cycle held up everywhere it was applied; R4 exists precisely because one function was missed, not because the approach failed.

## Recommendation

None of this cycle's findings reaches the severity of the RC1 gates (heap overflow, wrong distributions in the core domain). But three of the four verified defects put wrong or fatal behavior behind *documented, legitimate* usage — R1 silently breaks the reproducibility promise that distinguishes this library, R2 aborts a documented call form, R3 hands NaN to release builds — and all four fixes are small, local, and side-effect-analyzed above. **I would land R1–R4 plus A1/A2 (a few lines each, all in already-reviewed files) and the README fix (A4) before tagging 3.0.0.** The remaining A-items and the benchmark transparency notes can ride along or follow in 3.0.x without embarrassment.

One standing recommendation from the 29 August review remains open and this review re-endorses it with evidence in hand: the exact-PMF chi-square battery that verified the new generators here (and the empty-merge and PACF-NULL repros) should be landed in `test_random`/`test_data` as standing protection. The moment-based quality tests passed throughout every defect this review and the last one found; the exact-distribution tests are the ones that see.

---

*Review performed against commit `3cea210` in release and debug+ASan/UBSan configurations. Reproduction programs: `stat_check.c` (chi-square battery), `binchop.c` (replication), `flip_repro.c` (R1), `pacf_null.c` (R2), `beta_nan.c` (R3), `wtd_empty.c` (R4), `hist_edge.c` (A3); all run against the release build, R3 additionally under `-DNDEBUG` to demonstrate the silent path.*
