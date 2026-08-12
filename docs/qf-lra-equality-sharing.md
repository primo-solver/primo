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

`LraTableau::model_equal_classes()` separately implements the paper's linear
model-value partition over caller-supplied shared variables. It hashes the full
`DeltaRational` value, drops singleton classes, and returns deterministic
candidate classes. The mixed coordinator consumes this API as a
necessary-condition filter before exact entailment probes; the separate
model-equality experiment still has its own query and mutation path. Keeping
the API separate from `entailed_equalities()` is load-bearing because model
coincidence is a conjecture, not a theory consequence.

After a feasible mixed-theory LRA check, `smt::solve_qf_uf_lra` maps shared Real
terms to their tableau variables, requests these cheap entailments, and routes
them through `TheoryCombinationManager` and guarded SAT clauses. No equality
atom is preallocated: `force_implied_interface_equalities()` creates and
observes only an equality that a producer reports. If either cheap rule adds a
clause, SAT is re-entered and both theories see the equality on the next round.

These rules are deliberately incomplete. Once they and the EUF spanning
equalities reach quiescence for the current SAT assignment, the default
`final` path first removes pairs whose values differ in the current feasible
LRA model, then proves every still-unresolved model-equal pair by testing both
strict separations. An entailed equality must hold in the current model, so this
filter preserves completeness while avoiding probes for cross-value pairs and
equalities already established by EUF or the cheap LRA rules. The online
paired-bound producer remains an additional early source; it is not the
completeness mechanism.

## What the paper asks of the Simplex solver

### 1. Model-implied equalities — the `M-Propagate` trigger (§5)

> The current model for the simplex solver is given by β. Enumerating the
> equalities implied by β is simple: enumerate all the values of β(xᵢ),
> inserting each value into a hash table. The expected time is O(|B ∪ N|).

Primo now implements this operation as `model_equal_classes()`, but intentionally
does **not** expose its result as entailed equality propagation: equality in one
feasible LRA model is a conjecture, not a logical consequence. The existing
default-off model-equality branch remains a separate SAT-assumption experiment
and does not yet use the partition API.

Note for implementation: β values are `DeltaRational` (rational + infinitesimal
for strict bounds), and `operator==` already compares both components. Two
variables agreeing on the rational part but differing in δ are *not* equal in
the concrete model, so the delta component must participate in the key — using
only the rational part would over-report equalities and hand the NO layer
unsound propagations.

This operation is cheap, but its result must not be substituted for entailment.
Primo uses unequal model values only to reject impossible entailment candidates
and still proves both strict orientations for every retained pair. Using equal
model values as propagations belongs to the paper's explicit model-transition
machinery and remains unimplemented.

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

Primo implements all four as poll-only mixed-theory producers. `EqOffsetRows`
indexes the single non-fixed coefficient-one signature and exact fixed offset;
`EqRows` hashes the complete canonical non-fixed polynomial and exact fixed
offset. A bucket retains its first shared basic variable and emits a spanning
edge to later members. Reasons are the deduplicated fixing-bound literals from
both fixed row portions. Primo deliberately polls rather than maintaining the
indexes during every pivot, so this is the paper's logical rule and indexed
complexity but not its eager pivot-hook scheduling.

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

**Implemented on the LRA side and consumed by mixed QF_UFLRA:**

| method | what it is |
|---|---|
| `LraTableau::freedom_interval(nonbasic)` | `[Lⱼ, Uⱼ]`, `nullopt` endpoint = unbounded |
| `LraTableau::diversify_shared_model(shared, forbidden)` | the greedy sweep, returning the complete scratch assignment, post-mutation classes, updates, class counts, and forbidden-equality violations before/after |

Three properties govern the integration:

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
- **Assigned disequalities are objectives, not entailment tests.** An accepted
  update may separate a currently violated forbidden pair but may not
  reintroduce a forbidden equality that the scratch model already satisfies;
  it must also preserve the paper's non-decreasing class-count obligation.
  Any forbidden pair left equal requires targeted strict separation by the
  coordinator.

`solve_qf_uf_lra()` now calls `diversify_shared_model()` once for each feasible
mixed round when either `--qf-uflra-model-reconciliation` or
`--model-equality-branching` is enabled and mutation has not been disabled.
The coordinator projects the returned variable classes back to shared
`TermId`s. The post-mutation classes feed `TheoryCombinationManager`'s
round-scoped shared model, the exact convex scan's necessary-condition filter,
and optional model-equality branch candidates. It computes the pre-mutation
pair count from class sizes and materializes only post-mutation pairs.

