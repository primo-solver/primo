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

(already-exposed)=
## Tier 0 -- already exposed, never tuned (zero code change)

These are live CLI options that no search space contains. They are free
dimensions today.

| option | default | note |
|---|---:|---|
| `--lra-pivoting-rule` | `sparse` | 4 values (`bland`/`dantzig`/`soi`/`sparse`); the strongest single categorical |
| `--lra-row-propagation-max-row-size` | 64 | forward row propagation |
| `--lra-row-propagation-max-fanout` | 64 | |
| `--lra-bidirectional-row-propagation-max-row-size` | 32 | reverse direction |
| `--lra-bidirectional-row-propagation-max-fanout` | 32 | |
| `--lra-least-violated-leaving` | off | MathSAT's rule; measured 167 vs 174 overall but **beats most-violated by 5**, i.e. a complementarity candidate, not a loss |
| `--no-lra-sparse-leaving` | on | ablation of the leaving-side half |
| `--lra-fixed-elimination` | off | nets to nothing overall, doubles the >5MB band -- textbook portfolio arm |
| `--lra-propagation`, `--lra-row-propagation`, `--lra-bidirectional-row-propagation` | on/off/off | |
| `--lra-model-phase`, `--lra-theory-decisions` | off/off | see [](#theory-decision-share) before enabling |
| `--theory-phase` | auto | `positive` is known to help the tropical family by firing guards |
| `--preprocessing-passes` / `-adaptive-extra-passes` / `-adaptive-shrink-percent` | | already integer-ranged |
| `--cadical-option name=value` | -- | arbitrary CaDiCaL integers, see [](#cadical) |

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

(implemented)=
## Implemented 2026-08-06: the three Check/Pivot knobs

`LraTableau::Tuning` now carries what were three hardcoded constants, wired
through `CheckSatOptions::lra_tuning` and exposed on the CLI. Defaults are the
former constants, so an untuned run is unchanged -- verified on
`constraint-199552`, which still reports exactly the recorded 7,679 pivots and
128,402,240 pivot term ops.

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

That is worth knowing before spending a search dimension: **check
`:lra.bland-fallbacks` across the corpus first.** If it is zero everywhere, the
knob only does anything in its aggressive direction, which is a robustness arm
rather than a tuning dimension.

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

`minimize_soi_conflict` calls `quickxplain` on every multi-row conflict with
no size guard. Suggested `--lra-soi-minimize-max-vars` (skip minimization
above this error-set size, 0 = never minimize). Smaller conflicts are better
clauses but cost repeated `soi_subset_still_conflicts` evaluations;
`:lra.soi-quickxplain-calls` already measures the volume.

**Separate, non-tuning finding from the same code:** `quickxplain`
(`src/theory/arithmetic/lra_tableau.cpp:770-797`) is **recursive**, with a GCC
diagnostic pragma wrapped around it. That is against this project's standing
rule to use an explicit stack instead. Worth converting independently of any
tuning work -- the error set is bounded by the number of violated rows, so the
depth is data-dependent.

(theory-decision-share)=
### 10. Theory-decision share -- turning a cliff into a dial

`--lra-theory-decisions` is all-or-nothing, and on the tropical family it is
catastrophic: **9.2M theory decisions and a timeout at 90s, against 3.5s
without it** ([](tropical-matrix-structure.md)). A
`--lra-theory-decision-share p` -- supply a theory decision only every p-th
`cb_decide()`, otherwise return 0 and defer to VSIDS -- makes the option
searchable instead of a binary trap, and gives the L1 lead (invert the filter
so *Boolean* flags are decided first) the same dial when it is implemented.

### 11. Model-phase refresh interval

`--lra-model-phase` refreshes at every quiescence point whenever dirty. A
`--lra-model-phase-interval` bounds that cost. Lower priority: the option is
off by default and its cost has not been shown to bind.

## What not to turn into a parameter

- **`--max-theory-conflicts`** already exists but returns `unknown`. A
  portfolio wants *restart under a different configuration*, which is
  scheduling infrastructure, not a solver parameter. Do not overload it.
- **Anything whose counter reads zero across the corpus.** A search dimension
  that cannot change behaviour still costs configurations. Every suggestion
  above names the counter that settles this; check it before including the
  dimension.
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
