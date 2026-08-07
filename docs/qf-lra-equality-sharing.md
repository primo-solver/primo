# What QF_LRA owes Nelson-Oppen: convexity and equality sharing

Written 2026-08-07 for Primo's convex Nelson-Oppen work, based on de Moura and
Bjørner's *Model-based Theory Combination* (`docs/papers/model-guided-no.pdf`,
ENTCS 198:37-49, 2008; see also [](papers/model-based-theory-combination.md)
and [](papers/nelson-oppen-closure-methods.md)).

This note is about **the arithmetic side of the interface**. The paper's §5 is titled
"Simplex: an example model-producing theory solver" and is almost entirely a
specification of what the arithmetic solver must provide. Primo now implements
the sound `FixedEq` and `EqRow` entailment rules with explanations and consumes
them in its mixed QF_UFLRA fixpoint. It does not implement the paper's full
model-producing transition system.

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

## What Primo provides today

`theory::arithmetic::LraTableau::entailed_equalities()` implements `FixedEq`
and `EqRow` over a caller-supplied set of shared variables. Every result carries
the blocking SAT literals for the bounds that establish it. Results form a
deterministic spanning set rather than all pairwise equalities in one class.

After a feasible mixed-theory LRA check, `smt::solve_qf_uf_lra` maps shared Real
terms to their tableau variables, requests these cheap entailments, and routes
them through `TheoryCombinationManager` and guarded SAT clauses. No equality
atom is preallocated: `force_implied_interface_equalities()` creates and
observes only an equality that a producer reports. If either cheap rule adds a
clause, SAT is re-entered and both theories see the equality on the next round.

These rules are deliberately incomplete. Once they and the EUF spanning
equalities reach quiescence for the current SAT assignment, the default
`final` path proves every still-unresolved shared Real pair by testing both
strict separations. This exact fallback preserves completeness of the convex
EUF+LRA combination while avoiding probes for equalities already established
by EUF or the cheap LRA rules. The online paired-bound producer remains an
additional early source; it is not the completeness mechanism.

## What the paper asks of the Simplex solver

### 1. Model-implied equalities — the `M-Propagate` trigger (§5)

> The current model for the simplex solver is given by β. Enumerating the
> equalities implied by β is simple: enumerate all the values of β(xᵢ),
> inserting each value into a hash table. The expected time is O(|B ∪ N|).

The paper's proposed operation is to return the partition of shared Real terms
induced by equal β in one hash pass. Primo intentionally does **not** expose
that partition as entailed equality propagation: equality in one feasible LRA
model is a conjecture, not a logical consequence. The existing default-off
model-equality branch remains a separate SAT-assumption experiment.

Note for implementation: β values are `DeltaRational` (rational + infinitesimal
for strict bounds), and `operator==` already compares both components. Two
variables agreeing on the rational part but differing in δ are *not* equal in
the concrete model, so the delta component must participate in the key — using
only the rational part would over-report equalities and hand the NO layer
unsound propagations.

This operation is cheap, but it is useful only inside the paper's explicit
model-transition machinery. It is not the next convex-combination step and must
not be substituted for entailment.

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
matters to the mixed coordinator: their equality-sharing benefit was absent
from those measurements, so both options need re-measuring **with equality
propagation switched on**, and the answer may differ.

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

**Current decision.** The mixed solver now consumes `entailed_equalities`, but
fixed elimination remains off unless explicitly selected. `FixedEq` therefore
helps by default, while the basis-sensitive `EqRow` opportunity is partly
hidden. The mixed-path default must be decided by the committed 45-file
on/off ablation after fuzzing; the mechanism test pins that enabling fixed
elimination exposes the intended `EqRow` fast path without making completeness
depend on it.

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

## Remaining order of work

1. **Evaluate mixed-only fixed elimination.** Fuzz first, then compare the
   committed 45-file QF_UFLRA sample with the option on and off. Promote it only
   if solved count, wrong answers, and PAR2 justify the extra pivots.
2. **Add `EqOffsetRows`, then `EqRows`.** Each rule needs its own explanation
   invariant, soundness-oracle tests, counters, and a case where it removes
   exact strict-separation probes.
3. **Evaluate row-bound propagation in the mixed path.** Its relevant measure
   is newly exposed fixedness and fewer exact fallback probes, not pure-QF_LRA
   solved count alone.
4. **Implement model partition and freedom-interval mutation only with the
   paper's shared model transition state.** Model equality must remain distinct
   from entailed propagation.

## Combination contract

The current contract is a stream of `(lhs, rhs, blocking reasons)` entailments.
`TheoryCombinationManager` owns their canonical shared-pair identity, current
SAT assignment, and lazily registered equality literal; the LRA tableau owns
the arithmetic proof state. This is enough for convex EUF+LRA because the exact
fallback makes the stream complete at quiescence. It is not an interface for
non-convex arithmetic, integer arithmetic, or the paper's generic mutable model
state.