The default `--model-equality-mutation-scope=all` passes every mapped shared
Real variable to the sweep. The experimental `care` scope passes only current
EUF care-pair endpoints. Because movement of a scoped tableau variable can
also move or collide with values outside that subset, the mixed coordinator
reconstructs the full shared partition from the returned scratch assignment.
It rejects the mutation if that complete partition has more candidate pairs
than the original one. This scheduling specialization does not change the LRA
tableau implementation or the pure QF_LRA path.

This replaces the former `lra_model_equality_can_be_mutated` proxy, which built
and solved two fresh LRA instances per coincident pair without updating a
model. Failure of the greedy sweep to separate a pair is **not** an entailment
proof. Such a pair remains a model-equality candidate; only
`entailed_equalities()` or the exact two-orientation convex scan may publish it
as a theory consequence with an explanation.

## Two implemented equality-enabling LRA mechanisms

This is the immediately actionable part, and it needs deciding on our side
rather than theirs.

**The global `--lra-fixed-elimination` default (item H7) is off.** The mixed
QF_UFLRA path now has a separately measured scoped default, described below.
The paper, §5.1:
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

**Neither the global fixed-elimination default nor row propagation should be
flipped on this reasoning alone.** The mixed fixed-elimination exception below
was measured with equality propagation before being enabled. The later
1,284-file mixed row-propagation A/B kept the same solved count but exchanged
one solve for one loss, so row propagation remains off by default.

(fixed-elimination-for-no)=
### Should `--lra-fixed-elimination` be on by default for Nelson-Oppen?

Asked directly, and the evidence assembled 2026-08-07 says **yes, scoped to the
mixed path**. That scoped default is now implemented.

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

**Measured decision.** On the committed 45-file QF_UFLRA opportunity sample at
60 seconds per row, the exact promotion run (SLURM job 121509) preserved 42/45
solved with zero wrong answers and reduced PAR2 from 687.11s to 596.81s. It
increased opportunistic candidates from 131 to 1,000, increased `EqRow`
detections from 233 to 2,163, reduced exact fallback pair probes from 464,994
to 295,539, reduced SAT decisions from 33,230 to 29,187, and reduced SAT
propagations from 250,721 to 208,421. The candidate-final profile showed the
same direction: PAR2 423.28s to 403.66s and exact fallback pairs 94,763 to
54,147.

`CheckSatOptions::qf_uflra_fixed_elimination` is therefore on by default only
inside `solve_qf_uf_lra`. `--no-qf-uflra-fixed-elimination` preserves the
measured ablation. The existing `--lra-fixed-elimination` remains a global
opt-in and the pure QF_LRA default remains off. Completeness still comes from
the exact unresolved-pair fallback; fixed elimination only exposes the cheaper
`EqRow` consequence earlier.

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

## Exact fallback probe-reuse checklist

Before this change, the complete fallback constructed a new
`PreparedLraTheory` for each strict orientation of each unresolved shared pair.
That repeated atom normalization, structural row construction, and bound replay
even though only one temporary strict bound changed. The replacement is based on
Dutertre and de Moura, *Integrating Simplex with DPLL(T)*, technical report
SRI-CSL-06-01, Sections 3.1, 3.2.3, and 3.2.4: equations are prepared before
search, assertions tighten variable bounds, and a checkpoint restores those
bounds without reconstructing the equation system.

**Implemented subset.** This is probe reuse inside Primo's existing
complete convex fallback, not the model-guided paper's transition system. For
one candidate pair, copy `PreparedLraTheory::base_tableau`, add one unbounded
difference slack row `s = lhs - rhs` before any bound assertion, replay the
current arithmetic assignment once, and reach a feasible tableau. Then test
`s < 0` and `s > 0` as two checkpointed strict bounds on that same tableau. If
both extensions conflict, `lhs = rhs` is entailed.

**Ownership and data structures.** `PreparedLraTheory` continues to own the
structural tableau and the `TermId` to `LraVarId` map. A check-local probe
context owns its tableau copy and difference slack. `LraTableau` remains
unchanged; the mixed SMT coordinator owns pair enumeration, probe budgeting,
reason combination, and statistics.

