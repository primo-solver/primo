# What QF_LRA owes Nelson-Oppen: convexity and equality sharing

Written 2026-08-07 for the Nelson-Oppen work on
`nelson-oppen-online-lazy-ipasir-up`, which is implementing de Moura and
Bjørner's *Model-based Theory Combination* (`docs/papers/model-guided-no.pdf`,
ENTCS 198:37-49, 2008; see also [](papers/model-based-theory-combination.md)
and [](papers/nelson-oppen-closure-methods.md)).

This note is about **our side of the interface**. The paper's §5 is titled
"Simplex: an example model-producing theory solver" and is almost entirely a
specification of what the arithmetic solver must provide. Most of it we do not
provide yet.

## Why convexity is the whole point

**Linear rational arithmetic is convex** (paper §2: a theory is convex iff
`Γ ⊨ ⋁ᵢ uᵢ ≃ vᵢ` implies `Γ ⊨ uⱼ ≃ vⱼ` for some `j`). Linear *integer*
arithmetic is not — the paper's counterexample is `0 ≤ x₁,x₂,x₃ ≤ 1`, which
entails a disjunction of equalities but no single one.

The consequence is the reason this matters (paper §2.1):

> For convex theories, instead of *guessing*, one can **deduce** the equalities
> to be shared. The key idea is to propagate `x ≃ y` to `T₂` whenever
> `T₁ ∪ Γ₁ ⊨ x ≃ y`, and vice-versa. This process is repeated until no further
> equations can be propagated.

So **completeness on our side buys determinism on theirs.** If QF_LRA reports
*every* entailed equality between shared variables, the combination layer needs
no arrangement search, no case splits over `u ≃ v`, and no backtracking on
partitions — just a propagation fixpoint. Every equality we fail to deduce is a
case split the NO layer has to make instead.

That is the trade to put on the table before the interface is fixed.

## What we provide today: almost nothing

The only equality-sharing mechanism is
`enqueue_lra_interface_equality_if_forced` in `src/smt/check_sat.cpp`. It is
**purely syntactic**: it fires when both `x ≤ y` and `y ≤ x` exist *as atoms in
the formula* and both are currently assigned true.

It therefore cannot see an equality that the tableau entails but the input did
not spell out as a pair of opposing non-strict inequalities — which is most of
them. `x ≃ y` following from `x - y + z = 0 ∧ z = 0` is invisible to it.

What the tableau does expose (`src/theory/arithmetic/lra_tableau.hpp`):
`assignment(var)` (that is β), `upper_bound`/`lower_bound`, `is_basic`,
`row(var)`, `occurs_in(var)`. The raw material is there; none of the equality
machinery is.

## What the paper asks of the Simplex solver

### 1. Model-implied equalities — the `M-Propagate` trigger (§5)

> The current model for the simplex solver is given by β. Enumerating the
> equalities implied by β is simple: enumerate all the values of β(xᵢ),
> inserting each value into a hash table. The expected time is O(|B ∪ N|).

**Proposed API**: given the shared real terms, return the partition of them
induced by equal β. One hash pass, no pivoting, no entailment check.

Note for implementation: β values are `DeltaRational` (rational + infinitesimal
for strict bounds), and `operator==` already compares both components. Two
variables agreeing on the rational part but differing in δ are *not* equal in
the concrete model, so the delta component must participate in the key — using
only the rational part would over-report equalities and hand the NO layer
unsound propagations.

This is cheap and is the single highest-value thing we can add.

### 2. Opportunistic equality propagation (§5.1)

Four rules, all inexpensive, all on the tableau. `P` denotes a linear
polynomial; `P` is *fixed* iff every variable in it is fixed (`lᵢ = uᵢ`) or has
coefficient 0; `β(P)` is its value under β.

| rule | premise | conclusion |
|---|---|---|
| **FixedEq** | `lᵢ = uᵢ = lⱼ = uⱼ` | `xᵢ ≃ xⱼ` |
| **EqRow** | `xᵢ ≃ xⱼ + P`, `P` fixed, `β(P) = 0` | `xᵢ ≃ xⱼ` |
| **EqOffsetRows** | `xᵢ ≃ x_k + P₁`, `xⱼ ≃ x_k + P₂`, both fixed, `β(P₁) = β(P₂)` | `xᵢ ≃ xⱼ` |
| **EqRows** | `xᵢ ≃ P + P₁`, `xⱼ ≃ P + P₂`, `P₁,P₂` fixed, `β(P₁) = β(P₂)` | `xᵢ ≃ xⱼ` |

Implementation notes from the paper: FixedEq via a map from value to fixed
variables; EqRow checked cheaply when a row is updated during a pivot;
EqOffsetRows via a map `(x_k, v) ↦ xᵢ`. **The first three are applied eagerly;
EqRows only before `M-Propagate`**, being the expensive one.

