# Numerical parameters: what exists, what is hardcoded, what to add

Written 2026-08-06, ahead of a dedicated QF_LRA parameter-tuning effort, and
in the framing of [](open-leads.md)'s complementarity criterion: a
configuration earns a portfolio slot by solving something the others do not,
not by winning on average. Numerical knobs are the cheapest source of such
configurations, because each one is a dial rather than a new mechanism to
validate.

This note is a **code review of where Primo's behaviour is currently fixed by
a constant**, split by how much work exposing that constant would be. It does
not tune anything; the campaign that follows does.

## Where the tuning infrastructure already is

`scripts/irace/` and `scripts/smac/` (documented in [](experiments.md)) are
set up and have been used, but **both are QF_UF only** --
`scripts/irace/qfuf_parameters.txt` is the sole search-space file in the
repository. The first deliverable of the QF_LRA effort is a
`qflra_parameters.txt` alongside it; everything in
[](#already-exposed) can go into it with no code change at all.

(preconditions)=
## A knob is only a dimension where it is reachable

**Reviewed 2026-08-10, and it changed what belongs in the search space.** Most
options here are guarded: they do nothing unless some *other* option is set,
and several of those guards are off by default. A guarded knob handed to irace
is worse than useless — it consumes a dimension, doubles the configurations to
evaluate, and contributes only noise, while looking like a tuned parameter in
the report.

Two of the guards are structural rather than a matter of taste:

- **Pivoting-rule scoping.** `least_violated_leaving_` and `sparse_leaving_`
  are both read as `flag && rule_ == PivotingRule::Sparse`
  (`lra_tableau.cpp:1299`, `:1310`), and `sparse_pricing_candidates` is only
  reached through `select_sparse_entering_variable`, called under `Sparse`
  alone (`:913`). SOI minimization is likewise `Soi`-only.
- **Propagation nesting.** `scan_row_based_propagations` is called only when
  `propagation_enabled_ && row_propagation_enabled_` (`check_sat.cpp:6786`),
  and the bidirectional check sits *inside* that function (`:7107`). So the
  chain is `--lra-propagation` → `--lra-row-propagation` →
  `--lra-bidirectional-row-propagation`, and the last two are **off by
  default**.

**How to check a knob before spending a dimension on it:** run it at two
extreme values and diff a counter that it should move. If nothing changes, it
is not reachable in that configuration. That is how the dead options below
were found.

(already-exposed)=
## Tier 0 -- already exposed, never tuned (zero code change)

Live CLI options that no search space contains. The **requires** column is the
precondition without which the option does nothing.

| option | default | requires | note |
|---|---:|---|---|
| `--lra-pivoting-rule` | `sparse` | — | 4 values (`bland`/`dantzig`/`soi`/`sparse`); the strongest single categorical, and the guard for much of the rest of this table |
| `--lra-row-propagation-max-row-size` | 64 | **`--lra-row-propagation`** | dead until 2026-08-10, see [](#dead-knobs) |
| `--lra-row-propagation-max-fanout` | 64 | **`--lra-row-propagation`** | |
| `--lra-bidirectional-row-propagation-max-row-size` | 32 | **`--lra-bidirectional-row-propagation`** | |
| `--lra-bidirectional-row-propagation-max-fanout` | 32 | **`--lra-bidirectional-row-propagation`** | |
| `--lra-least-violated-leaving` | off | **`--lra-pivoting-rule sparse`** | MathSAT's rule; measured 167 vs 174 overall but **beats most-violated by 5**, i.e. a complementarity candidate, not a loss. Pairing it with any other rule wastes the dimension |
| `--no-lra-sparse-leaving` | on | **`--lra-pivoting-rule sparse`** | ablation of the leaving-side half |
| `--lra-fixed-elimination` | off | — | nets to nothing overall, doubles the >5MB band -- textbook portfolio arm. Note it also forces `model_phase_backtrack_may_change_beta` (`check_sat.cpp:7552`), as `soi` does, so it interacts with `--lra-model-phase` |
| `--no-qf-uflra-fixed-elimination` | mixed elimination on | mixed QF_UFLRA | ablation of the mixed-only default that exposes `EqRow`; pure QF_LRA remains off |
| `--qf-uflra-lra-equality-producer` | `legacy` | mixed QF_UFLRA | `model-unit` selects the CAV 2024 current-model-filtered unit-row producer; `adaptive` runs legacy until a producer round causes no SAT recheck, then model-unit rounds, and returns permanently to legacy when reconciliation still needs a SAT split after model-unit was attempted; both are evaluated, default-off experimental profiles because adaptive preserves the solved set but saves little producer time |
| `--no-qf-uflra-model-reconciliation` | reconciliation on | mixed QF_UFLRA | ablation of the default convex shared-model path; restores explained exact equality sharing |
| `--experimental-generic-model-combination` | off | mixed QF_UFLRA model reconciliation | routes checked EUF/LRA projections and model-choice batches through the backend-neutral sequential coordinator; keeps the established specialized scheduler as the default during parity evaluation |
| `--experimental-qf-uflia-model-combination` | off | QF_UFLIA | starts with input-visible shared equalities, uses EUF/LIA model partitions for reversible equality choices, and retains the complete arrangement as a restart fallback |
| `--experimental-qf-uflia-online-components` | off | complete-arrangement QF_UFLIA with the incremental EUF engine | reuses trail-synchronized EUF and rational-LIA state during SAT search; integer completion still runs after an accepted rational model |
| `--qf-uflia-model-decision-budget` | 16 | `--experimental-qf-uflia-model-combination` | maximum model-choice batches before the complete QF_UFLIA arrangement restart; `0` selects the complete path immediately |
| `--lra-propagation` | on | — | the root of the propagation chain |
| `--lra-row-propagation` | off | **`--lra-propagation`** | |
| `--lra-bidirectional-row-propagation` | off | **`--lra-row-propagation`** (hence `--lra-propagation` too) | |
| `--lra-model-phase`, `--lra-theory-decisions` | off/off | — | independent flags inside the propagator; see [](#theory-decision-share) before enabling |
| `--theory-phase` | auto | — | `positive` is known to help the tropical family by firing guards |
| `--preprocessing-passes` / `-adaptive-extra-passes` / `-adaptive-shrink-percent` | | — | already integer-ranged |
| `--cadical-option name=value` | -- | varies | arbitrary CaDiCaL integers, but several are disabled while an external propagator is attached -- see [](#cadical) |

(dead-knobs)=
### Four options that parse and do nothing

`CheckSatOptions::lra_row_propagation_limits` (`check_sat.hpp:224`) is written
by all four `--lra-*-max-row-size` / `-max-fanout` options in `main.cpp:232-252`
and then **never read anywhere in `src/`**. The propagation code uses
hardcoded constants instead: `kRowBasedPropagationMaxRowSize` /
`...MaxFanout` (`check_sat.cpp:4500-4501`) and their bidirectional
counterparts (`:4510-4511`).

Confirmed empirically, not just by reading: with row propagation switched on,
`--lra-row-propagation-max-row-size 1` and `100000` both give
`:lra.row-propagations 2694` on `sc/sc-9.base.cvc.smt2` — byte-identical
counters at extremes three orders of magnitude apart.

This matters more than a typical dead flag because the table above previously
advertised all four as free dimensions requiring zero code change. Putting
them in `qflra_parameters.txt` would have spent four dimensions on noise.

**Fixed 2026-08-10.** The struct is threaded through `OnlineLraPropagator` and
the two `compute_implied_*_bound` helpers, and the four constants are gone.
The defaults were already 64/64/32/32, identical to the constants, so this is
behaviour-preserving where it matters — verified against the pre-change binary
on 24 completed instances at default settings and 10 more with row propagation
explicitly enabled, 0 answer differences and 0 `:lra.*` counter differences in
both.

The knobs now bind, and one of them was binding all along without anyone being
able to see it: on `sc/sc-9.base.cvc.smt2`, `--lra-row-propagation-max-fanout
100000` yields **3177** row propagations against **2694** at the default 64.
So the forward fanout cap is not a formality on real input — it is discarding
about 15% of available propagations at its default, which is now a tunable
decision rather than a constant nobody could reach.

`row_propagation_limits_are_actually_enforced` in
`tests/unit/qf_lra_solver_tests.cpp` pins it. The test asserts the caps
**bind** rather than that propagation happens, deliberately: a test of the
latter shape would have passed throughout the entire period the options did
nothing.

(cadical)=
### The CaDiCaL sub-space

`--cadical-option` already forwards any integer option, so the entire SAT
search is tunable without touching Primo. The options worth putting in the
space first, given what the QF_LRA measurements show:

`restartint` (2), `stabilize` (1), `stabilizeinit` (1000), `target` (1),
`rephase`/`rephaseint`, `reduceint` (25), `walk` (1) and `walkeffort` (80),
`bumpreason`/`bumpreasondepth`, `chrono`/`chronolevelim`.

The specific reason to expect leverage: on `constraint-199552` the solved run
reports **15,766 decisions against 93 conflicts**
([](tropical-matrix-structure.md)). A nearly conflict-free walk is exactly the
regime where restart/stabilization/local-search policy dominates and clause
management is irrelevant. Caveat to check first: CaDiCaL disables several
inprocessing options while an external propagator is attached, so confirm an
option actually takes effect before spending a search dimension on it.

```{warning}
**Verified 2026-08-12 for `seed`: it is inert.** `--cadical-option seed=1`, `=7`
and `=99` give byte-identical `:sat.decisions` (8420) and `:sat.conflicts`
(726) on `sc/sc-9.base.cvc.smt2`. So the caveat above is not hypothetical --
at least one obvious dimension in the list is already dead under the external
propagator, and it cost a wrong conclusion once: a perturbation experiment
built on `seed` looked like strong evidence until the identical timings gave
it away. Apply the two-extreme-values check from [](#preconditions) to every
CaDiCaL option before it enters the search space.
```

(implemented)=
## Implemented: the Check/Pivot knobs and the SOI minimization arms

`LraTableau::Tuning` now carries what were hardcoded constants, wired through
`CheckSatOptions::lra_tuning` and exposed on the CLI. Defaults are the former
constants, so an untuned run is unchanged -- verified on `constraint-199552`,
which still reports exactly the recorded 7,679 pivots and 128,402,240 pivot
term ops.

**Every one of these is guarded by `--lra-pivoting-rule`**, which is the
single most important thing to know before putting them in a search space:

| knob | requires |
|---|---|
| `--lra-sparse-pricing-candidates` | `--lra-pivoting-rule sparse` |
| `--lra-bland-fallback-factor` / `-offset` | any rule **other than** `bland` -- under `bland` selection is already Bland's, so the fallback threshold is unobservable |
| `--lra-soi-minimize` | `--lra-pivoting-rule soi` |
| `--lra-soi-minimize-order` | `--lra-pivoting-rule soi` **and** `--lra-soi-minimize deletion` -- the order is deletion's visit order and `quickxplain`/`none` ignore it |

Since `sparse` is the default and `soi` is not, the SOI arms are unreachable
unless the rule is set explicitly. Treat the rule as a parent dimension and
these as conditional on its value; irace supports exactly that, and a flat
space would spend most of its budget on combinations that cannot differ.

A unit test (`tuning_knobs_never_change_a_verdict`) pins the property the
whole campaign rests on: these are cost levers, and no value of them may
change a verdict.

(sparse-pricing)=
### `--lra-sparse-pricing-candidates` (default 16, 0 = unbounded)

How many *eligible* entering candidates `select_sparse_entering_variable`
scores before taking the best so far. 1 is the cheapest possible pricing;
0 scores every eligible candidate in the row.

Safety: the limit only ever restricts *scoring*, never the search for whether
an eligible candidate exists -- "no eligible candidate" is what `check()`
reports as a conflict, so an early exit before finding one would manufacture
an unsound conflict. Any value is sound.

**Measured range on `constraint-199552`** (local, diagnostic, 60s budget,
`--guarded-real-equality-lowering`):

| candidates | result | pivots | pivot term ops | time |
|---:|---|---:|---:|---:|
| 1 | timeout (300s budget) | 180,958 | 16,534,507,811 | >300s |
| 4 | timeout | 61,588 | 3,321,322,628 | >60s |
| 8 | sat | 92,386 | 1,873,731,376 | 38.2s |
| **16 (default)** | **sat** | **7,679** | **128,402,240** | **2.8s** |
| 32 | timeout | 179,984 | 2,872,889,041 | >60s |
| 64 | timeout | 163,719 | 2,862,388,347 | >60s |
| 0 (unbounded) | timeout | 184,633 | 2,926,390,362 | >60s |

Two things follow, and the second matters more than the first. The knob has a
**>100x dynamic range in pivot term ops** on a single file, so it is a real
dimension rather than a rounding-error one. But the profile is a **spike, not a
plateau** -- 16 wins and both neighbours lose badly, non-monotonically. Since
this value was never tuned, that is not evidence that 16 is right; it is
evidence that the surface is chaotic, which is the same "search perturbation
changes everything" effect already noted for the leaving rule. A single global
default is the wrong shape of answer here; per-family or portfolio selection
is.

### `--lra-bland-fallback-factor` / `--lra-bland-fallback-offset` (4 / 64)

`check()`'s per-call fallback point, `factor * variables + offset` pivots.
The comment on it was explicit that it was never tuned. Small factor means
switching to Bland's rule early: anti-cycling-safe, slower, a robustness arm.

Termination only needs the fallback to happen after *finitely* many pivots,
not after this particular count, so **every value including 0 is sound** --
0/0 falls back to Bland selection from the second pivot of each call.

**A counter bug found while checking whether this knob is inert.**
`:lra.bland-fallbacks` incremented only under `PivotingRule::Dantzig`, so
under `Sparse` -- the solver's *default* -- it read 0 whether or not the
fallback fired. That made the counter useless for the one question it is
consulted for. Fixed the same day, with a regression test
(`bland_fallback_is_counted_under_the_sparse_rule`). With the fix,
`constraint-199552` reports 0 fallbacks at the defaults and 622 when forced
with `0 0` -- so the default zero is now a *meaningful* zero, and on this file
the threshold genuinely never binds.

**And with the counter fixed, the threshold turns out to bind on real
input.** An earlier draft of this section guessed the knob might be inert
because `constraint-199552` reports 0. It is not: across the
2017-Heizmann-UltimateInvariantSynthesis files in the stratified sample, the
default configuration reports fallback counts of 1, 2, 4, 7, 8 and 12 on
different instances. So `check()` really does exhaust `4 * variables + 64`
pivots within a single call on ordinary QF_LRA benchmarks, and the rule in
force for the remainder of those calls is Bland's -- not the Sparse rule the
solver is nominally configured with.

That makes both coefficients a live tuning dimension rather than a robustness
arm, and it raises a question worth answering before tuning them: on
instances where the fallback fires, how much of the pivoting is happening
under Bland? If it is a large fraction, the measured performance of
`--lra-pivoting-rule sparse` on those files is partly the performance of
Bland, which would matter for interpreting every pivoting-rule comparison in
[](qf-lra-heuristics.md).

## Tier 1 -- hardcoded constants still to expose (small, local diffs)

Each of these is a `constexpr` or a literal in an expression. Exposing means
routing it through `CheckSatOptions` and adding one `add_option` -- no new
mechanism, no new invariant.

### 3. Gaussian-elimination size caps

`src/smt/preprocessing.cpp:375-376`

```cpp
constexpr std::size_t kGaussianEliminationMaxEquations = 128;
constexpr std::size_t kGaussianEliminationMaxVariables = 256;
```

The comment already asks for this: *"Not precisely derived; tunable against
real evidence later."* The cost is O(equations² · variables), so these trade
preprocessing time against tableau size directly.

Local spot check (13 stratified-sample files, `--gaussian-elimination`,
diagnostic only): `:preprocessing.solve-eqs.gaussian-candidates` ranged 0-42,
non-zero on 11 of 13 -- so the pass does real work and the caps are plausibly
binding on the larger instances. The two zeros were not distinguished between
"no equalities" and "cap hit"; that distinction is worth a counter before
tuning, otherwise a search cannot tell an inert dimension from a blocked one.

### 4. Solved-constant substitution blow-up budget

`src/smt/preprocessing.cpp:186-187`

```cpp
constexpr std::size_t kSolvedConstantMaxDefinitionSize = 64;
constexpr std::size_t kSolvedConstantMaxAddedNodes = 200000;
```

Good tuning candidates because bindingness is *already observable*:
`:preprocessing.solve-eqs.rejected-blowups` counts exactly the
rejections these cause. Tune only where that counter is non-zero.

### 5. QF_UF-side caps the existing irace space only toggles on and off

`src/smt/preprocessing.cpp:181-185`: `kMaxBridgeBranches` (64),
`kMaxBridgePairChecks` (200000), `kMaxCongruenceDefinitionClauses` (10000),
`kMaxFullCongruenceApplicationsPerSymbol` (128),
`kMaxFiniteDomainEvaluationAssignments` (250000).

`qfuf_parameters.txt` currently offers `eq_bridge`, `ground_congruence` etc.
as `on`/`off` only. Each of these constants turns one binary dimension into a
sized one, which is usually where the gain is -- "off" and "unlimited" are
rarely both optimal.

### 6. `Rational::kSmallMax` -- a build parameter, not a search dimension

`src/util/rational.hpp:145`, `INT64_MAX`. The class comment states the intent
outright: *"kSmallMax below is the single knob, so the two widths can be
compared empirically rather than argued about"* (Monniaux uses 32-bit; Primo
deviated to 64-bit). This is compile-time, so it belongs in a one-off A/B and
possibly a second portfolio *binary*, not in an irace space.

## Tier 2 -- parameters that need a small mechanism first

(lra-check-interval)=
### 7. LRA check frequency -- the biggest gap, and the one with a precedent

`run_deferred_check_if_needed()` (`src/smt/check_sat.cpp:6715`) runs the full
Check/Pivot repair at **every** quiescence point. Measured on
`constraint-199552`: `:lra.online-check-calls 6528` against
`:lra.theory-checks 1`.

There is nothing to tune here today, and yet **the EUF side already has
exactly this idea parameterized** -- `--prop-interval` and
`--prop-assign-threshold`, both already in `qfuf_parameters.txt`. The
arithmetic analogues:

- `--lra-check-interval k` -- run the repair every k-th quiescence point.
- `--lra-check-assign-threshold f` -- skip while fewer than fraction `f` of
  arithmetic atoms are assigned.

**Requires:** nothing -- the repair runs on every path, which is exactly why
this is the biggest gap. Unlike most of the table it would be a knob with no
guard above it.

Both are completeness-preserving provided the check is forced at
`cb_check_found_model`, which is where the verdict actually matters. This is
the closest thing on the list to a cheap approximation of relevancy
propagation (lead L2 in [](tropical-matrix-structure.md)) -- deferring
arithmetic on assignments that later turn out irrelevant is precisely the
waste L2 targets, and a working interval knob is evidence about whether the
much larger L2 mechanism is worth building.

### 8. Same-slack propagation fanout cap

Row propagation has four caps; the default-on same-slack propagation
(`scan_for_propagations`) has none. It walks occurrence lists that average
**679 entries** on the tropical family -- the same measurement that motivated
the cross-link change. A `--lra-propagation-max-fanout` mirroring the row
version's cap is a two-line addition and directly bounded work.

### 9. SOI conflict-minimization budget

**Superseded 2026-08-10 — the original suggestion here was aimed the wrong
way, and the counter it cited does not measure what it claimed.**

The suggestion was `--lra-soi-minimize-max-vars`, skipping minimization *above*
a given error-set size on the theory that large sets cost repeated
`soi_subset_still_conflicts` evaluations, citing
`:lra.soi-quickxplain-calls` as already measuring the volume. Both halves were
wrong:

- **That counter measured successes, not calls.** It only incremented when
  minimization actually shrank the set (a 6x under-report: 345 against 2253),
  which made a high success rate look like a low one. It is now split into
  `:lra.soi-minimize-calls`, `:lra.soi-minimize-shrunk` and
  `:lra.soi-conflict-lits`.
- **There are no large sets to skip.** Instrumented over 2253 calls: 82% have
  a single element, the rest average 2.83, and the largest seen was 9. A
  size-threshold knob would never usefully fire, and minimization *succeeds*
  88% of the time on the sets that have more than one element — so a threshold
  would be disabling the case that pays.

What replaced it is `--lra-soi-minimize {quickxplain,deletion,none}` with
`--lra-soi-minimize-order {id,row-support,violation}`. Since minimization
reduces the set to 1.23 elements on average, there are usually several equally
minimal answers and the algorithm silently picks one; deletion makes that pick
orderable, where QuickXplain's recursion cannot be steered. The quantity to
watch is `:lra.soi-conflict-lits`, not the set size — `build_soi_conflict`
emits one literal per nonzero entry of the restricted SOI row *plus* one per
error variable, and the first term usually dominates.

**Separate, non-tuning finding from the same code:** `quickxplain`
(`src/theory/arithmetic/lra_tableau.cpp:770-797`) is **recursive**, with a GCC
diagnostic pragma wrapped around it. That is against this project's standing
rule to use an explicit stack instead. Worth converting independently of any
tuning work -- the error set is bounded by the number of violated rows, so the
depth is data-dependent.

(theory-decision-share)=
### 10. Theory-decision share -- turning a cliff into a dial

**Requires `--lra-theory-decisions` (off by default).** The share dial is a
refinement of that flag, so it inherits its guard -- searching `p` without
turning theory decisions on measures nothing.

`--lra-theory-decisions` is all-or-nothing, and on the tropical family it is
catastrophic: **9.2M theory decisions and a timeout at 90s, against 3.5s
without it** ([](tropical-matrix-structure.md)). A
`--lra-theory-decision-share p` -- supply a theory decision only every p-th
`cb_decide()`, otherwise return 0 and defer to VSIDS -- makes the option
searchable instead of a binary trap, and gives the L1 lead (invert the filter
so *Boolean* flags are decided first) the same dial when it is implemented.

### 11. Model-phase refresh interval

**Requires `--lra-model-phase` (off by default).**

`--lra-model-phase` refreshes at every quiescence point whenever dirty. A
`--lra-model-phase-interval` bounds that cost. Lower priority: the option is
off by default and its cost has not been shown to bind. Note also that
`--lra-pivoting-rule soi` and `--lra-fixed-elimination` both set
`model_phase_backtrack_may_change_beta`, so what a refresh costs depends on
those too.

## What not to turn into a parameter

- **`--max-theory-conflicts`** already exists but returns `unknown`. A
  portfolio wants *restart under a different configuration*, which is
  scheduling infrastructure, not a solver parameter. Do not overload it.
- **Anything whose counter reads zero across the corpus.** A search dimension
  that cannot change behaviour still costs configurations. Every suggestion
  above names the counter that settles this; check it before including the
  dimension.
- **Anything guarded by an option the space holds at its default.** The
  strongest form of the point above: see [](#preconditions) for the guards
  that actually exist, and [](#dead-knobs) for four options that were
  advertised here as free dimensions while reading nothing at all.
- **Correctness-relevant limits.** None of the above changes an answer, only
  how long it takes to reach one. Keep it that way: the fuzz gate
  ([](fuzz-coverage-qf-lra.md)) validates models, and a parameter that can
  change `sat`/`unsat` would make a tuning campaign a correctness risk rather
  than a performance one.

## Suggested order

1. `qflra_parameters.txt` over Tier 0 plus the three knobs in
   [](#implemented) -- no further code change, and it calibrates how much a
   search can move QF_LRA at all.
2. [](#lra-check-interval) -- the remaining item with measured evidence that
   the current fixed behaviour binds.
3. The remaining Tier 1 constants, each gated on its counter being non-zero.
4. Tier 2 items 8-11.

A warning the pricing sweep has already earned: the response surface is
non-monotone and spiky, so a search that reports a single best value has
probably fitted noise on whichever instances dominated its training set.
Validate on a held-out set, and prefer reporting a *set* of complementary
configurations over a new default -- which is the framing
[](open-leads.md) already argues for.