**Invariants and proof obligations.** The difference row is added before the
first probe-context `push()`, while the endpoint variables still have the
prepared base basis. Replaying the already checked assignment must be
consistent. Each orientation uses a fresh checkpoint and is popped before the
next orientation. The synthetic probe literal is `Lit{0}` and must be removed
from the returned conflict; all remaining literals are the original blocking
reasons. An equality is published only after both strict orientations return
conflicts. A missing endpoint mapping uses the old reconstruction path rather
than weakening completeness.

**Complexity expectation.** The change removes repeated normalization and
structural preparation and shares setup between two orientations of an
entailed pair. It does not remove the quadratic shared-pair enumeration, the
per-pair base-tableau copy and assignment replay, or the simplex work required
by a genuinely new strict constraint. Counters must therefore separate reused
pair contexts, reused orientation probes, and mapping fallbacks. Acceptance
requires fewer `PreparedLraTheory` constructions and lower wall time on the
profiled MathSAT shape; unchanged answers alone are insufficient.

**Tests derived from the design.** The focused mixed-theory tests pin an entailed
equality that requires both orientations and a non-entailed one-sided pair. They
also require that the normal shared-term path uses reusable contexts without a
mapping fallback or a context-build conflict. The old reconstruction oracle is
retained as the defensive missing-map fallback. Differential QF_UFLRA fuzzing is
required before benchmark evaluation.

**Measured acceptance.** Commit `0b6aa62` passed the 10 tracked mixed seeds and
3,000 generated exact-mode cases against Z3 on SLURM job 121882. On the same
45-file, 60-second sample as the fixed-elimination promotion run, job 121886
preserved all row answers, 42/45 solved, and the same three timeouts. Default
PAR2 fell from 596.809750 to 503.986808; excluding the unchanged timeout
penalties, solved-row time fell from 236.809750 seconds to 143.986808 seconds.
The candidate-final profile fell from 403.662678 to 386.682985 PAR2 and from
43.662678 to 26.682985 solved-row time. In both profiles 41 of 42 solved rows
became faster. Default SAT decisions, SAT propagations, and exact fallback pairs
remained 29,187, 208,421, and 295,539, respectively, isolating the measured gain
from search changes. All 264,934 completed default pair contexts used the reuse
path with zero mapping fallbacks and zero context conflicts. This accepts the
constant-factor repair; it does not close the remaining Z3 gap or remove the
pair cross product.

## Model-value filtering of the exact fallback

The next exact-scan optimization uses de Moura and Bjørner, *Model-based Theory
Combination*, Sections 4 and 5. Section 4 restricts model-guided equality case
splits to shared terms equal in a current component model. Section 5 observes
that a Simplex model is the tableau assignment `beta` and builds its equal-value
classes by hashing values in expected linear time.

**Implemented subset.** Primo uses a current feasible LRA model only as a
necessary-condition filter for its existing complete convex entailment scan. In
the two opt-in model-guided profiles, that model is first diversified by the
Section 5.2 scratch sweep. If the current LRA constraints entail `lhs = rhs`,
then `lhs` and `rhs` have the same value in every feasible LRA model, including
the diversified model. Pairs with different diversified values therefore
cannot be LRA-entailed and need no strict-separation probe. Every retained pair
still has to make both `lhs < rhs` and `rhs < lhs` conflict before Primo
publishes an equality. This is not `M-Propagate`: accidental model equality
remains a candidate, never a theory consequence.

**Ownership and data structures.** The mixed SMT coordinator maps shared Real
`TermId`s through `PreparedLraTheory::variables_by_term`, asks the already
checked `LraTableau` for `model_equal_classes()` or
`diversify_shared_model()`, and expands only the selected classes into canonical
`TermPairKey`s. It computes the partition once per convex round and reuses it
for shared-model reconciliation, optional model-equality branching, the normal
exact fallback, and later model-reconstruction fallback. A shared term missing
from the prepared map is paired conservatively with every other shared term;
missing metadata may cost time but cannot remove an entailment probe.

**Invariants and proof obligations.** The tableau must already have accepted
the complete current arithmetic assignment, including true shared equalities.
The full `DeltaRational` value, including its infinitesimal component, defines a
model class. Candidate-final care filtering may intersect this set but the
default complete path may not remove any model-equal pair. The strict-probe
oracle and its reason construction are unchanged. The candidate partition is
recomputed after every SAT re-entry because the accepted assignment and model
may have changed.