### 3. Model mutation via freedom intervals (§5.2)

The *freedom* of a non-basic `xⱼ` is `[Lⱼ, Uⱼ]`, computed from the rows it
occurs in; `update(xⱼ, v)` for any `v` in that interval preserves feasibility
without pivoting. The greedy heuristic: pick `v` that **increases** the number
of equal-value classes, i.e. makes β more diverse.

```{note}
This page previously said mutation *reduces* the class count. That is backwards
and worth stating plainly, because the sign is the whole algorithm: more
classes means fewer variables sharing a value, which is the diversity being
sought. The paper's obligation on a mutator is
`|classes(R_M)| ≤ |classes(R_δ(M))|` — never decrease it — and its greedy step
keeps an update only when `|classes|` strictly grows.
```

Why this matters to the NO side: every pair that merely *happens* to share a β
value becomes a case split under `M-Propagate`. Diversifying β before
propagating removes coincidental equalities and shrinks that set. The paper
reports the average number of shared equalities actually propagated by Z3 as
**2.46**, maximum 57 — small, because of exactly this.

**Implemented on the LRA side** (branch `qflra-model-mutation`), still with no
caller:

| method | what it is |
|---|---|
| `LraTableau::freedom_interval(nonbasic)` | `[Lⱼ, Uⱼ]`, `nullopt` endpoint = unbounded |
| `LraTableau::diversify_shared_model(shared)` | the greedy sweep, returning the post-mutation classes, the updates taken, and the class count before/after |

Three properties are worth knowing before wiring it up:

- **Nonbasic only, and that is the paper's choice, not a shortcut.** §5.2
  diversifies "without performing any pivoting operation", so a basic
  variable's value moves only through its row. This is what keeps mutation
  clear of `entailed_equalities`' basis-dependence.
- **It does not touch the tableau.** The sweep runs on a scratch copy of the
  assignment. That is forced, not stylistic: `update()` trails the old value
  only under `PivotingRule::Soi`, and `pop()` restores bounds alone (item H9),
  so a mutation applied to the live tableau in the default mode could not be
  undone on backtrack.
- **A mutated β is a genuine model**, since every value in the interval
  satisfies all rows and bounds. So a disequality read off it is true in some
  model — a stronger guarantee than the current `check_sat.cpp` proxy, which
  is sound only because a solver call said so.

The proxy it is meant to replace is `lra_model_equality_can_be_mutated` on the
NO branch, which builds a fresh `PreparedLraTheory` and solves **twice per
candidate pair**, and then does not actually mutate anything — it suppresses
the guess and leaves β with `x = y`. Swapping it is deferred to that branch,
since it lives in the file where the two trees diverge hardest.

## Two things we own that are already implemented — and switched off

This is the immediately actionable part, and it needs deciding on our side
rather than theirs.

**`--lra-fixed-elimination` (item H7) is off by default.** The paper, §5.1:
"We also aggressively remove fixed variables from the basis." Fixed variables
are the premise of three of the four opportunistic rules, and a fixed variable
sitting in the basis is one the rules cannot use. Our own note in
[](arithmetic.md) records H7 as netting to nothing on the scrambled competition
set — measured *without* any equality propagation depending on it. Under MBTC
its value changes, because it is no longer only a pivoting economy.

**`--lra-row-propagation` is off by default.** The paper, §5.1, immediately
after listing the rules:

> These rules can miss some implied equalities. [...] Fortunately, the bound
> propagation technique described in [8] can be used imply the bound `0 ≤ w`,
> making `w` a fixed variable, and enabling the application of the rule EqRow.

