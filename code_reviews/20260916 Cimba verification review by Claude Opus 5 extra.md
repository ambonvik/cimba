# Cimba Verification Review — Closing the RC2 Cycle

**Subject:** Cimba — multithreaded discrete event simulation library in C
**Version reviewed:** 3.0.0-RC2, `main` at commit `795d87d` ("Minor wording fix")
**Reviewer:** Claude (Anthropic), AI-assisted verification review requested by the maintainer
**Date:** 16 September 2026
**Scope:** Closes the cycle opened by the 30 August code review.
**Environment:** Ubuntu 24.04 x86-64, GCC 13.3, Meson 1.12, NASM 2.16, single-vCPU container. Linux paths exercised at runtime. Windows/MSYS2, ThreadSanitizer, PGO and CUDA configurations not exercised here; the project's CI matrix covers the first three.

## Method

Everything reported as verified below was checked by building the tree and executing it, not by reading diffs. The two ziggurat results were re-measured with probe programs written for this review, linked against the built library and independent of the project's own test code.

## Baseline

| | Result |
|---|---|
| Release build, `warning_level=3` | Compiles clean, **zero warnings** |
| Release test suite | **25/25 pass** |
| Debug + ASan/UBSan build | Compiles clean |
| Debug + ASan/UBSan test suite | **25/25 pass**, no sanitizer findings |
| `cimba` end-to-end test under sanitizers | 366.4 s, pass |

The suite has grown from 24 to 25 tests this cycle, with `testutils` added for the new statistical machinery. Run times under sanitizers are up substantially — `random` at 43 s and `stochastic` at 45 s against sub-second times for most others — reflecting the goodness-of-fit battery now running on every push.

## Status of the 30 August findings

All twelve findings verified fixed in source and, where observable, by execution. Nothing from that review remains open.

| ID | Finding | Status |
|---|---|---|
| R1 | `cmb_random_flip` bit cache survived re-seeding, breaking per-seed trial reproducibility | **Fixed.** `flip_bits`/`flip_bitpos` are now file-scope and reset in `cmb_random_initialize`, which pre-fills a full word and sets `flip_bitpos = 64`. |
| R2 | `cmb_dataset_PACF` aborted on its documented `acf = NULL` convenience form | **Fixed.** The stray assert is gone; the surviving `acf != NULL` assert at line 810 is in `cmb_dataset_ACF`, where it is correct. |
| R3 | `cmb_random_std_beta` returned silent NaN for small shape parameters | **Fixed.** The retry loop (`while ((x == 0.0) && (y == 0.0))`) is in place, matching the guards already used by `F_dist` and `std_t_dist`. |
| R4 | `cmb_wtdsummary_merge` NaN-poisoned on two empty sources | **Fixed.** The `count > 0` guard now mirrors `cmb_datasummary_merge`. |
| A1 | BTRD cast to `int64_t` before range check (formal UB) | **Fixed.** Both the PTRD-style `us <= 0.0` guard and a floating-point range check (`dk < 0.0 \|\| dk > dn`) precede the cast. |
| A2 | `cmb_random_pareto` returned +Inf when the uniform hit exactly 0 | **Fixed.** Now draws `y = 1.0 - cmb_random()` on `(0,1]`. |
| A3 | Autoscaled histogram could put ordinary data in the ±∞ overflow bins | **Fixed.** `ncand` is now `floor((max - ll)/w) + 1.0`, the exact bin count required, with the cap removed. |
| A4 | Stray bullet marker splitting an italic span in `README.md` | **Fixed.** |
| A5 | Broken `informs-sim .org` link in `welcome.rst` | **Fixed.** |
| A6 | Documentation nits in `cmb_random.h` | **Fixed.** |
| A7 | `cmb_dataset_PACF` aliasing footgun | **Fixed.** A `pacf != NULL` assert now sits alongside, and the aliased call in the test suite is gone. |
| A8 | `cmb_random_discrete_uniform` validated its argument at debug level only | **Fixed.** Now `cmb_assert_release`. |

## The defect found this cycle

The cycle's substantive result is a **real bias in both ziggurat samplers**, present in every Cimba release before 19 September 2026, found by the test battery built during this cycle and fixed at commit `d80a2ea`.

**Mechanism.** In `cmi_random_exp_not_hot` and its normal counterpart, the value `i_cand_x` served as the alias-table coin flip and was then reused as the X coordinate inside the selected overhang triangle. After the comparison `i_cand_x >= exp_zig_i_prob[jdx]`, that value is no longer uniform — it is conditioned large when the alias branch is taken and small when `jdx` is kept. Because the alias decision correlates with which region is selected, and because the exponential ziggurat's top region covers x ∈ [0, 0.1225] while carrying roughly 44 % of not-hot traffic, the bias concentrated near the origin. The fix draws an independent value for the coin flip, at a cost of one extra `sfc64` call on the not-hot path only.

**Magnitude before the fix**, measured by binomial tail test at n = 10⁷ with the inversion method as control:

| t | ziggurat deficit | inversion deficit |
|---|---|---|
| 0.001 | −3.1 % | −0.9 % |
| 0.010 | −2.7 % | −0.3 % |
| 0.030 | −2.4 % | −0.1 % |
| 0.060 | −0.4 % | −0.1 % |
| 0.100 | −0.03 % | −0.1 % |

**Verified after the fix**, re-measured for this review at n = 2×10⁷ with independent probes:

```
exponential, P(X <= t)              normal, P(|Z| <= t)
t        deficit    sigma           t        interval z
0.001    +0.045%    -0.06           0.005      -0.90
0.005    -0.008%    +0.02           0.010      -0.30
0.010    -0.099%    +0.45           0.020      -1.70
0.020    +0.095%    -0.60           0.050      -1.17
0.030    +0.215%    -1.68           0.100      -0.32
0.100    +0.107%    -1.55           0.200      +0.55
```

Every deviation lies inside 1.7σ with no sign pattern. (The normal's cumulative counts are nested, so its raw columns appear uniformly negative; the per-interval figures above are the independent ones.)

**Why it hid for so long.** The direct exponential goodness-of-fit test spread the affected samples across 7 of 256 bins and reported 1.5σ. Every moment test passed throughout. The defect surfaced only when a geometric distribution at p = 0.01 — which consumes the exponential through `ceil(E/denom)` and concentrates the affected region into three cells out of twenty-one — reported 5.5σ reproducibly across seeds. An omnibus test spread thin missed what an aimed test found immediately.

The CHANGELOG entry for RC3 states the magnitude, the affected versions and the fixing commit, which is the right disclosure for a user with results from an earlier build.

## The new verification infrastructure

The cycle's other output is a goodness-of-fit battery now running in CI. This is the standing protection recommended by both the 29 and 30 August reviews, and its absence had been the outstanding item since.

**Continuous distributions** are tested through the probability integral transform against their own CDFs, then subjected to Pearson's χ² on a coarsened binning; a Neyman smooth test decomposing the binned residual into four named orthogonal components (mean, variance, skewness, kurtosis) plus a remainder, Fisher-combined; and Anderson–Darling on the EDF. The two families are combined by Bonferroni, which is valid under the dependence that actually exists between them (measured correlation with Pearson: 0.36 for the Neyman components, 0.24 for AD).

**Discrete distributions** use the same decomposition built directly on the support with a PMF-weighted orthonormal basis, avoiding the binning approximation entirely and making the components read on the original scale. Anderson–Darling does not transfer to discrete data and is correctly omitted.

**Supporting numerics implemented this cycle:** a log-space regularized incomplete gamma; a log-space regularized incomplete beta (continued fraction with the transition point from Egorova, Gil, Segura & Temme 2023); AS 241 for the normal quantile; and Marsaglia & Marsaglia's Anderson–Darling distribution. All were implemented from published formulas rather than transcribed code, and verified against independent high-precision references.

**Coverage** now spans, with goodness-of-fit running in CI: uniform (continuous and discrete), triangular, normal (standard and scaled), exponential (standard and scaled), Erlang, gamma, beta (2- and 4-parameter), PERT and modified PERT, Cauchy, Pareto, Student's t (standard and scaled), geometric, binomial, negative binomial, Poisson, Bernoulli, dice, and Vose alias sampling. The alias generator had no independent verification of any kind before this cycle.

**Generator-level evidence** is separate and complementary: PractRand to 32 TB on `sfc64` output, plus seed-sweep runs at 1, 64, 65536 and 10⁶ words per trial seed, exercising the `fmix64` → splitmix64 → sfc64 initialization path that no published result covers.

**Test design.** The suite deliberately runs two ways: `tools/test_stochastic.py` with fixed seeds against reference files, and the quality tests seeded from hardware entropy so that cumulative CI runs explore a wide seed space rather than one happy path. Each test program prints its seed at the top of its output, so any failure is reproducible with `-s`. At the battery's 6σ action threshold the per-run false-alarm probability is around 10⁻⁷, so the free-running design costs nothing in stability while adding real coverage. This is the better arrangement, and worth stating explicitly in the validation write-up — a reader who sees a hardware seed will otherwise wonder about reproducibility.

## Assessment

All twelve findings from 30 August are closed. A real defect in the most-used variate generator was found, fixed, and independently confirmed by three routes — the generator's own tail probe against an inversion control, a geometric distribution consuming it indirectly, and a simulation of both code paths. The library builds clean, passes 25/25 in both release and sanitizer configurations, and now carries standing statistical protection against the class of defect that every previous review in this series found by hand.

**RC3 is warranted and I see no blockers.** The cycle carries a substantive correctness fix, not only documentation, and the disclosure of it is already in place.

One observation worth carrying forward. Two defects, this cycle and last, were of the same kind — a rare path at extreme parameters (`std_beta` underflow at shape 0.005; the ziggurat's top region) invisible to moment tests and to omnibus distributional tests, located only by an instrument aimed at the right place. The battery now in CI is the general protection; the targeted binomial tail probes are what actually found both. Keeping those alongside the omnibus battery is worth the lines.

---

*Verified at commit `795d87d` in release and debug+ASan/UBSan configurations. Ziggurat tail probes written for this review and linked against the built library; all other results from the project's own suite.*