**Complexity expectation.** Partition construction is expected linear in the
number of mapped shared variables. Pair expansion is proportional to the sum of
the squares of model-class sizes instead of the square of all shared terms. The
worst case remains quadratic when all shared values coincide; Section 5.2's
model diversification is still needed for that case. Each retained pair still
copies and replays one tableau probe context, so this step reduces the number of
copies rather than changing their individual cost.

**Tests and counters.** A focused exact-mode regression fixes three shared Real
terms to different values and requires all three cross-class pairs to be pruned
without a probe. Existing paired-bound and one-sided-bound regressions require,
respectively, that an entailed model-equal pair is proved and that an accidental
model-equal pair is probed but not propagated. The
`:no.interface.lra-model-filter-*` counters expose builds, full pairs,
candidates, pruned pairs, unmapped terms, and conservative pairs. Differential
QF_UFLRA fuzzing remains mandatory before benchmark evaluation.

**Measured acceptance.** Commit `84d5e8a` passed all 10 tracked QF_UFLRA seeds
and 3,000 fresh exact-mode generated cases against Z3 on SLURM job 121888, with
zero disagreements, timeouts, crashes, unsupported cases, or bug artifacts. On
the established 45-file, 60-second sample, job 121892 preserved every row and
42/45 solved with zero wrong answers. Relative to probe-reuse job 121886, the
default profile reduced PAR2 from 503.986808 to 416.435444 and solved-row time
from 143.986808 to 56.435444 seconds. Exact fallback pairs fell from 295,539 to
173,801, reusable contexts from 264,934 to 167,432, and replayed assignment
bounds from 82,998,708 to 65,583,791, while SAT decisions and propagations stayed
at 29,187 and 208,421. The 25 MathSAT rows fell from 91.850499 to 8.549319
seconds. The candidate-final profile also preserved all rows and improved PAR2
from 386.682985 to 374.066642. Both profiles reported zero unmapped terms,
conservative pairs, probe mapping fallbacks, or probe-context conflicts.

The subsequent full `sandbox/non-incremental/QF_UFLRA` sanity run used all
1,284 files at five seconds per row in SLURM jobs 121894 and 121902. Default
solved 1,123 files, timed out on 160, and reported one unsupported negated Real
equality; candidate-final solved 1,153, timed out on 130, and reported the same
unsupported row. Their PAR2 totals were 2043.045990 and 1486.812553. Neither
profile had an error, crash, wrong declared-status answer, or disagreement on
any jointly solved row. The unsupported CPAchecker row is nevertheless valid
QF_UFLRA: raw finite-domain AMO clauses reintroduced Real equality after the
lowering boundary. The full-corpus result is historical performance data and
requires revalidation after that preprocessing-order fix; it is not accepted
correctness evidence for the filter.

Because job 121888 exercised the exact/default option rather than
candidate-final, the option-specific prerequisite was closed separately before
finalizing this evidence. SLURM job 121910 passed the same 10 tracked seeds and
3,000 fresh mixed generated cases with
`--qf-uflra-candidate-final-scan`: 1,852 SAT and 1,148 UNSAT, all matching Z3,
with no timeout, crash, unsupported result, disagreement, or bug artifact. That
generator did not contain finite-Real-domain choices and did not assert
preprocessing activation, so it does not close the revalidation requirement.

## Remaining order of work

1. **Evaluate row-bound propagation in the mixed path.** Its relevant measure
   is newly exposed fixedness and fewer exact fallback probes, not pure-QF_LRA
   solved count alone.
2. **Complete the shared model transition.** The four Section 5.1 rules and
   freedom-interval mutation now exist, but using model classes to replace the
   exact scan still requires arrangement-aware negative equalities and a real
   `M-Propagate` witness transition. Model equality must remain distinct from
   entailed propagation.

## Combination contract

The current contract is a stream of `(lhs, rhs, blocking reasons)` entailments.
`TheoryCombinationManager` owns their canonical shared-pair identity, current
SAT assignment, and lazily registered equality literal; the LRA tableau owns
the arithmetic proof state. This is enough for convex EUF+LRA because the exact
fallback makes the stream complete at quiescence. It is not an interface for
non-convex arithmetic, integer arithmetic, or the paper's generic mutable model
state.