Reference [8] is Dutertre & de Moura — the paper our tableau is built from, and
`--lra-row-propagation` is that technique. So bound propagation is not only a
search economy here; it is what *makes variables fixed*, which is what makes
the opportunistic rules fire. Its own evaluation
([](qf-lra-heuristics.md)'s D1) found it neutral-to-negative on solved count —
again, measured with nothing downstream that consumed fixedness.

**Neither default should be flipped on this reasoning alone.** Both were
measured and both are off for good empirical reasons. The point is narrower and
should be stated to whoever owns the NO branch: *their* benefit was never in
those measurements, so if MBTC lands, both options need re-measuring **with the
equality propagation switched on**, and the answer may differ.

(fixed-elimination-for-no)=
### Should `--lra-fixed-elimination` be on by default for Nelson-Oppen?

Asked directly, and the evidence assembled 2026-08-07 says **yes, scoped to the
mixed path, and as part of the wiring rather than now**.

**Why yes.** Under theory combination it is not an optimisation. Same
constraints, basis differs, 0 equalities against 1 — see [](#measured-gap). It
decides what we can deduce.

**Why scoped to the mixed path is close to free.** After the dispatch narrowing
(`--mixed-dispatch narrow`, the default), the mixed path is essentially unused
on pure QF_LRA: **0 of 60 sampled division files** dispatch to
`:solver.qf-uflra`. So switching H7 on *when the mixed solver runs* changes
nothing on the QF_LRA division by construction, and only affects genuine
QF_UFLRA input.

**Why not flip the global default instead.** That is a separate question with
its own evidence, and the evidence is mildly *positive* rather than negative:
374 against 373 solved on the 595-file scrambled set, median 0.94x, the >5MB
band going 7 → 14 of 50, and mid-band median 0.64x. H7 is opt-in because it
"nets to nothing" on headline solved count, not because it costs anything.
Flipping it globally is defensible on its own merits — but doing it *as part of*
the NO work would confound two changes, and the QF_LRA measurement above would
then have to be redone anyway.

**Why not now.** Nothing consumes `entailed_equalities` yet, so enabling it
today buys no deduction and changes mixed-path search unmeasured. And the
QF_UFLRA corpus is **not staged on the cluster**, so neither the cost nor the
benefit is currently measurable. Stage it first.

(measured-gap)=
## The completeness gap, measured rather than assumed

`entailed_equalities` (FixedEq + EqRow) is deliberately incomplete. A
differential test in `tests/unit/lra_tableau_tests.cpp` pins *how* incomplete by
checking every reported equality against an independent oracle built from the
tableau itself: given a slack `s = x - y`, the equality `x ≃ y` holds in every
model iff neither `s > 0` nor `s < 0` is satisfiable. Same idea as
`lra_implied_real_equalities`, expressed against a tableau we already have
rather than a fresh one per pair.

**Soundness holds** — everything the rules report survives the oracle.

**The shape of what they miss is now concrete, and it is not what I first
assumed.** Pinning `s = x - y` to 0 entails `x ≃ y`, and neither rule finds it:

- **FixedEq** needs `x` and `y` themselves fixed. Here only their *difference*
  is.
- **EqRow** only inspects rows of shared **basic** variables. Here the basic
  variable is `s`, which is not shared at all.

The first reading of that was "this is the shape `EqOffsetRows`/`EqRows`
cover". **Measured, that is wrong** -- those two rules also relate *basic*
variables, so they do not reach it either. The gap is a property of the
**basis**, not of the constraints:

| | `s` basic? | `x` basic? | equalities deduced |
|---|---|---|---:|
| `--lra-fixed-elimination` off | yes | no | **0** |
| `--lra-fixed-elimination` on | no | **yes** | **1** |

Identical constraints both times. Pivot the fixed slack out of the basis and
the row becomes `x = y + s` with `s` fixed at 0 -- exactly EqRow's premise, so
it fires.

This is the paper's own remark in §5.1, *"We also aggressively remove fixed
variables from the basis"*, demonstrated rather than quoted. It also sharpens
the argument two sections above: **`--lra-fixed-elimination` is not merely a
pivoting economy under theory combination -- it decides what we can deduce at
all.** It was measured off on grounds that had nothing downstream consuming
fixedness, and that measurement does not transfer.

Pinned by `fixed_elimination_closes_the_eqrow_basis_gap` in
`tests/unit/lra_tableau_tests.cpp`.

## Proposed order of work on our side

1. **Model-implied equality partition** (§5). Small, self-contained, no
   behaviour change when unused, and it unblocks `M-Propagate` on their side.
   Falsifier: on a QF_UFLRA instance with shared reals, the partition must
   refine as bounds tighten, and must never merge two variables whose
   `DeltaRational` values differ in the δ component alone.
2. **Explanations for propagated equalities.** Anything we propagate needs a
   reason set for conflict analysis, in the same shape as `build_conflict`'s
   Farkas-style reasons. Without this the NO layer can propagate but not
   explain, and every equality becomes a decision rather than an implication.
   *This is the piece most likely to be underestimated.*
3. **FixedEq, EqRow, EqOffsetRows** eagerly; **EqRows** before `M-Propagate`.
4. **Freedom-interval diversification**, last — it is an optimisation of the
   number of case splits, not a correctness obligation.

## What we should ask the NO branch for

- **The interface shape**: do they want a partition of shared terms, a stream
  of `(x, y, reason)` propagations, or both? (1) and (3) produce different
  things — a model-induced partition is a *conjecture* needing a case split;
  an opportunistic rule firing is an *entailment* needing an explanation. The
  distinction matters and the API should keep them apart rather than blur them.
- **Whether they intend to rely on convexity.** If yes, the value of completing
  our equality deduction is high and arrangement search can go. If they keep
  arrangement search for QF_UFLIA-style non-convex reasons later, the calculus
  is different and (1) alone may be enough.
- **A shared benchmark**: none of this can be evaluated on QF_LRA alone. The
  QF_UFLRA corpus is **not staged on the cluster** — worth fixing before either
  side claims a measurement.
