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
without pivoting. The greedy heuristic: pick `v` that **reduces** the number of
equal-value classes, i.e. makes β more diverse.

Why this matters to the NO side: every pair that merely *happens* to share a β
value becomes a case split under `M-Propagate`. Diversifying β before
propagating removes coincidental equalities and shrinks that set. The paper
reports the average number of shared equalities actually propagated by Z3 as
**2.46**, maximum 57 — small, because of exactly this.

We already have the ingredients: `update()` exists in the tableau, and
`can_enter` already reasons about how far a non-basic variable may move.

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
