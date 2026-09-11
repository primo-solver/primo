# Open leads register

A single place to find every live idea for improving Primo's solver
performance, what evidence supports it, what it would cost, and -- stated
before the work starts -- what result would kill it.

It exists because leads were accumulating scattered across
[](qf-lra-heuristics.md), [](papers/lra-related-work.md),
[](tropical-matrix-structure.md), [](preprocessing.md) and individual paper
notes, which made it easy to re-propose something already rejected and easy to
lose something cheap. This register is the index; the detail stays in the
notes it points to.

The dated profiles and rankings are hypotheses for their recorded revisions,
not a fresh profile of the current branch. Consult [](current-status.md) before
reopening a lead: QF_UFLIA now has the N-component driver and an accepted linear
ordinary-decision selector, while QF_UFLRA remains dedicated. Old
`check_sat.cpp` engine references now map to `solver_engines.cpp`; frontend and
dispatch have separate modules. Reprofile a still-relevant bottleneck before
implementing from these historical measurements.

## How to use it

- **Before proposing work**, check the [rejected](#rejected) section. Several
  entries there look obviously right and are measurably wrong.
- **Before implementing**, check that the lead's precondition still holds --
  entries record what was true when measured, and the solver moves.
- **Every entry states a falsifier in advance.** That is the project's habit
  (see H11 and D1), and it is what lets a negative result close a lead instead
  of inviting a retune.
- **Ranking is expected value per unit of work**, not expected value. A cheap
  partial win outranks an expensive whole one; [](qf-lra-heuristics.md)
  section 6.1a is the argument.
- **Record what a configuration solves *uniquely*, not only how many it
  solves.** See [](#complementarity) -- a lead that loses on the promotion bar
  can still earn a place in a portfolio, and that information is destroyed by
  reporting solved counts and PAR2 alone.

## At a glance

| # | lead | expected size | cost | status |
|---|---|---|---|---|
| [1](#lead-pure-filter-lra) | Extend `--pure-theory-filter` to LRA atoms | zero as a wiring fix; the filter reaches only the once-called final check | large if done properly | **downgraded**; merge into lead 5 |
| [2](#lead-flag-first) | Flag-first decision ordering | unknown | blocked | **attempted; belongs to the theory-combination layer** |
| [2b](#lead-two-lra-paths) | Dispatch trigger: propositional variables route pure QF_LRA to the NO path | historical 300s regression; narrow dispatch now keeps the pure path | done | **fixed**; see [](nelson-oppen-qf-lra-regression.md) |
| [3](#lead-leaving-rule) | MathSAT's real leaving rule: least-violated, short-row tie-break | 167 vs 174 on the bar; beats the *other* existing policy by 5 | done | **implemented, opt-in; did not clear its bar** |
| [3b](#lead-occurs-in) | `occurs_in_` maintenance: the representation is half a sparse matrix | tropical 1/10 -> **2/10** at 1200s, gained 1 / lost 0; bar-neutral on the stratified sample | **large** (representation change) | **implemented, default**; 2 earlier prototypes rejected |
| [4](#lead-aep) | Adaptive Early Pruning | ~20% total time in its source | small-medium | open |
| [5](#lead-relevancy) | Relevancy propagation | unknown, principled | **large** | open, gated on 1 |
| [6](#lead-justification) | Justification / goal-directed decision heuristics | unknown | medium-large | open, no paper note yet |
| [7](#lead-row-representation) | Compare row representation against sorted arrays of pairs | already the endorsed design; 0.00% map cost in the profile | done | **closed: already implemented** |
| [8](#lead-dl6) | DL6: decide precision statically before search | unknown | small | blocked on a DL solver |
| [9](#lead-monotone) | Monotone (one-sided) variable elimination | neutral on the promotion bar | done | **implemented, opt-in; did not clear its bar** |
| [10](#lead-z3-cheap-shared-equality) | Current-model-filtered unit-row equality recognition | unknown; removes exact-offset arithmetic and hashing from mixed equality production | medium | **open; QF_UFLRA only** |
| [11](#lead-unconstrained-terms) | Unconstrained-term substitution (Brummayer/Bruttomesso) | 131/4.76M reals (QF_LRA), 1/217K (QF_UFLRA) -- negligible on both local corpora | small-medium | **open; not implemented, evidence says don't -- see falsifier** |
| [12](#lead-qflia) | QF_LIA preprocessing disabled; conflict clauses do not generalize | +339 answered on the full division from 12d and 12f so far | done/partial | **12d and 12f implemented; 12b, 12c, 12e open** |
| [13](#lead-qflira) | QF_LIRA -- frontend gap, not a solver gap | search core already runs mixed tableaus | medium (frontend) | **open; run the falsifier first** |
| [14](#lead-monotone-int) | Monotone elimination for `Int`, not just `Real` | unmeasured -- the QF_LIA zero is definitional | small, gated on 9 | **open; measure candidate density first** |
| [15](#lead-boolean-bridge-exclusion) | Delete the `bridge-atoms` Boolean-domain encoding | superseded by registration: 0 disagreements on both fragments, 3% faster on QF_UF | small (deletion) | **open; the A/B is done and came out flat** |
| [16](#lead-ite-preprocessing) | If-then-else preprocessing: CITE simplification + ITE compression + ITE reconstruction | **+1693 QF_LIA in the source's own ablation**; 3449 files of Primo's corpus are in the affected families | medium-large | **open; largest measured number in any paper Primo has read. Unblocked 2026-09-04: ITE is a term kind now, so the ordering constraint this lead called its main design question is gone** |
| [17](#lead-equality-basis) | Complete implied-equality basis (Bromberger thesis Ch. 5) | replaces per-pair two-orientation probing with one strict-system check + normal-form hashing | medium-large | **open; also the gate for 12c and 19** |
| [18](#lead-hard-backtrack) | `HardBacktrack`: revert bounds only, recompute the assignment | +123 QF_LIA, **+117 on `convert` alone** in the source's ablation | **small (a deletion)** | **open; cheapest entry on this list** |
| [19](#lead-bounding-transformations) | Double-Bounded reduction + Mixed-Echelon-Hermite: make branch-and-bound terminate | +169 QF_LIA, 162 of them in `20180326-Bromberger` (806 files here) | large | open, gated on 17 |
| [20](#lead-decision-recommendation) | Evaluate `--lra-model-phase` on QF_LIA and consider promoting it | +129 QF_LIA, **116 of them `convert`**; the source's number is for the mechanism Primo already has | **none -- an A/B of an existing flag** | **open; cheapest lead in this register** |
| [21](#lead-divisibility) | Divisibility-aware `div`/`mod`: modular normalization, then a first-class `k \| t` atom | **not a corpus lead** -- 0 of 600 QF_LIA benchmarks use these operators; 2 of 400 generated cases hit the branch-and-bound node limit where Z3 decides | small (piece 1); medium-large (piece 2) | **open; read the falsifier before sizing it** |
| [22](#lead-incremental-uf-scans) | Incremental UF: avoid rescanning old atoms and initializing full-manager scratch | preparation/reset is 41% of one long session; only part is addressable here | small-medium | **open; not implemented** |
| [23](#lead-incremental-uf-distinct) | Compact large `distinct` handling | 554,403 pairwise constraints in the worst completed ratio | large | **open; design required** |
| [24](#lead-incremental-uf-equations) | Incrementally sound equation elimination | search dominates `firewire`; Z3 eliminates 22,931 variables in its first query | large | **open; substitution/restoration deferred** |
| [25](#lead-incremental-uf-structure) | Share immutable EUF structure across checks | 413 copies of each prepared structure in one session | medium-large | **open; not implemented** |
| -- | D2 floating-point-guided basis selection | ~14% in the literature | medium | open, see [](qf-lra-heuristics.md) |
| -- | D4 revised simplex, LU-factored basis | unknown | largest | open, see [](qf-lra-heuristics.md) |
| -- | H4 batched bound assertion; H6 DL fast path; H11 fill-in-aware preprocessing elimination | -- | -- | design only, see [](qf-lra-heuristics.md) |
| [R1](#lead-recursion-audit) | Audit input-sized recursion across the repository | robustness; prevents depth-dependent crashes | medium-large | **open; deliberately deferred** |
| [12a](#lead-qflia-preprocessing-safe) | QF_LIA: enable bare-form solved-constant substitution | no measurable effect on the 2 motivating instances; real but narrow win elsewhere | small | **done: substitution shipped default-on; atom canonicalization attempted and reverted, unsafe as written** |
| [12b](#lead-qflia-preprocessing-gauss) | QF_LIA: partial (unit-pivot) Gaussian elimination | needs a population census first | medium | open, gated on a census |
| [12c](#lead-qflia-dio-hnf-cube) | QF_LIA: Diophantine/HNF/cube search-time reasoning | z3 closes 96% of calypto's conflicts via Dio, not branching | large | open; paper note now exists, split into patching (done), cubes (needs the octagon twist first), Dio/HNF (large) |
| [12d](#lead-qflia-conflict-generalization) | QF_LIA: generalizing (Farkas-style) arithmetic conflict clauses | lemmas 27-55x larger than the certificate the tableau already returned | small (est. large, wrongly) | **done: shipped default-on; full corpus +19 solves, PAR2 43.12 -> 42.94, 0 wrong** |
| [12e](#lead-qflia-extended-gcd) | QF_LIA: extended GCD test for bounded-but-not-fixed rows | closes the divergence `gcd_uncatchable_divergent_system_hits_the_node_limit` pins | small | open, gated on a census |
| [12f](#lead-qflia-patching) | QF_LIA: patching (paper Sect. 4.1) | entire deficit vs cvc5 is on sat; this is the layer that finds models | small | **done: full corpus +300 solves (356 gained / 56 lost), 331 of them sat, PAR2 42.91 -> 40.20, 0 wrong; cvc5 gap -535 -> -212** |

(lead-pure-filter-lra)=
## 1. Extend `--pure-theory-filter` to LRA atoms

**Status: downgraded 2026-08-05 after reading the code. The original framing
of this entry was wrong in two ways, both recorded below.**

**What the mechanism actually is.** `should_filter_assignment` omits a theory
literal from the theory assignment when the atom occurs with only one polarity
*and the SAT solver assigned it the opposite way* -- such an assignment cannot
be what satisfies any clause, so the theory need not check its consistency
([](euf.md), from Sebastiani's survey §6.10). It omits a constraint from a
check rather than adding one, which is why it is sound where the pure-literal
rule is not ([](preprocessing.md)'s [](#pure-literal-trap)).

**Correction 1: it is not "QF_UF only".** The rule is generic -- it needs only
`occurs_positive`/`occurs_negative` -- and the LRA side already *consumes* it:
five sites skip an atom on `!prepared_atom.is_arithmetic || atom.filtered`.
What the QF_LRA path does is call the two-argument
`assigned_atoms_from_backend` overload, which hardcodes
`PureTheoryFilterMode::Off`, so `filtered` is never set. Passing
`options.pure_theory_filter` at the two call sites in `solve_qf_lra_impl` (and
one more for the LRA half of QF_UFLRA) is a two-line change. Note this is
deliberate rather than a bug: [](euf.md) documents the implemented subset as
QF_UF atoms.

**Correction 2, twice over -- the first downgrade was itself measured
wrong.** This entry cited "2,892 of 3,846 theory atoms pure-positive (75%)" as
the size of the prize, on the reasoning that a pure-positive atom is filtered
only when assigned false and most would be assigned true. A first experiment
appeared to confirm that. **That experiment was inert**: it patched
`solve_qf_lra_impl`, and these files do not use it -- `:solver.qf-uflra` is 1
and `:solver.qf-lra` is 0, so the live path is `solve_qf_uf_lra`. Identical
counters proved nothing.

Redone against the path that actually runs, the picture reverses and then
lands somewhere more useful:

| measurement on `constraint-199552` | value |
|---|---:|
| LRA atoms seen at the check | 2,892 |
| **filtered with `--pure-theory-filter=auto`** | **1,177 (41%)** |
| filtered with `=off` (control) | 0 |
| `:lra.pivots`, either way | 7,679 |
| `:lra.pivot-term-ops`, either way | 128,402,240 |

So the reasoning was wrong in the other direction too: 41% of the atoms *are*
assigned against their polarity and *are* filtered. The filter fires hard --
and changes nothing at all.

**The actual reason, and it is the useful finding.** The counters say
`:lra.theory-checks 1` against `:lra.online-check-calls 6528`. Wiring the
filter at `assigned_atoms_from_backend` reaches only the **final check, which
runs once**. Every one of the 7,679 pivots happens inside the online
propagator, which never consults `filtered`. The cheap version of this lead is
therefore worthless by construction, not by accident of the benchmark.

**What the lead becomes.** To matter, the filter has to reach the online
propagator's assertion path. That is a materially larger change than two
lines, and it sits much closer to [](#lead-relevancy) than to a wiring fix --
both are then about *not asserting* atoms during search rather than about
polarity bookkeeping at the end.

**Consequence for lead 5.** [](#lead-relevancy) was gated on this one as its
"cheap static approximation". That gating still holds, but the reasoning
inverts: this lead failing for *this* reason (nothing to filter statically)
does not say dynamic relevancy is worthless, because relevancy filters on
whether an atom is *reachable* under the current assignment, not on polarity.
The two mechanisms would fire on different atoms.

(lead-flag-first)=
## 2. Flag-first decision ordering

**Status: attempted 2026-08-05 and blocked. The stated mechanism does not
exist, and the real one is owned by a different layer.** Kept open in
principle; not actionable from the LRA propagator.

**The claim.** On guarded families the Boolean layer is self-contained and
determines which arithmetic is relevant, so settling it first should stop the
search doing tableau work a better flag assignment would have made
unnecessary. `constraint-199552` shows 15,766 decisions against only 93
conflicts -- the search is walking, not fighting.

**Why "invert the `cb_decide()` filter" is not implementable.** Measured, not
inferred:

| fact | evidence |
|---|---|
| These benchmarks do not run `solve_qf_lra` | `:solver.qf-uflra` 1, `:solver.qf-lra` 0 -- on tropical *and* uart |
| `cb_decide`'s atom list is 100% arithmetic | probe: `lra_atoms=2892 non_arithmetic=0` (tropical), `1363 / 0` (uart) |
| so inverting its `is_arithmetic` filter yields an **empty** candidate set | it would disable theory decisions, not reorder them |
| the flags are elsewhere | they are the 954 `euf_atoms` -- Bool-sorted `Constant` terms, split off by `is_euf_theory_atom` |
| the dispatcher does not help | `MixedTheoryPropagationDispatcher::cb_decide` just delegates to the LRA one, and is only built when `nelson_oppen_propagation == Online`, which is not the default |

**The hard blocker.** Passing `euf_atoms` into `OnlineLraPropagator` and
returning one of their literals from `cb_decide` aborts immediately:

```text
cadical: fatal error: invalid API usage of 'ask_decision':
external decisions are only allowed over observed variables.
```

A propagator may only decide variables it *observes*. Making the Boolean flags
observed by the LRA propagator changes which callbacks CaDiCaL delivers to it
and how much it is notified about -- that is a theory-combination decision,
not an arithmetic one.

**Design ownership, and why this was reverted rather than finished.** Deciding
*which theory's atoms get decided first* belongs to the layer that owns which
variables each theory observes -- the theory-combination/Nelson-Oppen layer,
where `MixedTheoryPropagationDispatcher` already sits and already sees both
atom sets. Bolting it into `OnlineLraPropagator` would have the LRA propagator
reach into EUF's atom list, and would collide with in-flight Nelson-Oppen work
in another branch. The prototype was reverted; nothing of it is committed.

**What would make this actionable.** A decision hook on the combination layer
that observes both theories' atoms and can order across them. That is a
natural companion to whatever the Nelson-Oppen work settles on, and should be
picked up there rather than re-attempted here.

(lead-two-lra-paths)=
## 2b. Dispatch is a nondeterminism trigger (not a performance bug)

Current disposition: the later full-division investigation established a real
regression and fixed it at `d52799c`; [](nelson-oppen-qf-lra-regression.md)
records the outcome. The earlier downgrade and measurements below explain the
investigation's progression, not the current open status.

**Status: measured and downgraded. The design is deliberate -- a dedicated
LRA-only solver without Nelson-Oppen is kept -- but the *trigger* for choosing
it is not, and that is worth knowing about.**

### What happens

`has_euf_predicate` is set by
`(kind == Application || kind == Constant) && sort == Bool`, so a bare Boolean
**Constant** -- an ordinary propositional variable -- counts as an EUF
predicate. The dispatch at `(has_equality || has_euf_predicate) &&
has_arithmetic` then sends the formula to `solve_qf_uf_lra`.

`constraint-199552` declares no sorts and no uninterpreted functions
whatsoever: it is pure QF_LRA plus propositional variables, and it takes the
mixed path anyway. **130 of 237 stratified-sample files are in that
category.** Nelson-Oppen correctly no-ops there for lack of shared real terms,
so nothing unsound or expensive happens -- but which of two solvers runs is
decided by something semantically unrelated to whether theory combination is
needed.

### Upgraded again 2026-08-07: on the full division it *is* a performance bug

```{note}
The full evidence, both mechanisms, the still-unexplained LassoRanker half and
**the argument for what order to fix them in** are in
[](nelson-oppen-qf-lra-regression.md). That note is the one to read before
touching the dispatch classifier; this entry keeps only the lead-level summary.
```


**The "not a performance bug" verdict below was measured on the 237-file
stratified sample at a 15s cap. It does not survive the full division at
300s, and the section after it should be read with that limit in mind.**

The full QF_LRA division (1,753 files, 300s) came back **1,588 solved against
the 1,631 recorded at `b6b8d55`**, and bisecting one of the lost files landed
exactly here. `2019-ezsmt/travellingSalesperson/tsp_rand_70_300_1155482584_7`
is pure QF_LRA plus propositional variables -- no sorts, no uninterpreted
functions:

| commit | `:solver.qf-lra` | `:solver.qf-uflra` | result |
|---|---:|---:|---|
| `4d4a185` (parent) | **1** | -- | **sat, 0.93s**, 11,735 pivots |
| `ffaf9eb` | 0 | **1** | **timeout**, deterministic over repeated runs |

`ffaf9eb` ("Add separable QF_UFLRA final checks", 2026-07-30) is the commit
that *introduced* `contains_euf_predicate_atom` -- the classifier described
below. So this entry documents the consequence of a specific commit, not a
long-standing quirk, and that commit costs at least this file outright.

Two things follow. First, **the cost is not symmetric at long timeouts.** The
even 2-2 timeout split below was a 15s measurement; a 0.93s-to-timeout flip
does not show up at 15s because the file is fast on both sides of nothing.
Second, **this is a main-branch regression, not a branch-local one** -- main
at `0c0843d` reproduces the timeout (1.09M pivots) in the same regime as this
branch's HEAD (1.14M), while `b6b8d55` solves in 0.20s.

**Why the mixed path is slower here, since "NO should no-op" is correct.**
It does no-op -- `:no.interface.conflicts 0`, every purification counter zero.
The cost is that the mixed path also starts the **incremental EUF engine** and
hands it the propositional variables:

| counter | pure path (`4d4a185`) | mixed path (`ffaf9eb`) |
|---|---:|---:|
| `:euf.checks` | 0 | 5,364 |
| `:euf.incremental.merges` | 0 | **39,436,128** |
| `:euf.incremental.terms` | 0 | 39,446,856 |
| `:no.mixed.euf-atoms` | -- | **7,352** |
| `:sat.clauses` | 26,117 | 31,481 |

The file declares **7,352 Bool-sorted nullary `declare-fun`s, zero sorts and
zero functions with arguments** -- and `:no.mixed.euf-atoms` is exactly 7,352.
Every propositional variable is being routed to congruence closure, which
performs 39.4M merges to decide nothing the SAT solver does not already own.
So the defect is not "NO costs something"; it is that a formula with no EUF
content is given an EUF solver.

What this does **not** explain: LassoRanker, 40 of the 55 lost files, which
reports `:solver.qf-lra 1` -- the pure path -- on both sides. See
[](#lead-2b-lassoranker).

(lead-2b-corpus)=
### Corpus-level: main lost 59 files, and this branch recovers 16 of them

Settled 2026-08-07 by running the full division on **main itself** under
conditions matched to the branch run (jobs 120072 and 120122: same 40 shards,
same 300s, same `--cpus-per-task=3`, same three nodes with the same split).

| | solved / 1753 | wrong | PAR2 |
|---|---:|---:|---:|
| `b6b8d55` (2026-08-03) | **1631** | 0 | 95,091 |
| main `0c0843d` | **1572** | 0 | 136,330 |
| branch HEAD | **1588** | 0 | 127,101 |

- **main vs `b6b8d55`: gained 2, lost 61** -- LassoRanker 46, 2019-ezsmt 12,
  miplib 2, tropical-matrix 1. That is the regression bisected above.
- **branch vs main: gained 16, lost 0**, PAR2 -9,229, median paired 0.94.
  All 16 gains are LassoRanker, the family main regressed hardest on.

The lesson about how this was nearly misread is worth keeping: the first
comparison was branch-vs-`b6b8d55`, a *pre-regression* commit rather than the
branch's merge base, which made a main-side loss look like a branch-side one.
**Compare against the merge base, not against the last number someone wrote
down.**

(lead-2b-lassoranker)=
### The other half: LassoRanker, same path and still 10x slower

Separate mechanism, same origin. On
`dropbuf.t2.c_Iteration3_Loop_4-pieceTemplate` (3.4MB):

| | answer | dispatch | pivots | decisions |
|---|---|---|---:|---:|
| `b6b8d55` | **sat, 13.0s** | `:solver.qf-lra 1` | 10,217 | 256,688 |
| main `0c0843d` | timeout 150s | `:solver.qf-lra 1` | 3,793 | 190,089 |
| branch HEAD | timeout 150s | `:solver.qf-lra 1` | 4,183 | 198,695 |

The dispatch never flips, so [](#lead-two-lra-paths)'s mechanism is not at
work. Preprocessing is not it either (`--preprocess-only` 1.35s vs 1.42s).
And **the time is not in the tableau**: at a 45s cap main does 1,472 pivots
and 1.66M pivot-term-ops against b6b8d55's 10,217 and 76.1M in 13s. The
online check-call rate falls from 13,976/s to 2,143/s *while the LRA work per
call shrinks*, so something outside the simplex is being paid per call.

Bisected (first-parent, so only mainline states are tested) to **`84336a2`
"Merge remote-tracking branch 'origin/main'"** -- the merge that brought
`theory_combination.{cpp,hpp}` and the Nelson-Oppen closure work into main,
46 files and 14,392 insertions. Its first parent `38be7fd` is good.

**Open: the mechanism.** The next step is a profile of main on this instance
answering "what runs per online check call on the pure QF_LRA path after the
merge that did not before". `perf` is unavailable in the current sandbox, so
use a `-pg` build or profile on the cluster.

**Bisection hazard worth reusing.** Two earlier attempts at this bisect were
invalid and both converged on a *docs-only* commit. Main contains merge
commits from the Nelson-Oppen branch, and that branch's own history predates
QF_LRA support, so those commits answer `unsupported logic: QF_LRA`; the
script marked them **bad** rather than skipping. Any bisect across this merge
needs `--first-parent` plus an explicit skip on unsupported-logic and
no-answer outcomes.

### It is not a performance bug at 15s -- the original measurement

An earlier version of this entry called the divergence a behaviour bug, on the
strength of a single instance where the mixed path was 40x faster. Measured
across all 130 files where the dispatch actually differs (same binary, one
classifier line changed, 15s cap):

| verdict | files |
|---|---:|
| comparable | **80 (62%)** |
| both timeout | 21 |
| pure path faster (1.5-7.6x) | 19 |
| mixed path faster (2.2-55.2x) | 6 |
| pure solves, mixed times out | **2** |
| mixed solves, pure times out | **2** |

**Timeout wins are exactly even, and where the paths differ the pure one is
faster more often.** Neither dominates. Large swings in both directions are
what a search perturbation does in CDCL(T) -- the 40x on tropical is one draw
from a two-tailed distribution, not evidence of a defect. Corroborating this,
`rand_80_340_1159656267_18.lp` and `0051-labyrinth-12-0` each appear as large
swings here *and* in the lead 3 comparison, on opposite sides: instances that
swing hard swing hard under any perturbation.

(dispatch-nondeterminism)=
### What to actually record: this is a nondeterminism trigger

The decision is to **keep** a dedicated LRA-only solver without Nelson-Oppen.
On a single theory the combination layer has nothing to do, and paying for it
by default would be the wrong trade. What should be written down is the
consequence:

**A semantically irrelevant edit to a formula can change which solver runs,
and therefore change runtime by an order of magnitude in either direction.**
Adding a single `(declare-fun p () Bool)` to an otherwise pure QF_LRA problem
flips the dispatch. So:

- **Benchmark results are not comparable across such an edit.** A preprocessing
  change that introduces or removes a Boolean constant can move a file between
  solvers, and the resulting timing difference will look like the
  preprocessing change's effect when it is not. This is a live hazard for
  exactly the kind of A/B this register is full of.
- **Reproduction reports should name the path.** `:solver.qf-lra` and
  `:solver.qf-uflra` distinguish them, and a performance bug report that does
  not say which one ran is under-specified.
- **It interacts with the fuzzers.** Mutation-based fuzzing (opfuzz, typefuzz)
  routinely adds and removes declarations, so a mutant and its seed can take
  different solve paths. That is fine for answer-differential testing, which
  is what those tasks do, but it means mutation timing data is not meaningful.

#### Assertion packaging is another trigger

The default QF_UF `auto` preprocessing selector still reads
`assertions.size()`. The legacy policy distinguishes a single assertion, a compact
multi-assertion batch, and a batch above 10,000 assertions. Those branches
enable different preprocessing subsets and can also select different static
SAT phase hints. Consequently, these two inputs can follow different pipelines:

```text
(assert (and A B))
```

```text
(assert A)
(assert B)
```

They denote the same conjunction. Assertion roots remain useful operational
storage for SMT-LIB scopes, incremental lifetime, source bookkeeping, and
avoiding one enormous synthetic `And`; their number is not a semantic property
of the formula and should not be a preprocessing classifier feature.

This is deterministic for one fixed input, but it is a nondeterminism trigger
for benchmark and mutation comparisons in the same sense as dispatch above: a
semantics-preserving packaging edit changes the algorithm and can cause CDCL
search churn. Until the structural replacement is validated and promoted,
reports comparing rewritten or mutated inputs under the legacy default must record
`:preprocessing.auto.single-assertion`,
`:preprocessing.auto.basic-single`,
`:preprocessing.auto.basic-multi`,
`:preprocessing.auto.strong`, and
`:preprocessing.auto.large-assertion-skip`.

The opt-in `--auto-preprocess-selector=structural` implementation now performs the
repair without concatenating roots or flattening every Boolean DAG. It uses
representation-stable costs of the implicit conjunction: reachable terms,
top-level conjunct and clause shape, and analysis-only finite-domain recognizer
output. It also reports its profile disagreement with the legacy selector. The
implementation and equivalence tests are specified in
[](structural-auto-preprocessing.md).

Promotion remains open. It requires merge-tier QF_UF/QF_UFLRA fuzzing, the
QF_UF selector benchmark, and the QF_LRA no-regression comparison documented in
[](preprocessing.md). The existing assertion-count selectors are empirically
tuned, so changing the default without those comparisons would merely exchange
one source of search churn for another.

### Not to be "fixed" naively

Restricting `has_euf_predicate` to `Application` sends pure QF_LRA formulas to
the pure path, and on `constraint-199552` that is a 4.39s solve turning into a
180s timeout. On the full 130 files it is close to a wash. Either way it is a
coin flip dressed up as a cleanup, and it touches the dispatch that in-flight
Nelson-Oppen work also touches. If the classifier is ever tightened, it should
be for predictability, and it should be measured across the whole set rather
than justified on one instance.

(lead-leaving-rule)=
## 3. MathSAT's actual leaving-variable rule

**Claim.** Primo has never tried the rule its own docs attribute to Griggio.
The primary source (thesis §2.5.3, p. 53) picks the violated basic variable
**closest** to its violated bound, with **short row** as the tie-break. Primo
offers most-violated-first (default) and shortest-row-first (opt-in
`--lra-sparse-leaving`) -- each inverts one half of it.

**Evidence.** The thesis measures its greedy rule at **1.42x median / 2.77x
mean** against Bland, on SMT-LIB QF_LRA. Full context in
[](papers/griggio-mathsat-thesis.md).

**Cost.** Small -- a comparator inside an existing selection loop.

**Caveat, recorded so it is not skipped.** The thesis is Dec 2009 and describes
MathSAT **4**; King (2014) attributes most-violated to MathSAT**5**. The rule
may genuinely have changed, so this is a third design point to test, not a
correction to apply blindly. Implement the rule as stated rather than flipping
one comparator and assuming that reproduces it. Worth varying the Bland
fallback threshold alongside it: MathSAT used the number of input variables,
Primo uses `4 * vars_.size() + 64`.

**Falsifier.** No improvement in solved count or PAR2 on the 237-file
stratified sample against both existing leaving policies.

### Outcome: implemented, did not clear the bar, and corroborated the source

Slurm job 119530, 237 files, 10s, same binary all three ways:

| leaving policy | solved | PAR2 | wrong |
|---|---:|---:|---:|
| shortest-row (current default) | **174** | **1415.97** | 0 |
| least-violated + short-row tie-break (this lead) | 167 | 1543.92 | 0 |
| most-violated, Dantzig-style | 162 | 1653.46 | 0 |

**Complementarity**, the first application of [](#complementarity): against the
default it gained 2 and lost 9; against most-violated it gained 12 and lost 7.
Virtual best of all three is 177, of default plus this rule 176, against the
default's 174.

**And the caveat in that section immediately earns its place.** Both gained
files are boundary churn, not capability: at 120s the default solves
`rand_80_340_1159656267_18.lp` in 14.73s (against 2.99s) and
`0051-labyrinth-12-0` in 10.10s (against 6.60s). So the portfolio case is
weak. The 5x on the first file is a real per-instance speedup, but "solves
what the default cannot" is not what happened.

**The result that does matter.** This rule beats the *other* existing policy
by 5 solved and 110 PAR2, gaining 12 files against it. Griggio's actual rule
really is better than the most-violated rule this project's docs had
attributed to him -- independent support, on our own benchmarks, that the
attribution corrected in [](papers/griggio-mathsat-thesis.md) was wrong.

(lead-occurs-in)=
## 3b. `occurs_in_` removal was a linear scan, and it dominated `pivot`

**Status: resolved 2026-08-06 -- implemented, default, and no longer a linear
scan.** What ships is described in [](arithmetic.md)'s "Cross-linked
`occurs_in_` index" entry; this entry is the investigation log, kept because
the two rejected prototypes and the King design fork are the reusable part.
Read it for *why*, not for *what*.

**Found 2026-08-06 by asking why the removal was a linear scan at all. It was
the largest measured single cost in the arithmetic layer.**

`pivot()` maintains `occurs_in_`: variable -> the list of basic variables
whose rows mention it. It is the first Markowitz factor, so it has to be
right, and `pivot()` is the only place it changes during search.

### When the erase actually happens

There are **two** removal sites, and they are unrelated in both cause and
frequency. Measured on `tropical-matrix/constraint-199552` (7,679 pivots,
128.4M merged terms):

| site | when it runs | calls | elements scanned |
|---|---|---:|---:|
| **1. dissolving the leaving row** | once per term of the leaving basic variable's old row, on **every** pivot | 264,081 | 180,921,747 |
| **2. cancellation during the merge** | only when a coefficient sums to **exactly zero** | **12,033,307** | **8,174,083,067** |
| | | | **8,355,004,814 total** |

**Site 1 is structural and unconditional.** When `basic_var` stops being
basic, its row is dissolved and rewritten in terms of the entering variable.
Every variable that appeared in that row therefore no longer has `basic_var`
among the rows mentioning it, so each gets one removal. The count is exactly
`sum over pivots of |row(leaving)|` -- 34 per pivot here. It is bounded by row
length and is *not* the problem.

**Site 2 is arithmetic and data-dependent, and it is the problem.** During the
two-way merge, a variable present in both the affected row and the scaled
entering row has its coefficients added. If they cancel to exactly zero the
variable genuinely leaves that row, so `other_basic` must leave
`occurs_in_[var]`. This fires on **9.4% of all merged terms** -- 12.0M times,
each scanning an occurrence list averaging **679** entries.

So the ratio that matters is not "removals are rare". Site 2 alone performs
**ten times more removals than there are `occurs_in` entries iterated**
(1,260,035), and its scans touch **65x more elements than there are pivot term
ops**. At a generous 4 elements/ns that is roughly **2.0s of pure scanning in
a 4.4s solve**, consistent with gprof putting `pivot` at 74% self time while
`Rational` copies sit at 1.05%.

**Why cancellation is this common here** is legible from the encoding rather
than mysterious: [](tropical-matrix-structure.md) shows the family is a DAG of
definitions over 36 free variables, so rows composed from the same handful of
`f*` variables cancel against each other systematically. Whether 9.4% is
typical of other families is **not established** -- it was measured on one
instance, and that is the first thing a prototype should check.

**The comment that hid this.** Site 2 carries the note "This is the *only*
removal the loop performs", which is true and describes *which* removal it is
-- there is no second removal inside the merge. It was read as "removals are
rare", which the measurement contradicts. Site 1 is outside that loop, which
is why the comment is not wrong, only easy to misread.

### How this was found, and why that matters

Worth recording, because the finding did not come from pursuing a lead. It
came from *closing* one, and four intermediate claims along the way were
wrong.

1. **Lead 7 was opened from a paper number** -- Griggio's thesis measures 1.20x
   for choosing hash tables over sorted arrays of pairs. Checking it closed it
   immediately: Primo already uses the sorted-array design, and item H8 went
   further than the thesis by replacing the sort with a merge.
2. **The profile taken to close it was the actual payload.** `perf` is blocked
   in this sandbox, so it is gprof over a `-pg` build. It put
   `LraTableau::pivot` at 74% self time with `Rational` arithmetic separately
   and correctly attributed (those operators are defined out-of-line in
   `rational.cpp`, so they are not folded into `pivot`).
3. **Wrong claim #1: "53.6M Rational copies is a lot".** Stated while reading
   the profile. It is 1.05% of runtime. Chasing "trivial engineering to avoid
   the copies" would have won about one percent.
4. **Correcting that pointed at the right thing.** If the copies are 1% and
   the arithmetic is separately accounted for at ~20%, then `pivot`'s 74% is
   genuinely its own loop -- comparisons, vector writes, and `occurs_in_`
   maintenance.
5. **Wrong claim #2: "removals are rare".** Taken from the comment "This is
   the *only* removal the loop performs", which says *which* removal, not how
   often.
6. **Wrong claim #3: a general principle beat the data.** Asked why not a hash
   set, the first answer was that `occurs_in(entering)` is iterated in full
   every pivot and vectors iterate better than hash sets. True, and irrelevant
   *as stated*: iteration touches 1.26M entries against 8.17 billion for the
   removal scans, so it cannot be the reason. **Worth reading with hindsight**
   -- when the hash set was actually built it lost on 3 of 5 instances, and the
   reason was cache behaviour after all, just on the *removal* path rather than
   the iteration path ([](#occurs-in-prototypes)). The instinct was sound and
   the justification offered for it was not, which is a different failure from
   simply being wrong.
7. **Wrong claim #4: one erase site.** The first probe instrumented only the
   cancellation branch. Asking precisely *when* the erase happens surfaced a
   second, structural site in the same function that the probe had silently
   skipped.

The transferable lesson is narrow and practical: **the profile that closes a
lead is worth reading for what else it says.** Four of the six items above are
corrections to statements made confidently from code reading, and every one of
them was settled by a counter rather than by an argument.

### The real shape of this lead: the representation is half a sparse matrix

The three prototypes below are all workarounds for one missing thing, and it
is worth stating that before anyone writes a fourth.

**At the cancellation site, `pivot()` already holds the dying entry.** It has
`other_row[i]` and `var`, and it knows this occurrence is dead. The entire
expense is that `occurs_in_[var]` is a *separate structure with no link back
to the row entry*, so the code must **search** for something it is already
holding.

| candidate | what it does about the missing link |
|---|---|
| hash set | replaces the search with a hash |
| swap-pop + hash position index | replaces the search with a hash, and pays for two |
| tombstone + live counter | avoids the removal, defers the cost to iteration |

A **cross-linked sparse matrix** -- each nonzero linked into both its row and
its column, as King's thesis Section 2.2.7 specifies --
removes the problem rather than working around it. Removal becomes an unlink
in place: O(1), no search, no hash, no tombstones, no live counter, and
`occurs_in_[var].size()` stays exact for Markowitz pricing at no cost.

**What Primo had at the time this was written was half of that structure**:
row-wise contiguous storage (`Row = std::vector<std::pair<LraVarId,
Rational>>`) plus a column index (`occurs_in_`), without the cross-links that
make maintaining the column index cheap. *Superseded 2026-08-06:* `Row` is now
`std::vector<RowEntry>` with an `occ_slot` back-index, so the row-to-column
link exists. The column-to-row link still does not -- see
[](#lead-3b-cost-elsewhere), where that missing second half turns out to be
the cause of the measured regressions.

**And the missing half is not free to add**, which is why this is a real
design question rather than an oversight. The current speed comes from rows
being contiguous and rebuilt by a linear two-way merge -- that contiguity is
exactly why the hash set *lost* on short lists. Cross-linking costs one of:

- **Linked-list rows** (the classical form). Nodes never move, so back-links
  stay valid, but the contiguous merge that items H8 and H10 were built around
  is gone.
- **Array form with back-indices.** Rows stay contiguous and each entry stores
  its slot in `occurs_in_[var]`. An earlier version of this entry rejected this
  as needing ~128M scattered writes to refresh back-links on every rebuild;
  that was wrong, and the investigation below explains why. It is the
  recommended shape.

**So the question to answer is not "tombstone or hash set".** It is whether
the representation is right, and the two sub-questions are: does the
cross-linked form's O(1) removal outweigh losing contiguous rows, and is there
a form that keeps both. Neither is answerable by another micro-prototype of
the removal alone -- the two rejected prototypes already showed that the
removal mechanism is not where the remaining headroom is, since replacing it
wholesale bought 25% on the best case and lost on three others.

**Scope, measured rather than guessed.** `Row`'s element type has 19 uses in
`lra_tableau.cpp`, 12 structured bindings over rows, and 6 external users via
`row()`/`occurs_in()` in `check_sat.cpp`. This is not a contained change.

### Investigation: what King's form would actually cost here

Asked whether the cross-linked form is doable. Investigated 2026-08-06; the
short answer is **yes, in array form, and it is smaller than it first looked**
-- because a claim made earlier in this entry was wrong.

**Source note, updated 2026-08-06.** King's thesis is now available at
`docs/papers/king_tim.pdf` and Section 2.2.7 has been read -- summary in
[](papers/summaries.md). Reading it confirmed the direction and exposed a fork
this entry had not accounted for, recorded below. The array design further
down is a *variant*, not King's design, and should not be attributed to him.

**What King actually specifies, now that the source has been read.** Each
non-zero is a member of two doubly linked lists, row and column, with forward
and backward pointers for both; setting an entry to zero is an O(1) unlink
"given a pointer to the entry". That is precisely the missing capability. But
one clause changes the comparison:

> "(Note that these lists are not sorted. The elements may appear in any
> order.)"

**King's rows are unsorted.** Primo's are sorted by variable id, which is the
invariant `pivot()`'s two-way merge depends on and which item H8 was built
around. Because his lists are unsorted King cannot merge them, so row addition
loads one row into a **temporary dense map** of O(n) entries and walks the
other against it (Fig. 2.9). Both reach O(||Ti|| + ||Tk||); they pay for it
differently.

So adopting King's form wholesale means giving up sortedness, the two-way
merge, contiguity *and* item H8, and taking on an O(n) scratch map -- in
exchange for O(1) removal. That is a much larger and less obviously good trade
than "add cross-links". The array variant below keeps Primo's side of the fork
and buys most of the removal win.

**The correction.** This entry previously claimed the array form would need
"roughly 128M scattered writes" because `pivot()` rebuilds `other_row`
wholesale so every back-link would need refreshing. That conflates two
different positions. The back-link points from a row entry *into*
`occurs_in_[var]`. Rebuilding the row moves the entry within the **row**, but
does not touch `occurs_in_[var]` at all for a surviving term -- so the merge
simply **copies the slot along** with the variable and coefficient. No refresh.

**The resulting design.** `Row`'s element becomes
`(LraVarId var, Rational coeff, uint32 occ_slot)`, where `occ_slot` is that
term's index inside `occurs_in_[var]`. Removal, at a point where `pivot()`
already holds the row entry:

1. `slot = other_row[i].occ_slot` -- O(1)
2. swap-pop `occurs_in_[var]` at `slot` -- O(1)
3. repair the moved element's back-link:
   `b = occurs_in_[var][slot]`, then `lower_bound(rows_[b], var).occ_slot = slot`
   -- **O(log |row_b|)**

| | steps per removal |
|---|---:|
| today | scan ~679 elements |
| this design | ~7 comparisons (log2 of a ~102-term row) |
| | **~97x fewer** |

Insertion is O(1) (`occ_slot = occurs_in_[var].size()` before the `push_back`),
and the merge carries slots over free. **Rows stay contiguous**, so the
two-way merge, item H8 and the cache behaviour that made the hash set lose on
short lists are all preserved -- which is what makes this different from the
linked-list form.

**Scope, from the code rather than estimated.** `Row`'s element type appears
in 4 declarations, there are 22 structured bindings over rows that would need
a third field or `_`, `add_row`'s public signature takes pairs, the trail
stores whole `Row`s (`old_basic_row`, `old_other_rows`), and `check_sat.cpp`
has 6 external uses of `row()`/`occurs_in()`. Real but bounded -- a day's work
plus tests, not a rewrite.

**Three things that needed care, and were where a bug would hide** (all three
were handled in the implementation; kept because they are the checklist for
the remaining column-to-row half):

- **Backtracking.** `pop()` restores rows from the trail, and those rows carry
  `occ_slot` values from when they were saved. `occurs_in_` is rebuilt on that
  path (`occurs_in_index_stale` -> `rebuild_occurs_in_index`), so the rebuild
  must reassign every slot. The prototype bug found earlier -- stale index
  after a rebuild, with all 13 suites passing -- was exactly this class, and
  nothing currently tests rebuild-then-remove.
- **`occurs_in_index_matches_full_rebuild()`** must additionally check that
  every back-link round-trips:
  `occurs_in_[var][row_entry.occ_slot] == basic_var`. That check is what makes
  the whole scheme falsifiable in a debug build, and is worth writing before
  the optimisation rather than after.
- **`add_row`'s public signature** takes `pair`s. Either it keeps doing so and
  fills slots internally, or every caller changes. Keeping it is clearly right.

**Expected payoff, stated as a prediction before it was measured.** Replacing
the removal *entirely* with a hash bought 25% on tropical and lost on three
other instances (data in [](#occurs-in-prototypes)). This design should keep
tropical's win without the short-list losses -- but that is a prediction, and
this project's record on predictions of this kind is poor. See
[](#lead-3b-outcome) for what actually happened.

(occurs-in-prototypes)=
### The two rejected prototypes, and exactly why they were rejected

Both were built, measured and reverted on 2026-08-06; nothing of either is
committed. **This section exists because the rest of this entry cited their
conclusions in three places without ever giving the data, which made the
rejection impossible to check or remember.**

#### Prototype 1: `occurs_in_` as `std::unordered_set`

`:lra.pivots` was identical between baseline and prototype everywhere
measured, so the hash set's iteration order does not perturb pivot selection --
the timings measure container cost, not a search change.

| instance | avg list length | baseline | hash set | change |
|---|---:|---:|---:|---:|
| tropical `constraint-199552` | 679 | 4.24s | 3.20s | **-25%** |
| `sc-21` | 176 | 0.48s | 0.47s | -2% |
| `uart-16` | 86 | 0.26s | 0.25s | -4% |
| miplib `danoint-166` | 76 | 65.19s | 77.52s | **+19%** |
| latendresse `ecoli` | 43 | 0.26s | 0.34s | **+31%** |

**The deciding factor is average occurrence-list length, not total elements
scanned** -- and that distinction is the whole reason for the rejection.
`danoint-166` scans **1.8x more elements in total** than tropical and still
gets *worse*, because a vector scan is contiguous and vectorisable while a hash
erase pays a hash, a probe and a likely cache miss whatever the length. A
structure that is better asymptotically loses on the constant everywhere except
where lists are enormous.

**The initial-capacity setting made it worse, not better.** Asked for
explicitly, and measured: no reserve 3.20s, `reserve(8)` 3.50s, `reserve(32)`
3.38s. Reserving pays allocation and memory for *every* variable when most
lists are short, which is the same short-list penalty in another form.

#### Prototype 2: swap-and-pop with a position index

Loses to the hash set everywhere: tropical 4.11s -> 3.70s against the set's
3.25s, and worse than baseline on the other three.

The reason is structural rather than tunable: **the position index is itself a
hash map**, so each erase costs a hash find, a hash erase, and a hash write to
repair the moved element -- two to three hash operations where
`unordered_set::erase` costs one. It only wins with a *non-hash* index, and a
dense `pos_[var][basic]` is O(vars^2): 23MB for tropical, about **34GB** for a
LassoRanker instance with 92,937 reals.

That is what forced the reframing above: both prototypes work around a missing
back-link instead of adding one, and the swap-pop result is the proof, because
swap-pop *is* the right removal once the position is known for free. The
implemented design is exactly this prototype with the hash index replaced by
`RowEntry::occ_slot`.

(occurs-in-histogram)=
#### Where the scan cost actually lives, and a static-prediction lead

Histogram of occurrence-list length at each cancellation erase on tropical
`constraint-199552` (12.0M calls, 8.17B elements scanned):

| list length at erase | share of all scanning |
|---|---:|
| < 256 | 2.9% |
| 256-511 | 10.5% |
| 512-1023 | **51.6%** |
| >= 1024 | **34.9%** |

**86.5% of the scanning happens on lists of 512 or more**, so the mean of 679
hides a heavily skewed population -- which is why an average-case argument
about "long lists" was the wrong frame and the histogram was needed.

This follows from the formula's shape: tropical-matrix is a DAG of definitions
over 36 free `f*` variables ([](tropical-matrix-structure.md)), so pivoting
composes rows back toward those few variables and their occurrence lists grow
enormous.

**Recorded but NOT tested: this may be predictable statically.**
`scripts/analyze_smt2.py` already recovers free-versus-derived structure and
occurrence counts without solving. If those correlate with observed list
lengths, the representation could be chosen per instance -- or per variable --
*before* search rather than guessed. Checking that correlation gates any
adaptive scheme, and is the cheap first step.

**Falsifier for the whole lead, and its answer.** Stated before the work: if a
prototype does not materially reduce `pivot`'s share in a fresh profile, the
scan was not where the time went and the 8.17B figure is being misread. The
hash prototype cut tropical 25% with pivot counts unchanged, so the figure was
read correctly and the falsifier did not fire.

**Gate applied.** This touches the core tableau invariant and
`occurs_in_index_matches_full_rebuild()`, so it needed the QF_LRA model fuzz
gate on Slurm rather than unit tests alone -- job 119566, clean.

(lead-3b-outcome)=
### Outcome: implemented, and it clears the bar on the family it targets

```{note}
This entry is a chronological investigation log -- it records how the design
was reached, including the wrong turns. **For what is actually implemented
today, read [](arithmetic.md)'s "Cross-linked `occurs_in_` index" entry**,
which states the representation and its current evidence in one place without
the history.
```

Implemented as the array variant (`RowEntry::occ_slot`, `unlink_occurrence`),
with the round-trip check added to `occurs_in_index_matches_full_rebuild`
exactly as the "would need care" list above demanded.

**Correctness gate** (job 119566, 9-task Slurm array): clean -- 0 wrong
answers, 0 invalid models.

**Promotion bar** (job 119555, 237-file stratified sample, 10s): **neutral**.
The PAR2 gain of 8.00 is smaller than the 12.22 run-to-run noise floor
measured from the *identical baseline binary* across jobs 119530 and 119555,
so it is not a result. That is expected rather than disappointing: the
stratified sample excludes tropical-matrix as a family, every reference
solver failing it, and the change acts on long occurrence lists.

**The family it targets** (job 119577, 10 tropical files, 1200s):

| | baseline `dfb83f8` | crosslink `83afe14` |
|---|---:|---:|
| solved | 1/10 | **2/10** |
| PAR2 | 21610.65 | **19998.33** |
| wrong | 0 | 0 |
| `constraint-199552` | 10.65s | **6.80s** (-36%) |
| `constraint-223827` | timeout | **sat in 791.53s** |

**gained 1, lost 0.** [](#complementarity) rightly warns that `gained == 1` is
usually churn at the timeout boundary rather than capability -- but that
warning does not apply here, and the reason is worth stating because it is the
unusual part. This change **cannot perturb the search**: on 25 solved
stratified-sample files the branch reports counters bit-identical to `main`
(`:lra.pivots`, `:lra.pivot-term-ops`, `:sat.decisions`, `:sat.conflicts`), so
it is a pure constant factor on an unchanged search path. The extra solve is
therefore causally the speedup, not a lucky trajectory -- the one case where a
single gained instance is real evidence.

Remaining caveats, stated plainly: n=10, one run, and 791s of a 1200s budget
is close enough to the line that a rerun could lose it. The -36% on the file
that *both* arms solve is the more robust half of this result.

(lead-3b-cost-elsewhere)=
### Where it makes things worse, and why that is predictable

"Does it cost us elsewhere?" is answerable unusually precisely here, because
the change cannot perturb the search: every per-file time difference **is** the
constant factor, with no trajectory noise mixed in. Paired per-file ratios from
job 119555 (crosslink / baseline; below 1 is faster):

| band | n | median | >10% slower | >10% faster | worst |
|---|---:|---:|---:|---:|---:|
| all commonly solved | 174 | 1.014 | 40 | 40 | 5.27x |
| baseline > 0.2s | 83 | 0.985 | 7 | 22 | 1.58x |
| baseline > 1.0s | 44 | 0.988 | **0** | 6 | 1.10x |

**gained 0, lost 0** -- no file flipped in either direction. The all-files row
is timer noise on sub-0.2s instances (the "biggest wins" there are 0.25s ->
0.01s); the >1s band is the honest one, and there the worst regression is
1.10x.

**But the 7 regressions are not scattered, they are one shape.** Sorted by
severity: `ecoliFBAyicesTest-500` (1.58x), `p6-zenonumeric_s5` (1.45x),
`p50.56` (1.29x), `d50.9` (1.28x), `ecoliFBAyicesTest-750` (1.14x),
`clocksynchro_6clocks` (1.13x), `ecoliFBAyicesTest-63875-64` (1.12x). Three of
seven are latendresse/ecoli -- **the family this file already cites as having
the longest rows in the corpus, 691 terms on `ecoliFBAyicesTest-500`.**

That is the mechanism, and it is the mirror image of the win:

| | row length | column length | effect |
|---|---|---|---|
| tropical `constraint-199552` | 2.44 initially, ~102 in search | **avg 679** | **wins** -- the removed scan was 679 long |
| latendresse `ecoliFBAyicesTest-500` | **reaches 691** | short | **loses** -- see below |

`unlink_occurrence` swap-pops the column and then repairs the moved entry's
back-index with a `std::lower_bound` **over that entry's row**. That is
O(log row) with poor locality -- roughly 10 scattered probes into a 691-entry,
~27KB row -- and it replaces an old linear scan of a column that on this family
is *short*. Long columns pay the old cost; long rows pay the new one.

**The prediction this entry made was wrong, and it is worth saying so.**
[](#occurs-in-prototypes)'s "expected payoff" argued the array design would
"keep tropical's win without the short-list losses", because it removes the
hashing and preserves contiguity. It did keep the tropical win. It did **not**
avoid the latendresse loss: the hash set made that family ~31% slower and the
array form makes `ecoliFBAyicesTest-500` **58%** slower. (Different runs and
hosts, so the two percentages are not directly comparable -- the direction is.)
The reasoning had identified hashing and contiguity as the causes of the
short-list penalty and missed that the fixup introduces a *row*-length term,
which is precisely what that family maximises. The entry's own warning that
"this project's record on predictions of this kind is poor" held.

**Two candidate causes, and the experiment that separates them.** Besides the
binary search, `RowEntry` grew from 32 to 40 bytes, which taxes every row walk
in `pivot()`'s merge -- also proportional to row length, so both explanations
predict exactly the family we see. Narrowing `LraVarId` from `std::size_t` to
32 bits removes the memory half only: if the latendresse regression survives
that, the binary search is the cause.

**The principled fix is to finish the cross-link.** What is implemented is
*half* of one -- rows point into columns, columns do not point back into rows.
Storing the row slot alongside the basic variable in `occurs_in_` makes the
fixup O(1) and deletes the `lower_bound` entirely, which is what King's
doubly-linked form gets structurally. That would remove the mechanism rather
than trade against it, and it is the natural next step if this family matters.

Until then the trade is explicit and small: **0 files lost, worst case ~10% on
instances over a second, in exchange for tropical 1/10 -> 2/10.** It is also a
clean portfolio axis -- long-column instances want this on, long-row instances
want it off.

(lead-aep)=
## 4. Adaptive Early Pruning

**Claim.** Adapt the *frequency* of theory calls to their measured usefulness,
rather than using a cheap incomplete solver for them.

**Evidence.** Griggio thesis §2.3.1 (p. 42), presented there as not described
elsewhere: a `tsolvers_call_interval` counter, reset to 1 on a theory conflict,
halved when the call produced deductions, doubled after two consecutive useless
calls. Reported at 4 more instances solved and up to ~20% less total time on
SMT-COMP'09, on both sat and unsat sets.

**Cost.** Small-medium: a counter and four rules, but wired into the
DPLL/theory interaction, which is where correctness bugs hide.

**Falsifier.** If `:lra.online-check-calls` is not a material share of runtime
on the target families, there is nothing to save. **Check this first** -- it is
one stats run and it gates the whole item.

(lead-relevancy)=
## 5. Relevancy propagation

**Claim.** Do not assert theory atoms that the current partial assignment has
made irrelevant. z3's mechanism (de Moura and Bjorner); absent from Primo and
from [](papers/lra-related-work.md) entirely.

**Evidence.** Indirect but strong. The tropical encoding puts 468 arithmetic
definitions under guards, and the unguarding experiment shows what happens when
that arithmetic is forced to be live: 27x slower, 68x the pivots
([](tropical-matrix-structure.md)). Whether Primo is *already* doing some of
that wasted work under the guarded encoding is exactly what lead 1 measures.

**Cost.** Large. It is a cross-cutting mechanism touching the SAT interface,
theory assertion, propagation and explanation.

**Sequencing.** Gated on lead 1, which is its cheap static approximation. If
filtering on polarity alone moves these families, dynamic relevance is worth
the investment; if it does not, that is evidence about the family before a
large mechanism gets built. Needs its own paper note first, per AGENTS.md.

(lead-justification)=
## 6. Justification / goal-directed decision heuristics

**Claim.** Choose decisions by working backward from what is needed to satisfy
the formula's definitional structure, rather than by VSIDS activity. CVC4's
`JustificationHeuristic`, with roots in ATPG (PODEM/FAN).

**Evidence.** Structural only, and weaker than the others: the guarded families
are one huge assertion built from thousands of definitional chains, which is
the shape this heuristic class targets. Lead 2 is a narrow, cheap special case
of the same intuition, which is a reason to do lead 2 first and treat its
outcome as evidence about this one.

**Status.** No paper note exists. Not actionable until one does.

(lead-row-representation)=
## 7. Compare the row representation against sorted arrays of pairs

**Claim.** Worth confirming Primo's row storage against the alternative the
Griggio thesis reports testing.

**Evidence.** Thesis §2.5.2 (p. 51): they tried per-equation hash tables and
sorted arrays of `(variable, coefficient)` pairs, and kept the arrays despite
logarithmic rather than amortized-constant coefficient lookup, because
contiguous layout and faster iteration matter more -- iteration being what
pivoting does. Measured at **1.20x median / 1.30x mean** for the wrong choice.
Unusual in reporting the losing alternative, which is what makes it useful.

**Cost.** Small to check what Primo currently does; larger to change it.

**Falsifier.** If Primo already stores rows as contiguous sorted pairs, this
closes immediately as already-done.

### Outcome: closed, already implemented

The falsifier fired. `Row` is `std::vector<std::pair<LraVarId, Rational>>`
kept ordered by var id -- the Row invariant `pivot()`'s two-way merge depends
on -- with `lower_bound` for coefficient lookup. That is the thesis's design
including its log-time-lookup trade, and the class comment records that
`rows_` *was* `std::unordered_map` and was migrated away for exactly the
reason the thesis gives. Item H8 went further than the thesis describes,
replacing the post-concatenation sort with a linear merge.

A gprof profile confirms it rather than resting on the code read
(`perf` is blocked in this sandbox; `kernel.perf_event_paranoid` is 4):

| symbol | self time |
|---|---:|
| `LraTableau::pivot` | **74.1%** |
| `Rational::operator*` | 12.0% |
| `Rational::from_int128_reduced` | 3.9% |
| `Rational::operator+` | 2.9% |
| `LraTableau::update` | 2.1% |
| `Rational` copy constructor | 1.1% |
| **hash/map in the LRA path** | **0.00%** |

The only hashtable symbols in the profile are `TermPairKey` lookups --
Nelson-Oppen interface equalities -- at 0.00%. There is no container overhead
left to win here; the thesis's 1.20x is already banked.

**What the profile points at instead**, for whoever picks up the next
arithmetic lead: term-operation volume and rational cost, not the container.
83.0M `from_int128_reduced` calls against 48.1M multiplies is roughly 1.7
normalisations per multiply, and 53.6M `Rational` copies against 1.26M row
updates is a lot of copying. Fewer terms (fill-in) or a cheaper small-rational
path are the levers this profile supports.

(lead-dl6)=
## 8. DL6 -- decide precision statically, before search

Recorded in full at [](papers/difference-constraint-propagation.md). For a
difference-logic solver based on negative-cycle detection, sum the absolute
values of the constants: if that fits the native integer type, no path or cycle
weight can overflow, so the whole computation can run overflow-check-free
rather than through the two-tier rational's per-operation check.

**Blocked** on `src/theory/arithmetic/difference_logic.hpp` being real (H6 Tier
2). Recorded now because it is a design constraint on a solver not yet written:
retrofitting a check-free path is harder than designing for one.

**Falsifier, stated in advance.** If the sum overflows on most real DL
benchmarks the pass is dead weight. Compute it over `DTP-Scheduling`,
`clock_synchro` and `tta_startup` before writing solver code.

(lead-monotone)=
## 9. Monotone (one-sided) variable elimination

**Claim.** If every constraint mentioning a real variable pushes it the same
direction -- after accounting for Boolean polarity -- that variable can be sent
to its extreme and those constraints discharged. This is the Fourier-Motzkin
observation that eliminating a single-sign variable generates **zero** new
constraints, and the LP-presolve notion of a dominated column.

**This lead was closed on 2026-08-05 and reopened the same day.** The closure
rested on a screen of *one file per family*, eight files, which found almost no
one-sided variables. Running `scripts/analyze_smt2.py` over the **entire local
QF_LRA corpus -- 1,753 files, all 17 families, zero analysis failures** --
contradicts it:

| family | files | reals | one-sided | share |
|---|---:|---:|---:|---:|
| keymaera | 21 | 89 | 29 | **32.6%** |
| meti-tarski | 488 | 1,480 | 365 | **24.7%** |
| 2017-Heizmann-UltimateInvariantSynthesis | 58 | 22,319 | 442 | 2.0% |
| LassoRanker | 419 | 4,455,966 | **85,120** | 1.9% |
| sal | 107 | 7,226 | 67 | 0.9% |
| DTP-Scheduling | 91 | 7,650 | 52 | 0.7% |
| TM | 25 | 14,531 | 41 | 0.3% |
| 2019-ezsmt | 105 | 91,853 | 10 | 0.0% |
| clock_synchro, latendresse, miplib, sc, tropical-matrix, uart, spider, check | 434 | 154,062 | 0 | 0% |
| **total** | **1,753** | **4,756,176** | **86,126** | **1.8%** |

**The population is real and has a mechanism.** The one-sided variables in the
Heizmann family are named `motzkin_*`: they are Farkas/Motzkin multipliers from
constraint-based invariant synthesis, which are non-negative by construction
and therefore appear only with one-directional bounds. LassoRanker is the same
lineage (ranking-function synthesis via Farkas' lemma), which is consistent with
its 85,120. These are not incidental variables; they are a structural feature of
how that whole class of benchmark is generated.

**Why it is interesting.** `LassoRanker` and `2017-Heizmann` are gap families --
[](qf-lra-heuristics.md) lists Heizmann/LassoRanker as "hard for everyone" --
and 85,120 eliminable columns is a large absolute number even at 1.9%.

**Cost.** Small-medium. The detection already exists in the analyzer; the work
is a preprocessing pass in the solver plus the polarity bookkeeping to make it
sound on non-NNF input.

**Two caveats, both to check before building anything.**

1. **The high-percentage families may not be where the time goes.**
   `meti-tarski` and `keymaera` have the best ratios but are families Primo
   already matches the reference solvers on, and `meti-tarski`'s files are tiny
   (3 reals each in the 100% cases). The value, if any, is in LassoRanker and
   Heizmann.
2. **Eliminating a variable is not automatically a win.** These are columns in
   an already-sparse tableau; removing them helps only if it removes rows or
   reduces fill-in. Measure the tableau shape before and after, not just the
   variable count.

**Falsifier, stated in advance.** Implement detection only, report
`:preprocessing.monotone-eliminable` on LassoRanker and Heizmann, and check it
against `:lra.rows` / `:lra.row-nonzeros`. If eliminating those columns does not
reduce rows or fill-in materially, the lead closes for real this time, and it
closes on solver counters rather than on a syntactic census.

### Outcome: implemented, and it did not clear the promotion bar

**Status: implemented behind `--monotone-elimination`, correctness-gated, and
NOT promoted.** The prototype numbers below did not survive a real sample.

Slurm job 119465, 237-file stratified sample, 10s bar, same binary both sides:

| configuration | solved | PAR2 | wrong |
|---|---:|---:|---:|
| default | **174** | **1415.10** | 0 |
| `--monotone-elimination` | 173 | 1432.84 | 0 |

One file lost, none gained. Over the 71 instances both configurations solve in
more than 0.2s the paired effect is **neutral** -- median ratio 0.997, p10
0.986, p90 1.014, 147.9s against 149.1s in total. The lost file
(`LassoRanker/.../byron-2...phaseTemplate`) is a genuine 3.5x slowdown rather
than boundary churn: re-run at 120s it takes 3.14s by default and 10.91s with
the pass. That single file accounts for 16.81 of the 17.75 PAR2 delta.

Worth recording precisely because it is the trap this register exists for: a
first cut of the analysis reported "faster on 57 files, slower on 1", which
counted sub-second instances where the ratio is noise. On non-trivial
instances the effect vanishes. Correctness held throughout -- zero wrong
answers here, and a clean fuzz gate at job 119430 (3,000 cases, 0 wrong
answers, 2,594 models validated, 0 invalid).

The pass stays available as an option and its machinery -- polarity-aware
one-sidedness detection, and postsolve model reconstruction -- is reusable.
But nothing about it is proposed for default on this evidence.

### Prototype measurement, 2026-08-05

Caveat 2 above has now been answered, and the answer corrects the framing that
produced it. **The saving is mostly in the Boolean structure, not the tableau.**
Replacing an atom by a constant removes it from the CNF and satisfies the
clauses containing it; dropping tableau rows is the secondary effect.

An external prototype (scratchpad, not committed) computes one-sidedness with
polarity and coefficient signs, then replaces every atom mentioning a one-sided
variable with the constant its own occurrence requires -- `true` at positive
polarity, `false` at negative. That is Fourier-Motzkin on a single-sign
variable: with no opposite bounds to pair against, every constraint mentioning
it is discharged outright.

**Soundness spot-check.** 24 files spanning the families with a population,
z3 on the original against z3 on the transformed file: **24 agree, 0 disagree**.

**Effect on files that take real time**, same binary, answers unchanged (both
`unsat`):

| file | atoms folded | clauses | rows | pivots | time |
|---|---:|---|---|---|---|
| Heizmann `_standard_copy4...bpl_7` | 192 | 2,909 -> **2,507** | 618 -> **543** | 146,833 -> 147,931 | 15.77s -> **14.15s** |
| LassoRanker `2013POPL-BenAmGen-Ex3.20...pieceTemplate` | 21 | 1,946 -> 1,925 | 622 -> 622 | 97,777 -> **69,828** | 12.19s -> **7.84s** |

The second row is the instructive one: **21** atoms folded, **no** row
reduction at all, and still 36% faster. The win there is not tableau size, it
is that a smaller Boolean structure produces a different and better search.
That also means caveat 2's proposed falsifier -- "if it does not reduce rows or
fill-in, close it" -- was the **wrong test**, and would have closed a lead that
works. Judge it on end-to-end time and `:sat.clauses`, not on `:lra.rows`.

On the smallest `meti-tarski` files the transformation discharges the problem
completely (`:sat.clauses` to 0, tableau gone), but those solve instantly
already, so they are not evidence of value.

**Revised effort estimate.** Detection and rewrite are both small; the real
cost is elsewhere and should not be discovered late:

- **Model reconstruction is the actual work.** Primo emits models
  (`(define-fun x () Real 0)`), and an eliminated variable is no longer in the
  tableau, so its value must be computed *after* the rest of the model is
  fixed, from the constraints that were discharged -- in dependency order,
  since those bounds can mention other variables including other eliminated
  ones.
- **The fuzz gate is mandatory**, since this changes preprocessing: merge tier
  at minimum, campaign tier if it is ever promoted to default. Per the standing
  rule, on Slurm rather than locally.

So: worth doing, tractable, and **not** a one-afternoon change. The honest
summary is a real but moderate speedup (roughly 10-36% on the two timed files),
available on maybe a quarter of the families, for a moderate implementation
with a genuine model-reconstruction obligation.

**Coverage note.** The corpus screen analyzed 1,753 of 1,753 files with no
timeouts and no memory failures, so no file was excluded and the denominator is
complete. Two families carry a real blind spot: `sc` (20.8% of atoms opaque)
and `tta_startup` (12.1%), both from `ite` inside arithmetic terms, where the
analyzer conservatively forces every variable two-sided. Their zeros therefore
mean "could not tell", not "none". Every other family is at 0.0% opaque.

(lead-z3-cheap-shared-equality)=
## 10. Current-model-filtered unit-row equality recognition

**Status: open, added 2026-08-17 after reading Bjørner and Nachmanson,
"Arithmetic Solving in Z3" (CAV 2024), Section 3.2. This is a mixed-theory
QF_UFLRA lead, not a pure-QF_LRA lead.** The paper explicitly says Z3 disables
this equality propagation for pure arithmetic logics such as QF_LRA and
QF_LIA. Its purpose is to communicate arithmetic equalities to another theory
(and, outside Primo's fragment, to nonlinear arithmetic reasoning).

**Current cost.** Primo's sound `LraTableau::entailed_equalities()` producer
implements `FixedEq`, `EqRow`, `EqOffsetRows`, and `EqRows`. The latter two
construct and hash exact fixed offsets; the general rule also hashes the full
non-fixed row polynomial. The Z3 paper identifies large-numeral offset
computation and offset-table hashing as avoidable overhead. It first restricts
attention to shared variables equal in the current arithmetic model, then
recognizes the useful one-row and same-sign two-row shapes through a common
non-basic variable with unit coefficient. The current model equality plus
fixedness of every remaining row variable establishes equality without
constructing the offset.

**Why this does not change pure QF_LRA.** Primo calls the producer from
`lra_entailed_shared_equalities()` in the mixed QF_UFLRA coordinator, over the
solve-scoped `shared_real_terms` interface. The pure QF_LRA path neither builds
that interface nor calls this producer. An implementation should remain a
read-only query on the LRA tableau and should not add pivot hooks, rows, bounds,
or variables. Pure-QF_LRA regression is still mandatory to verify that this
isolation was preserved, but no pure arithmetic statistic should change.

Do not conflate this with Section 3.3 of the same paper. Direct and row-derived
**bound propagation** can help both pure and mixed arithmetic because the
consequences are existing SAT-visible arithmetic bound atoms. It is a separate
lead with separate activation and performance evidence.

**Implementation shape.** Reuse the model partition already constructed by the
mixed coordinator, inspect only non-singleton shared model classes, recognize
the two unit-row shapes without evaluating fixed offsets, and collect the fixing
lower/upper literals as the equality explanation. Emit deterministic spanning
edges. A missed shape means only "not cheaply established": retain the current
explained general producer or strict-orientation exact fallback for
completeness. Model-equal pairs remain conjectures unless a structural rule or
exact fallback supplies an explanation.

**Counters required before promotion.** Model classes and candidate pairs
visited; one-row and two-row matches; rows and nonzeros inspected; fallthroughs
to `EqOffsetRows`, `EqRows`, and exact probing; reason literals emitted. A
large-numeral regression must demonstrate that the fast path avoids fixed-offset
`Rational` work rather than merely returning the same equality afterward.

**Falsifier.** Close or downgrade the lead if profiling shows the current
offset construction/hashing is not material on the slow QF_UFLRA families, if
the unit-row rules rarely fire, or if a full mixed-corpus A/B does not improve
PAR2/common-solved time without losing solved instances. Any pure-QF_LRA
non-time statistic change is an implementation-boundary failure, not an
acceptable tradeoff. See [](papers/arithmetic-solving-in-z3.md) for the full
paper mapping and test obligations.

(lead-unconstrained-terms)=
## 11. Unconstrained-term substitution (Brummayer/Bruttomesso)

**Claim.** A term is *unconstrained* if it has exactly one occurrence in the
whole formula (one parent in the term DAG) and its enclosing operator is
invertible with respect to that occurrence -- then the term can be replaced
by a fresh variable of the same sort, because whatever value the rest of the
formula needs from it, the lone occurrence can always be adjusted to produce.
Independently discovered by Bruttomesso (PhD thesis, Trento, 2008) and
Brummayer (PhD thesis, Linz, 2010; `docs/papers/Brummayer-PhD-Thesis-2009.pdf`,
Sect. 3.4, read 2026-08-17) for bit-vectors and arrays, and generalized to
partially-constrained terms and quantified formulas by Jonáš & Strejček
(SAT 2017, `docs/papers/On_Simplification_of_Formulas_with_Unconstrained_Variables_and_Quantifiers.pdf`,
read in full 2026-08-17). See [](papers/monotone-variable-elimination.md) §3
for the full reading trail and why this is a genuinely different rule from
lead 9, not an instance of it: lead 9 needs *many* same-signed occurrences
of a bare Real leaf in `Le`/`Lt` atoms and explicitly disqualifies anything
under `f(...)`; this rule needs exactly *one* occurrence anywhere -- nested
inside `+`/`*`, an `=`, an `ite`, or an EUF application argument -- and does
not care about polarity or direction at all. Existing solve-eqs/Gaussian
elimination also does not cover it: those need an explicit top-level
defining equality (`x = t`), however many times `x` is *used* afterward,
whereas this rule needs no defining equality anywhere.

For LRA specifically the rule is strictly simpler than the bit-vector
setting it was designed for: addition and multiplication by a nonzero
rational constant always invert in `Q`, so the bit-vector corner cases (an
*even* constant multiplier is not invertible mod `2^n`; a comparison against
an extremal constant `0`/`2^n-1` is not free either way) do not exist for
reals at all.

`grep -rn "unconstrained" src/smt/*.cpp` (2026-08-17) finds no such pass --
only unrelated counter names. Not implemented.

**Evidence: population census, both local corpora, same methodology as
lead 9.** A throwaway analyzer (scratchpad, not committed; reuses
`scripts/analyze_smt2.py`'s tokenizer/parser) counted declared Real
constants with exactly one raw occurrence in the whole assertion set, then
split them by whether that occurrence is already a bare leaf of an
`Le`/`Lt`/`Ge`/`Gt`/`=` atom -- i.e. already reachable by monotone
elimination or solve-eqs today -- versus genuinely new (nested, or under a
function-application argument):

| corpus | files | total reals | single-occurrence | already covered (bare leaf) | new opportunity |
|---|---:|---:|---:|---:|---:|
| `tmp/QF_LRA` (local, 1,753 files, lead 9's corpus) | 1,753 | 4,756,176 | 17,394 | 17,263 | **131** (0.003%) |
| `tmp/QF_UFLRA` (remote `primo-adaptive-mutation`, 1,284 files) | 1,284 | 217,051 | 3 | 2 | **1** (0.0005%) |

Both counts are zero analysis failures. The "already covered" bucket
slightly over-credits solve-eqs (it is top-level-conjunct-only; this check
is not scoped that way), which makes the "new opportunity" column an
underestimate if anything -- and it is still negligible on both corpora.

**Why the population is this small, and why that might not be the whole
story.** Bruttomesso and Brummayer's own motivating source is symbolic
execution of programs: a query variable read at most once, or a program
variable assigned on an executed path but not yet read, both of which are
common when analyzing SSA-form code such as LLVM IR. Primo's QF_UFLRA
corpus is SV-COMP CPAchecker BMC/induction output -- also program-derived,
but evidently not exhibiting this pattern at the Real-variable level, either
because CPAchecker's own encoding already simplifies such variables away
before emitting SMT-LIB, or because the pattern shows up in Boolean/integer
variables that this LRA-only census does not count, or because it simply
is not a strong pattern in this specific translation pipeline. This is a
real unknown, not resolved here: the corpora actually available to Primo
are not the corpora the technique was designed against.

**Cost.** Small-medium if attempted: propagate outward from a single-occurrence
leaf through invertible operators (`+`, `-`, scalar `*`), rewriting to a
fresh variable, with model reconstruction analogous to solved-constant
substitution's `x -> t` trail (here the direction is reversed -- the fresh
variable's value must be pulled back through the inverted operator chain to
recover the eliminated variable's value, which needs its own reconstruction
logic, unlike Brummayer's array/BV setting where the paper does not discuss
model recovery in detail either).

**Falsifier, stated in advance.** Given the census above, implementing this
before a representative benchmark set exists would almost certainly repeat
lead 9's outcome (correctness-clean, promotion-bar-neutral-to-negative,
kept opt-in at best) without even lead 9's redeeming 85,120-variable
LassoRanker population to point to. **Do not implement on the current
evidence.** Revisit only if a symbolic-execution- or SSA-derived benchmark
family (the literature's actual motivating source) is added to the corpus,
and re-run this census against it first -- if that population is also
negligible, close this lead without ever writing the solver-side pass.

(lead-recursion-audit)=
## R1. Audit input-sized recursion across the repository

**Status: open and deliberately deferred.** The SMT-LIB frontend was already
converted to explicit stacks after fuzzing exposed depth-dependent stack
overflows. Eager nullary-definition compilation then exposed the same class of
failure in downstream whole-formula walkers: many shallow definition bodies can
form one very deep hash-consed `TermId` DAG. The immediate preprocessing,
dispatch, purification, Boolean-bridge, and clausification paths are now
iterative, but that focused repair is not evidence that the rest of the
repository is stack-safe.

The follow-up is a whole-tree call-graph and source audit. Classify every
recursive function by what bounds its depth:

- recursion proportional to parser input, syntax depth, reachable `TermId` DAG
  depth, equality-proof depth, or another user-controlled graph size must be
  replaced by an explicit worklist or given a documented and enforced limit;
- structurally bounded recursion, such as a fixed number of sort or operator
  cases, may remain only when the bound is explicit in the nearest design page;
- mutually recursive helpers and recursion hidden behind callbacks must be
  included, not only functions that call themselves textually.

Known starting points include EUF term registration and explanation traversal,
the recursive repair noted in the LRA tuning documentation, model and printer
walkers, rewriting/substitution utilities, and test or diagnostic code that can
receive solver-produced DAGs. LRA-owned code must be audited with the active LRA
work rather than changed opportunistically from the Nelson-Oppen branch.

Acceptance requires a checked-in inventory with an owner and disposition for
every finding, iterative replacements for all unbounded production paths,
deep-chain and deep-shared-DAG regressions for each affected layer, ASAN runs of
those regressions, and the ordinary merge fuzz gate. A grep for self-calls is
only a starting aid and does not complete the audit.

(lead-qflia-preprocessing)=
(lead-qflia)=
## 12. QF_LIA preprocessing is fully disabled, and conflict clauses don't generalize

**Status: open, added 2026-08-20.** Follows directly from the 428-file
representative-sample evaluation against z3 in
[](qf-lia-branch-and-bound.md)'s "Real-world benchmark evaluation and the
fuzzing gap" section (that section covers the four soundness/completeness
bugs the evaluation and the mutation-fuzz gate found and fixed; this lead
is the performance follow-up on the same evaluation, once the answers
were trustworthy).

**Claim.** Two compounding causes, not one, explain why primo takes
orders of magnitude more search steps than z3 on QF_LIA instances it
should solve trivially: (a) QF_LIA preprocessing is forced off wholesale
(`options.preprocessing_mode = PreprocessingMode::Off`), including passes
that turn out to be already Int-safe or cheaply made so; (b) primo's
arithmetic conflict clauses (from branch-and-bound, GCD-consistency, or a
Gomory cut) block the *entire* current arithmetic-atom assignment rather
than a minimized/generalizing explanation, so the SAT solver re-derives
near-identical conflicts thousands of times instead of learning once.

**Evidence.** Two files from the sample evaluation's worst-gap families
([](qf-lia-branch-and-bound.md)'s gap table: `calypto` 3/15, `prime-cone`
4/15 against z3's 15/15 on both), same local machine, z3 `-st` vs primo
`--stats`:

| file | z3 | primo |
|---|---|---|
| `prime-cone/prime_cone_unsat_6.smt2` (1KB) | unsat, 0.01s, 33 conflicts, 60 decisions | **times out at 20s**, 6 pivots, 0 `sat.decisions` (a candidate model is found almost instantly, then the search never returns from branch-and-bound's node loop to let the SAT solver backtrack) |
| `calypto/problem-002267.cvc.1.smt2` (3.5KB) | unsat, 0.02s, 266 conflicts, 9,089 decisions | **times out at 20s**, **56,870 `sat.conflicts`, 2,585,255 `sat.decisions`** (210x / 285x z3's counts, still unresolved) |

On the calypto file `:lra.theory-conflicts` (56,864) sits within 6 of
`:sat.conflicts` (56,870) -- essentially every SAT conflict traces to a
fresh arithmetic conflict, the signature of coarse, non-generalizing
blocking clauses. This is already named as a known gap in
[](qf-lia-branch-and-bound.md)'s "Where branching lives" section ("Sound
... but not tight ... Tightening this ... is future work, not blocking
for stage 1"); this is the first concrete measurement of its cost.

z3's own stats point at what closes these instances instead of
branching: `arith-dio-calls`/`arith-dio-tighten-conflicts` (Diophantine/
Omega-test reasoning -- 256 of 266 total conflicts on the calypto file
are closed via Dio, not branching), `arith-hnf-calls` (Hermite Normal
Form cuts), `arith-cube-calls` (the Bromberger-Weidenbach cube test,
Bjørner & Nachmanson Sect. 4.2 -- already named and explicitly deferred
in [](qf-lia-branch-and-bound.md)'s "GCD-consistency" section).

Four separable sub-leads follow, ranked cheapest first.

(lead-qflia-preprocessing-safe)=
### 12a. Enable the already-Int-safe preprocessing passes

Audited every pass QF_LIA's blanket `preprocessing_mode = Off` currently
disables (2026-08-20, reading `src/smt/preprocessing.cpp` and
`src/smt/check_sat.cpp`), classified by whether it does anything
domain-unsafe for Int:

| pass | mechanism | Int-safe? | why |
|---|---|---|---|
| `arithmetic_atom_canonicalization` | rewrites an atom to a canonical `sum <= sum` form via `primitive_integer_scale` and dedups syntactically-equivalent atoms onto the same SAT variable | **no, as first assessed -- corrected below** | `primitive_integer_scale` does not just clear denominators; on already-integer coefficients it *reduces by their gcd* (`4x+4y` -> scale `1/4`), which is exact for Real but not for Int without the same ceil/floor rhs tightening `bound_assertion()` already does elsewhere |
| `substitute_solved_constants` (bare form, `enable_gaussian_elimination=false`) | `x = t` (`x` a nullary declared symbol not occurring in `t`) -> replace every occurrence of `x` with `t` | **yes, confirmed** | pure syntactic substitution, no coefficient scaling or division anywhere |
| `substitute_solved_constants` (`enable_gaussian_elimination=true`, **the `CheckSatOptions` default**) | Gauss-Jordan row reduction over Real-sorted equalities (`collect_real_equalities` is hardcoded to `real_sort()`); divides every other coefficient in a pivot row by the pivot coefficient | **no, as written** | `coefficient / pivot_coefficient` (`preprocessing.cpp:764`) silently drops the divisibility constraint a non-unit pivot needs for the eliminated variable to stay integer -- the same class of bug this session already found and fixed three times in the tableau/GCD code, here at the term-rewriting level instead |
| Monotone (one-sided) variable elimination | hardcoded to "0-ary **Real** `Constant`" only; discharges one-sided atoms, then reconstructs a witness value as `tightest_bound +/- 1` | **no, as written** (the elimination reasoning generalizes; the model reconstruction does not) | `apply_monotone_eliminations` (`check_sat.cpp:5678`) computes `bound = (atom.rhs - rest) / coefficient` then `chosen = tightest +/- 1` -- for an Int variable this can and will produce a **fractional witness value** whenever the tightest bound isn't already an integer (e.g. `3x <= 7` -> bound `7/3` -> chosen `4/3`), printed straight into the model |
| everything else under `preprocessing_mode` (finite-domain-at-most-one, UF symmetry breaking, structural selector, congruence-definition generation, eq-bridge, ...) | all reason about uninterpreted functions or Bool-sorted finite-domain structure | **N/A** | QF_LIA's own scope excludes uninterpreted functions entirely ([](qf-lia-branch-and-bound.md)'s "Scope" section); these passes have nothing to fire on in a QF_LIA formula regardless of the flag |

**Outcome (2026-08-20): bare-form `substitute_solved_constants` implemented
and enabled by default for QF_LIA; `arithmetic_atom_canonicalization`
attempted, found unsafe by this project's own test suite, and reverted --
worth recording as another data point for how often "should be safe by
inspection" is wrong on this codebase.**

**What shipped.** `check_sat.cpp` now calls `substitute_solved_constants`
directly for QF_LIA (`enable_gaussian_elimination=false` explicitly, not
`options.gaussian_elimination`, which defaults true and stays wrong for
Int per the table above), in a small capped loop (8 passes, break on no
acceptance) rather than through the large preprocessing loop -- each pass
fully rewrites accepted definitions into the remaining assertions before
the next pass collects fresh candidates, so a chain like `x=y, y=5`
resolves the same way the Real-side loop's repeated calls do, without
needing any cross-pass dependency tracking.

**Evaluated follow-up (2026-09-03).** Pure QF_LIA `auto` still maps to this
substitution-only path. QF_UFLIA `auto`, and explicit `basic`/`strong`, run an
audited Boolean/value-propagation subset: non-distributing NNF, Boolean units,
forced atoms, solved constants without Gaussian candidates, and unit-equality
consequences. Explicit `strong` additionally recognizes disjunctive domains of
distinct Int numerals for AMO and equality-definition channeling, using the
SMT-LIB Ints semantics rather than invented disequality assumptions. Numeral
domains are excluded from UF symmetry. The 659-file QF_UFLIA SLURM evaluation
and generated/model fuzz gates promote only the `basic` subset for QF_UFLIA;
they do not justify changing pure QF_LIA or enabling numeral-domain clauses.

**The bug that shipping this caught.** `build_qf_lia_model`
(`check_sat.cpp:5929`) reads a variable's value straight from
`PreparedLraTheory::variables_by_term`/the tableau; a variable eliminated
entirely by substitution never reaches either, and the function defaulted
it to `0` -- silently wrong whenever the eliminated value wasn't 0
(caught immediately by `qf_lia_solver_tests.cpp`'s
`trivially_sat_needs_no_branching`, `(assert (= x 7))` printing `x`'s
value as `0`). Fixed by threading the accepted `(variable, definition)`
pairs through `solve_qf_lia`/`solve_qf_lia_impl`/`build_qf_lia_model` and
adding `evaluate_qf_lia_model_term`, an Int-sorted analogue of the
existing `evaluate_real_model_term` used for QF_LRA's own monotone-
elimination model reconstruction -- it only ever needs to resolve
literals, `+`/scalar-`*` structure, and tableau-resident variables, never
another substitution's definition, because each pass already inlined any
such reference before the next pass could collect it as a candidate.

**The bug that reverted `arithmetic_atom_canonicalization`.** Enabling it
alongside the above made `qf_lia_solver_tests.cpp`'s
`gcd_uncatchable_divergent_system_hits_the_node_limit`
(`4x+4y` bounded in `[2,3]`, no integer solution, deliberately meant to
hit `--lia-branch-limit` and report `Unknown`) fail outright with
`Int literal must be an integer value` -- `canonical_linear_side`
(`preprocessing.cpp:449`) builds a constant summand via the one-argument
`mk_rational(value)` overload, which defaults to `real_sort()`
regardless of the atom's actual sort (fixed separately, since it is a
real bug independent of Int-safety -- an Int atom with a nonzero constant
summand under `arithmetic_atom_canonicalization` would previously have
built a sort-mismatched term even before the gcd-reduction issue could
matter). Once that was fixed, the gcd-reduction issue in the table above
still stood, so the flag itself stays off for QF_LIA. **Do not re-enable
without first adding rhs ceil/floor tightening to
`canonicalize_arithmetic_atom`, mirroring `bound_assertion()`'s own,
including its "worked through explicitly" eight-combination comment as a
template for what needs re-deriving here.**

**Verification.** Full test suite (18 suites) green; both `convert` repro
files, the delta-infinitesimal repro, and both opfuzz-found bugs from
earlier this session re-checked correct; 500 further local FuzzSMT cases
against z3 (seed 99), zero disagreements; full local `convert`-family
recheck, 0 wrong. Performance effect on the two z3-comparison files from
this lead's own motivating evidence (`prime_cone_unsat_6`,
`calypto/problem-002267`): **none measurable** -- both still time out
with the same pivot/decision counts as before. Solved-constant
substitution alone was never expected to move files whose gap comes from
missing search-time integer inference (12c) or the atom-canonicalization
dedup this entry had to revert; it is a real but narrow win (fewer
tableau variables/rows on formulas with actual `x = t` definitions), not
the lever those two files need.

**Falsifier, satisfied for the shipped half.** Changed no answer on the
local QF_LIA corpus/fuzz gates (confirmed above). **Falsifier for the
reverted half**: re-attempt only after the ceil/floor fix, and re-run
this exact test suite before trusting it -- `gcd_uncatchable_divergent_
system_hits_the_node_limit` is now the regression test for this specific
failure mode.

(lead-qflia-preprocessing-gauss)=
### 12b. Partial (unit-pivot) Gaussian elimination for Int

**Claim.** Restricting Gaussian-elimination pivot selection to
coefficients of exactly +-1 needs no division at all
(`coefficient / (+-1) = +-coefficient`, exact), so it is unconditionally
Int-safe while still catching definitions the bare-form pass in 12a
can't (e.g. `x + y = 5` solved for `x`, or a variable only solvable
jointly from several equations).

**Cost.** Medium: `collect_real_equalities` needs generalizing to accept
Int equalities too (rename or parameterize), and
`collect_gaussian_elimination_candidates`'s pivot search needs an
`is_integer_domain` restriction that skips non-unit pivots when true.
Real's existing (unrestricted, division-based) behavior must not change.

**Falsifier.** If real QF_LIA formulas rarely contain a unit-coefficient
variable in an equality, this is a small win not worth the fuzz-gate
obligation -- run the same kind of population census lead 9/11 ran for
QF_LRA (`scripts/analyze_smt2.py` or a throwaway variant) over
`sandbox/non-incremental/QF_LIA` before implementing, not after.

(lead-qflia-dio-hnf-cube)=
### 12c. Search-time integer inference: Dio/HNF/cube

**Claim.** The bigger lever, per the evidence above -- z3 closes the
calypto file almost entirely through Diophantine tightening
(`arith-dio-tighten-conflicts` is 96% of its conflicts) rather than
branching, and the cube test is explicitly named as deferred waterfall
work in Bjørner & Nachmanson Sect. 4.2 (already cited in
[](qf-lia-branch-and-bound.md)).

**Cost.** Large -- new algorithmic machinery in the theory solver, not a
preprocessing pass; closer in scope to stage 3 (Gomory cuts) than to
anything in 12a/12b.

**Status (updated 2026-08-21).** The paper note this lead was gated on now
exists: [](papers/arithmetic-solving-in-z3.md) summarizes Bjørner &
Nachmanson Sect. 4 in full, so the standing "needs a paper note first"
block is lifted. Reading it split this lead into four separable pieces of
very different cost, and re-ranked them:

- **Patching (Sect. 4.1) -- DONE**, shipped the same day as stage 5. It
  was never part of this lead's framing, because the lead was written
  before the paper's Section 4 had been read: patching is the *first*
  layer of that waterfall and Primo had skipped it entirely. See
  [](qf-lia-branch-and-bound.md)'s "Patching".
- **Cube test (Sect. 4.2) -- deferred behind a prerequisite, not merely
  unstarted.** Measured 2026-08-21: 249 of 300 sampled instances contain
  equality atoms, and an asserted equality fixes a slack, which makes the
  tightened cube system contradictory. Cubes alone would therefore be
  inconclusive on most rounds. ~~It needs z3's difference/octagon twist
  (same section) to be worth building, so treat the twist as part of the
  work rather than a later refinement.~~

  **Corrected 2026-08-29 against the primary source.** The measurement stands;
  the named prerequisite was wrong. Bromberger's thesis
  ([](papers/bromberger-linear-arithmetic-thesis.md), Chapters 4--5) is where
  the cube test comes from, and it says two things Bjørner & Nachmanson's
  summary does not:

  1. The fixed-slack case is a **known skip condition with a constant-time
     test** -- skip when an integer variable has `U - L = 1` or a slack has
     `U - L = 0`; substitute out fixed integer variables first (§7.2.4).
     z3's difference/octagon twist is a refinement for difference and octagon
     *rows* and does nothing about a fixed slack, so it is not this lead's
     gate.
  2. The real enabler for equality-carrying inputs is the **implied-equality
     basis** (thesis Chapter 5, registered here as [lead
     17](#lead-equality-basis)). Chapter 5 exists *because* equalities break
     the cube tests, and §5.7 names equality elimination as the preprocessing
     step that unblocks them.

  So the dependency is cubes → equality basis, not cubes → octagon twist, and
  the cheap first move is the guard rather than either.

  **Size, measured on the full QF_LIA SMT-LIB division** (thesis Fig. 7.27):
  6806 solved with the unit cube test vs. 6750 without (+56 at a 40-minute
  limit), over a population of 1483 absolutely unbounded instances -- 229/233
  `dillig`, 503/591 `CAV_2009_benchmarks`, 229/233 `slacks`, 503/806
  `20180326-Bromberger`, 19/37 `prime-cone`, all families Primo runs. The
  larger structural effect is that it removes the need for branch-and-bound
  altogether on 1271 instances (641 need branch-and-bound with the test, 1912
  without).

  **Falsifier**: a census showing that, after applying the two skip conditions,
  fewer than ~5% of QF_LIA final-check rounds on the sampled corpus reach a
  cube-eligible state. That would say the equality-basis prerequisite is doing
  all the work and cubes should be folded into lead 17 rather than pursued
  separately.
- **Extended GCD test (Sect. 4.3)** -- the smallest remaining item and
  the best-scoped: see 12e below.
- **Diophantine (Omega/Pugh) and HNF cuts** -- still large, still
  unstarted, and still the pieces z3's `arith-dio-*`/`arith-hnf-calls`
  counters actually attribute its calypto performance to.

The Sect. 4.5 cut refinements Primo lacks (relaxed "row-integral" Gomory
rows, cut selection by distance to nearest integer, extremum bound
deduction, large-numeral temporary scope) are enumerated in the paper
note's gap table. The relaxed row definition is the one with direct
measured evidence behind it -- Primo's instrumented `f_unbounded` count is
exactly the eligibility failure it fixes -- and it also changes the
antecedent rule stage 4's `LiaExplanation` uses, so it should not be
treated as an isolated tweak.

(lead-qflia-patching)=
### 12f. Patching (paper Sect. 4.1) -- done

**Claim.** Primo's integer waterfall began at the paper's *third* layer.
Patching, its first, nudges nonbasic values so Int variables land on
integers, turning an LRA model into an integer one with no search.

**Evidence that this was the right target.** The full corpus splits the
gap by answer: against cvc5 Primo is **ahead by 147 on unsat and behind by
661 on sat**. Every layer stages 1-4 built is refutation machinery, so the
entire deficit sat where none of them could help. Patching was chosen over
the cube test on a measurement -- see 12c.

**Outcome (2026-08-21).** Shipped default-on as stage 5. Full corpus
(13,306 files, 60 s, Slurm 128125), same binary with `--lia-patch-limit 0`
as the control: **8,727 -> 9,027 solved, PAR2 42.91 -> 40.20, 0 wrong, 0
answer disagreements**. +300 net (356 gained, 56 lost), and **331 of the
gains are satisfiable** -- the population the whole cvc5 deficit sat in.
The gap to cvc5 narrows from -535 to **-212**. Model validity confirmed by
Dolmen at scale (3,494 sat models, all pass, Slurm 128295).

The 56 losses are a genuine cost rather than boundary noise: 19 of them
were solved in under a second by the control. Patching occasionally sends
an easy instance somewhere much worse, the same trajectory-change
pathology 12d's `rings` regression showed. Bounding it is open follow-up.

Two things the paper's prose leaves implicit, both found by building its
own Example 3 as a test rather than trusting the derivation: scoring a
move by `|delta|` alone oscillates (the nearest fix for one row breaks
another, and the two undo each other), so candidates are scored globally
and only strictly improving moves are taken; and floor/ceil alone is too
narrow a candidate set, since Example 3's answer `x = 6` is two integers
away from the current value. See [](qf-lia-branch-and-bound.md)'s
"Patching".

First QF_LIA stage to touch `LraTableau`, deliberately and minimally:
`set_nonbasic_value`, a narrow public wrapper over the existing private
`update()`, whose comment carries the trail caveat that makes it safe only
on a per-round tableau copy.

(lead-qflia-extended-gcd)=
### 12e. Extended GCD test for bounded-but-not-fixed rows

**Claim.** Bjørner & Nachmanson Sect. 4.3 extends the basic GCD test from
*fixed* variables to merely *bounded* ones: for a row `a.x + b.y + c = 0`
with `x` bounded, if the achievable range `[l, u]` of `a.x` contains no
value congruent to the required residue modulo `gcd(b, c)`, the row is
infeasible. Primo's stage 2 implements only the basic test, because
`fixed_value()` requires equal lower and upper bounds.

**Evidence.** This closes the exact divergence
[](qf-lia-branch-and-bound.md) documents as caught by neither stage 2 nor
stage 3, and which `gcd_uncatchable_divergent_system_hits_the_node_limit`
pins as a test: `4x + 4y` bounded to `[2,3]` with `x, y` unbounded. The
slack is bounded but not fixed, so nothing currently fires and the search
diverges to `--lia-branch-limit`.

**Cost.** Small -- an extension to `row_is_gcd_consistent`, in the same
shape as the existing residual computation, plus the corresponding
antecedent rule in `LiaExplanation` (the bounds used become the
explanation, rather than the pair of equal bounds the fixed case uses).

**Falsifier.** A population census: if almost no real QF_LIA row is
bounded-but-not-fixed with an unbounded integer remainder, this closes a
crafted-benchmark case and nothing else. Worth counting before building,
the same way 12b is gated.

**Watch the printed inequality.** The paper renders the condition as
`floor(u/gcd) > ceil(l/gcd)`, which is backwards -- that direction reports
infeasibility precisely when a solution exists. The intended test is
`floor(u/gcd) < ceil(l/gcd)`. Verified against the paper's own shape in
[](papers/arithmetic-solving-in-z3.md); do not transcribe it as printed.

**Status.** Not started. Unlike 12c's remaining pieces this needs no new
paper note -- Sect. 4.3 is already summarized.

**Caveat shared with everything in lead 12.** An instrumented run found 24
of 36 sampled QF_LIA timeouts never execute a single integer round, so no
Section 4 layer can reach that population; their bottleneck is the Boolean
and LRA search. Sect. 3.3's indirect bounds propagation is the part of the
paper aimed at them.

(lead-qflia-conflict-generalization)=
### 12d. Conflict-clause generalization (Farkas certificates / CDCL-integrated branching)

Already named as a known gap in [](qf-lia-branch-and-bound.md)'s "Where
branching lives" section: every arithmetic conflict blocks the whole
current assignment rather than a minimized explanation. The calypto
evidence above (56,864 near-1:1 lra/sat conflicts on one file) is the
first concrete measurement of its cost, not just a theoretical concern.

**Status: done (2026-08-21), and it turned out to be neither large nor to
need a design note.** The estimate above was wrong in a specific and
instructive way: it assumed the explanation had to be *built*. It did not
-- `LraTableau` already returns a Farkas-style conflict certificate from
`assert_upper`/`assert_lower`/`check()`, and QF_LRA has always used it
directly as its theory lemma (`check_lra_theory_in_place` returns
`tableau.check()` as the clause). The QF_LIA path was calling exactly the
same tableau operations and *discarding* what they returned:
`apply_gomory_cuts` collapsed a `std::vector<sat::Lit>` conflict to
`bool`, `gcd_inconsistent_row` returned only a row id, and `try_branch`
returned only a feasibility flag. The coarse clause was then rebuilt from
scratch out of every arithmetic atom.

**What the discarded certificate was worth.** Measured by instrumenting a
build and running the per-family sample (3 files/family, 94 files, 10s):

| instance | rounds | coarse lemma | tableau conflict | cut antecedents |
|---|---|---|---|---|
| `calypto/problem-001542` | 6,528 | 219 lits | 5 lits | 4 bounds |
| `convert/convert-jpg2gif-query-1141` | 19,630+ | 1,266 lits | 5 lits | ~19 bounds |

Every conflict contained exactly one `sat::Lit{0}` -- the synthetic reason
the cut's own threshold bound was asserted with. That single literal is
the whole reason the certificate could not just be forwarded, and
resolving it away is what the fix is.

**The fix.** A new `LiaExplanation` accumulator
(`src/theory/arithmetic/lia_explanation.{hpp,cpp}`) threaded through all
three integer stages, collecting conflict literals and dropping
`kDerivedBoundReason`. Dropping it is sound because each stage supplies
what its derived bound rests on: a Gomory cut contributes the bound
reasons of its row's terms (the cut is implied by them; the row equation
is a tableau invariant, not an assumption), while a branch-and-bound split
needs nothing at all, since `x <= n` or `x >= n+1` is a tautology over the
integers and the union of both refuted sides already refutes the parent.

Soundness does not rest on those arguments being right, though.
`generalized_lia_lemma()` in `check_sat.cpp` **checks** that every literal
of the derived explanation already appears in the coarse clause, and falls
back to the coarse clause otherwise. A superset of the true explanation is
always a valid (if weaker) lemma; a literal that is *not* blocking for the
current assignment would fail to exclude it and spin the outer loop
forever -- the containment test rules that out, and rules out a leaked
synthetic literal at the same time. An empty explanation also falls back,
since the empty clause claims the whole problem is unsat.

**Result** (same instances, 60s):

| instance | coarse | generalized |
|---|---|---|
| `calypto/problem-001542` | unsat, 1.10s | unsat, **0.02s** (6,528 rounds -> 2) |
| `calypto/problem-001553` | unsat, 0.86s | unsat, **0.02s** |
| `convert/convert-jpg2gif-query-1141` | **unknown, timeout** | unsat, **0.04s** (19,630+ rounds -> 6) |

Zero generalization fallbacks on any of them. Kept switchable via
`--no-lia-conflict-generalization` (default on) so the coarse form stays
A/B-measurable, and four `:lia.*` statistics report lemma sizes and
fallback counts.

**Full corpus (Slurm 127949, both arms of the same binary, 60s):** 8,706
solved coarse -> 8,725 generalized, PAR2 43.12 -> 42.94, 0 wrong, 0 answer
disagreements. The coarse arm reproduces the pre-change baseline (8,704),
which is what makes +19 attributable to this change. 27 of 30 gains solve
in under 5s; 9 of 11 losses were already over 45s and are boundary noise.

One real regression, and it is informative:
`rings_preprocessed/ring_2exp4_4vars_3ite_unsat.smt2` goes 0.18s ->
unsolved at 300s. Generalized needs *fewer* rounds (814 vs 995) but each
costs ~2,000x more, because a 23-literal clause propagates hard where the
91-literal coarse clause was nearly inert. A stronger lemma is not free.
Gating the derived lemma on how much smaller it actually is, is the
obvious refinement -- worth its own lead if anyone picks it up.

This does not close the cvc5 gap (-535 -> -514), and it was never going to:
24 of 36 sampled timeouts never execute a single LIA round. Notably
primo still solves 553 files cvc5 does not, so the two are strongly
complementary -- see [](#complementarity).

Griggio's thesis was not needed after all -- but the note stands for
whoever takes 12c, where the same "check what the existing machinery
already hands back" question is worth asking before designing anything.

(complementarity)=
(lead-qflira)=
## 13. QF_LIRA -- the search core is already mixed, only the frontend is missing

**Status: open, added 2026-08-26.** Analysis only; nothing measured.

**Claim.** Supporting mixed `Int`/`Real` linear arithmetic needs no new
solving algorithm. Every QF_LIA stage is already parameterized by *which*
variables are integer rather than assuming all of them are, because
scale-canonicalized slacks are non-integer variables sitting in rows with
`Int` variables -- so Primo already solves mixed tableaus on ordinary
QF_LIA input. The work is entirely in the frontend. Full analysis, with
the per-stage behaviour and the exact blockers, in
[](qf-lia-branch-and-bound.md)'s [](#qf-lira) section.

**Evidence.** Read from the code, not run -- no QF_LIRA input can reach
the solver. Branch-and-bound splits only over `integer_vars`;
GCD-consistency checks `is_declared_integer[]` per row term and declines
to conclude when a free variable is not integer (a guard added after
mutation fuzzing found a false conflict from exactly this in August);
Gomory uses King's *mixed* cut, which requires integrality of the basic
variable only; patching moves any non-basic within its freedom interval,
and a Real non-basic is the ideal mover. The Sat condition is already
`all_of(integer_vars, ...)`, not "the whole assignment is integral".

**Cost.** The frontend is the project. Ranked by what actually bites:
context-sensitive numeral sorting (today a literal is `Int` iff the logic
is `QF_LIA`, so `(+ x 1)` must instead take its sort from `x`), then
`to_real`/`to_int`, then mixed-sort operands in `mk_plus`/`mk_times`,
then the logic whitelist and a model printer that emits both sorts.
Cut *strength* is a separate, optional follow-up: `apply_one_cut` treats
every non-basic as continuous, forgoing mixed-integer-rounding
strengthening on `Int` non-basics -- valid, weaker, already recorded as
out of scope.

**Falsifier, stated before the work.** A unit test that builds a mixed
tableau directly (`Int` `x`, `Real` `y`, shared rows) and drives
patching, GCD-consistency, cuts and branch-and-bound against it, asserting
the existing stage post-conditions (`check()` still feasible; every `Int`
variable integral on Sat). If any stage assumes total integrality, that
test fails and this lead is wrong about the core -- which would make the
frontend work pointless to start. **Run the falsifier first**: it costs an
hour and gates a project.

(lead-monotone-int)=
## 14. Monotone (one-sided) elimination for `Int`, not just `Real`

**Status: open, added 2026-08-26.** Follows from [](#lead-monotone), whose
pass is `Real`-only for no stated reason.

**Claim.** The one-sided elimination rule is sound over `Int` by exactly the
argument that makes it sound over `Real`, and the implementation restriction
is a sort predicate rather than an algorithmic one.

**Why it transfers.** The rule is Fourier-Motzkin on a single-sign variable:
if every atom mentioning `x` pushes it the same direction, some extreme value
of `x` satisfies all of them at once, so each atom can be replaced by the
constant its own occurrence needs. The only property of the domain that
argument uses is that it is *unbounded in the pushing direction*, and `Z` is
unbounded in both. The saving is also the same shape -- mostly Boolean, since
the atom leaves the CNF and the clauses it satisfies disappear.

**What would change.** `is_real_constant` in `preprocessing.cpp` gates
candidacy on `sort == real_sort()`, and it is used in exactly two places.
Model reconstruction is the part that needs actual thought rather than a
widened predicate: `MonotoneEliminatedVariable` re-derives a satisfying value
from the discharged constraints, and for an `Int` variable that value must be
an *integer* in the satisfying ray. The ray is unbounded, so one always
exists, but the reconstruction has to round into it rather than take the
rational extreme -- `delta_floor`/`delta_ceil` in
`theory/arithmetic/lia_integer_values.hpp` are the existing helpers, and
scale-canonicalization means an `Int` variable's bound may itself be
fractional (`(<= (* 3 x) 5)` stores `5/3`), so the rounding has to be done
against the bound rather than assumed integral.

**What would break it, and does not exist here.** A divisibility constraint on
`x` (`div`, `mod`, `divisible`) makes the ray no longer uniformly satisfying:
pushing `x` far in one direction does not preserve `x mod 3 = 1`. Primo
implements none of those, and they are listed as out of scope in
[](qf-lia-branch-and-bound.md). Any future work adding them, or adding
QF_LIRA's `to_int` ([](#lead-qflira)), must disqualify variables under them --
the same way the pass already disqualifies a variable occurring under an
uninterpreted application.

**Evidence: none yet, and one number that must not be misread.** Sampling
`--monotone-elimination --stats` across the corpora on 2026-08-26 gave 24 of
48 QF_LRA files firing (2,157 variables eliminated), 4 of 35 QF_UFLRA files,
and **0 of 58 QF_LIA files**. That zero is definitional, not empirical: the
pass refuses `Int` candidates before counting them. The first step of this
lead is therefore to measure *candidate density on QF_LIA at all*, either by
relaxing the predicate behind a flag or by an offline scan, before any
performance question is asked.

**Sequencing.** [](#lead-monotone) is currently being re-tested at full
QF_LRA scale (300s, 1,753 files) after its rejection on a 237-file 10s
sample. If the `Real` pass does not clear the bar there, extending it to `Int`
buys a wider domain for a mechanism that does not pay -- so wait for that
result before starting. The QF_LIA fragment is also the one where the profile
puts all integer-specific stages at 0.1% of runtime, which bounds the upside
of anything acting *inside* the integer search; this pass acts before it, on
the Boolean structure, which is the reason it is worth asking about at all.

**Falsifier, stated before the work.** An offline scan of the QF_LIA division
counting `Int` variables that occur one-sidedly under the pass's own
conservative disqualification rules. If the median instance has zero such
variables -- as the QF_LRA-shaped intuition may not survive contact with
`slacks`, `convert` and `miplib` -- the lead closes on a measurement that
costs an afternoon, with no solver change written.

(lead-boolean-bridge-exclusion)=
## 15. Delete the `bridge-atoms` Boolean-domain encoding

**Status: open, added 2026-08-26, rewritten the same day after the QF_UF
cardinality fix (`fa44295`) landed.**

**Claim.** `EufBooleanDomain::BridgeAtoms` should be deleted, along with
`materialize_boolean_argument_literals`, `ensure_boolean_domain_bridge`,
`ensure_boolean_domain_constants` and the `--euf-boolean-domain` flag. The
default `Registration` encoding supersedes it: same information to congruence
closure, no SAT variable and no clause per term instead of 2 atoms and 3
clauses, and it does not bridge Bool-VALUED applications, which are already
theory atoms.

**Evidence already in hand.** Both encodings were A/B-ed on one binary
(2026-08-26). QF_UF division, 7,503 files at 60s: zero answer disagreements,
and registration 3% faster over the 881 files both solve in more than a second.
QF_UFLRA, 1,284 files at 120s: 1,272 answered in both arms, identical 761/511
split, zero gained, zero lost, zero disagreements. The Bool fuzz gate is clean
under both. See `docs/run-log.json`'s
`2026-08-26-qf-uf-boolean-cardinality-fix`.

**The original form of this lead is now moot.** It asked whether the bridge's
third clause -- `(not (t = true)) or (not (t = false))` -- was redundant. It
is: asserting both merges `true` with `false`, which the unit disequality
already forbids, so EUF raises that conflict itself. Removing it kept 20/20
suites, all eight QF_UFLRA bool seeds and a 200-case differential run
unchanged, with conflicts over those 208 files going 270 -> 223. But the clause
only exists inside the encoding this lead now proposes to delete outright, so
the narrower question does not need answering separately.

**Cost.** Small: a deletion plus removing one option and its plumbing. The only
reason it is not already done is that the flag is what makes the A/B above
reproducible, and deleting it in the same commit as the fix would have made a
later regression harder to bisect.

**Falsifier.** None needed beyond what is recorded -- the measurement that
would have justified keeping `bridge-atoms` has already been taken and came
out flat. If anything, delete it only after one more full QF_UFLRA run at the
next opportunity, so the deletion rests on two independent readings rather
than one.

(lead-ite-preprocessing)=
## 16. If-then-else preprocessing: CITE simplification, compression, reconstruction

**Source.** [](papers/bromberger-linear-arithmetic-thesis.md) §8.2.3, read
2026-08-29. Techniques credited there to Kim/Somenzi/Jin (constant ITE
simplification), Burch (ITE compression) and CVC4 (ITE reconstruction, no
publication).

**Claim.** Primo's frontend eliminates non-Boolean ITEs with a fresh constant
plus two guarded equalities ([](preprocessing.md), "ITE Lowering"), which is
exactly the encoding the thesis identifies as structure-destroying -- to the
point that it implements a pass to *undo* it on inputs where someone else
applied it. Three techniques repair this, and the thesis measures that **two of
them only work together**.

**Size, measured.** All of SPASS-SATT's preprocessing on vs. off, full QF_LIA
SMT-LIB division at 40 minutes: **6806 vs. 5113 (+1693)**, plus >2× faster on
3146 further instances. This is the largest single number in any paper this
project has read; for comparison the entire branch-and-bound extension suite in
the same document is +272. On QF_LRA the same techniques are worth +2, so this
is a QF_LIA lead only.

Per family, and every one of these is in Primo's corpus:

| family | files here | thesis with | thesis without |
|---|---:|---:|---:|
| `nec-smt` | 2780 | 2772 | 1422 |
| `rings` | 294 | 294 | 136 |
| `rings_preprocessed` | 294 | -- | -- |
| `pb2010` | 81 | 66 | 44 |

**Why the shape matches.** Local census 2026-08-29
(`2026-08-29-qflia-ite-let-census` in [](run-log.md)): **2780 of 2780 `nec-smt`
files contain both `(let` and `(ite`**; `large/checkpass_pwd/prp-24-38.smt2` is
200 KB with 886 `let`s and 5240 `ite`s inside a *single* `assert`.
`rings_preprocessed` has **0** files containing `(ite` against `rings`'s 245 of
294, confirming the thesis's account that the two families are the same problems
with the ITEs pre-eliminated. `pb2010` has neither construct, so its gain comes
from the separate pseudo-boolean pass.

**The three pieces.**

1. **Constant if-then-else simplification (CITE).** Push constant monomials and
   coefficients to the leaves of an all-constant-leaf ITE tree, then push a
   comparator recursively down the branches, collapsing any branch whose leaves
   all decide the comparison the same way. Needs two memo caches (leaf sets per
   shared term, `csimp` results per `(op, lhs, rhs)`) or it is exponential.
2. **ITE compression.** Chains `(ite c_i t_{i+1} false)` / `(ite c_i false
   t_{i+1})` are conjunctions. Compress *selectively* -- extend the chain only
   through subterms occurring once, then name the result with a fresh
   propositional variable, so the multiply-occurring tail keeps its sharing.
3. **ITE reconstruction.** Detect at least two of
   `{(or c (= y t3)), (or (not c) (= y t2)), (or (= y t2) (= y t3))}`, delete
   them and substitute `y := (ite c t2 t3)` back in, then apply 1 and 2.

**Cost.** Medium-large. It is frontend/preprocessing work rather than theory
work.

**Update 2026-09-04: the ordering constraint that was this lead's main design
question is gone.** It read: "Primo's core term language does not store ITE
nodes at all, so piece 1 has to run before lowering while piece 3 has to run
*after* parsing an already-lowered input." `core::TermKind::Ite` now exists and
the frontend builds it, with lowering moved into an unconditional
`lower-extended-terms` leaf at the head of the initial sequence. So pieces 1 and
2 are ordinary term rewrites that run before that leaf, on the same
representation, and only piece 3 -- reconstruction, which recognises an
*already lowered* encoding in the input and rebuilds the ITE -- still has to run
on lowered shape. Sequencing is no longer the obstacle; the obstacle is now just
the size of the three techniques.

Two further things the same change makes available, neither of which existed
when this lead was written: the ITE node is hash-consed, so identical ITEs
share one auxiliary instead of one per occurrence; and `(ite c a a)` and a
constant condition fold in the constructor, which they previously could not.

**Update 2026-09-05: the first rule of this kind Primo shipped lost solves, and
the reason is one this lead already names.** Making ITE a term came with an
equality distribution, `(= u (ite c a b))` to
`(and (=> c (= u a)) (=> (not c) (= u b)))`, which avoids the auxiliary. Over
the full QF_LIA division it made the preprocessed formula **10.11% larger on
`nec-smt`** and 0.00% different on almost every other family, for a 2.7%
reduction in auxiliary constants, and cost about 44 solves at 60s
(`2026-09-05-ite-loss-attribution-131983` in [](run-log.md)). Three things
carry over to the work below, and none of them are hypothetical any more:

1. **Sharing is the whole game.** Lowering pays once per hash-consed ITE and
   then one equality per parent; distribution pays the guard structure per
   parent. The rule now declines any ITE with more than one parent edge. That
   is piece 2's "extend the chain only through subterms occurring once" arriving
   early, and it is not a refinement -- it is the difference between the rule
   helping and hurting.
2. **Do not iterate to a fixed point.** Running the distribution to closure is
   exponential: 116s and 23GB on
   `nec-smt/large/getoption_directories/prp-4-113.smt2` against 3.0s with the
   rule off, converging only after 32 rounds. CITE's memo caches are described
   in this lead as an optimization; on this family they are a termination
   requirement.
3. **Auxiliary count is not the objective.** The 824-file A/B that promoted the
   ITE term change measured auxiliaries, saw them fall, and shipped. They fell
   while the formula grew. Measure size and solves.

**And a fourth, learned the expensive way: the size proxy is not sufficient
either.** Switching the rule off globally was measured on preprocessed size,
looked strictly better, and lost 29 solves on `20210219-Dartagnan` -- a family
where the rule is nearly free on size and removes 30% of the auxiliaries. The
rule ended up guarded on *shape* rather than switched off: it fires only when
neither branch is itself an ITE, which is exactly when the ITE actually goes
away. Whole-division solve counts against the pre-change binary, one corpus,
three defaults:

| default | overall | `nec-smt` | `20210219-Dartagnan` |
|---|---:|---:|---:|
| distribute always | -44 | -50 | +6 |
| never distribute | -24 | +8 | -29 |
| **distribute only a flat ITE** | **+20** | **+15** | **+6** |

The lesson for the three pieces below is that a rewrite of this family has a
*range* where it pays, its edges are structural rather than numeric, and
neither a size proxy nor an auxiliary count locates them. Only solve counts on
both an ITE-nested and an ITE-flat family do.

**Update 2026-09-06: piece 2 is mostly already implemented, and piece 1 now
exists behind a flag.**

Piece 2's *semantic* content is in Primo already. A three-deep
`(ite c t false)` chain compresses fully today -- three rewrites, no
auxiliaries, four clauses -- through the Boolean-ite rules in
`simplify-extended-terms` plus hash-consing. What the thesis adds on top is
selectivity (extend a chain only through subterms occurring once) and naming
the result with a fresh propositional variable; hash-consing already supplies
what the naming is for, since the compressed conjunction is one node however
many parents it has. So "implement 1 and 2 together" turned out to mean
"implement 1, and make sure it runs before the rules that already do 2".

Piece 1 is `--cite-simplification` (default off), `simplify_constant_ites` in
`preprocessing.cpp`. Two departures from the thesis, both because the integers
make its presentation unsound: it normalises a CITE by pushing constants to the
leaves, whereas this pushes the *comparison* down and hoists an ite above a
scale or sum only where that is an identity, so no coefficient is divided out.
The caches are not optional -- keying the csimp cache per atom instead of on
`(op, term, constant, side)` across the pass cost 183 seconds against 2.8 on
`nec-smt`'s largest file.

**What it does to the formula is genuinely two-sided**, which is why the
falsifier below is a solve measurement and not a size one. On
`nec-smt/large/getoption_directories/prp-4-113.smt2`: ite auxiliaries
40284 -> 10026, a 75% cut, and preprocessed size 443762 -> 1280886, a 190%
growth, with preprocessing alone going from 2.8s to 27s. The growth is
inherent rather than an ordering artefact: an atom that only partly decides
becomes a Boolean tree mirroring the CITE.

**Outcome 2026-09-06: the falsifier ran and the lead clears its bar by a factor
of three.** Job 133177, same binary, `--cite-simplification` on versus off, all
2780 `nec-smt` files at 60s:

| | |
|---|---:|
| base solved | 644 |
| CITE on | **1269** |
| delta | **+625** |
| gained / lost | 625 / **0** |
| bar | +200 |

Zero lost, zero wrong answers, zero disagreements -- and the correctness claim
is the checkable kind, because every one of the 2780 files carries a declared
`:status` (1524 unsat, 1256 sat) and all 1269 answers were compared against it.
On the 577 files both arms solve with base above a second, the paired runtime
geometric mean is 0.589 and total time falls 38.5%, so the 190% formula growth
and the tenfold preprocessing cost are bought back several times over.

For scale: the thesis reports 1422 without against 2772 with for *all* of
SPASS-SATT's preprocessing on this family. Piece 1 alone takes Primo from 644
to 1269.

**The full division confirms it and shows no cost elsewhere** (job 133466,
13,306 files, one binary): off 9061, on **9660**, **+599**, with 601 gained
against 2 lost and no wrong answers. Only `nec-smt` moves. Paired runtime is
0.612 there with total time down 36%, and **1.0045 on every other family** with
total time up 0.9% -- neutral where it does not apply rather than harmful,
which is the check the ite equality distribution earlier on this branch failed.

Promoted to default in `d8462db`; `--no-cite-simplification` keeps the other
arm reachable. Piece 3, reconstruction, is not implemented and is what is left
of this lead. Its natural target is `rings_preprocessed` -- 294 files, none
containing `(ite` -- against `rings`, the same problems with the ITEs already
eliminated by whoever produced them.

**Falsifier, as stated in advance.** Implement CITE simplification and
compression together (the thesis is explicit that either alone is worse than
neither), then measure on the 2780 `nec-smt` files with the same-binary control
the stage 4 evaluation established. **If the pair does not gain at least 200
solves on `nec-smt` at 60s, close this lead** -- the thesis's own claim is that the
technique turns those instances into ones the simplex layer alone decides, so a
small gain means Primo's bottleneck on that family is somewhere else and the
next step is a profile, not a bigger ITE pass.

**Do not** measure piece 1 or piece 2 alone against the promotion bar and close
the lead on the result. Figures 8.15 and 8.16 report that single-technique
configurations are *worse* than no preprocessing at all on this family.

(lead-equality-basis)=
## 17. Complete implied-equality basis

**Source.** [](papers/bromberger-linear-arithmetic-thesis.md) Chapter 5, read
2026-08-29 (the thesis form of Bromberger & Weidenbach, SMT@IJCAR 2016, which
[](papers/lra-related-work.md) had carried from the abstract only since
2026-07-31).

**Claim.** [](qf-lra-equality-sharing.md) reaches completeness at quiescence by
testing each unresolved shared pair in **both strict orientations** against a
reused `PreparedLraTheory`. The thesis reaches the same completeness with:

- one feasibility check on the system with every bound made strict
  (Lemma 5.2.2 -- **satisfiable means no equality is implied at all**, which is
  a whole-system negative test, not a per-pair one);
- the simplex conflict explanation read as an equality set (Lemma 5.2.3 -- one
  infeasibility yields every tight variable in the row, not one candidate);
- normalisation of every variable through the substitution the tableau already
  encodes, after which **entailed equality is syntactic identity of normal
  forms**, so hashing replaces probing (§5.4.1).

Justification clauses are given explicitly and are minimal (§5.4.2), including
the non-obvious filter that a tight bound `z_j` with `a_ij = a_kj` is redundant
and must be dropped.

**Why the transfer looks safe.** The `Initialize()` step "pivot as many `L = U`
variables out of the basis as possible" is exactly the mechanism
[](qf-lra-equality-sharing.md) measured its way to under the name
`--lra-fixed-elimination`, pinned by
`fixed_elimination_closes_the_eqrow_basis_gap`. Primo already found one named
step of this algorithm empirically.

**Incrementality is the load-bearing property**, and it is argued rather than
assumed (§5.3.2): `Check()` never pivots a non-basic `z_j` with
`L(z_j) = U(z_j)` back into the basis, so tight variables found in an earlier
round stay non-basic and are never recomputed. Asserting bounds can only add
tight variables.

**Known limitation, from §5.6.** The method finds only equalities implied over
the **rationals**. Integer- or mixed-entailed equalities that are not rationally
implied are outside it. Fine for QF_UFLRA; a real constraint on any integer
extension.

**Cost.** Medium-large: a second bound-state layer over `LraTableau` plus the
justification-clause machinery. It is *not* a new entailment rule alongside
`FixedEq`/`EqRow`/`EqRows`; it replaces the exact fallback.

**Falsifier.** Instrument the current QF_UFLRA path for the number of exact
fallback probes per fixpoint round on the mixed corpus. **If the median round
issues fewer than ~5 probes, close this lead** -- the opportunistic rules plus
model reconciliation are already doing the work and a complete basis buys
determinism nobody is paying for. The prize is the tail, so report the
distribution, not the mean.

**Also unblocks**: [12c](#lead-qflia-dio-hnf-cube) (cubes need the equalities
eliminated first) and [19](#lead-bounding-transformations) (the bounded/unbounded
classification *is* an equality basis of the homogeneous system, Lemma 5.5.1).

(lead-hard-backtrack)=
## 18. `HardBacktrack`: revert the bounds, recompute the assignment

**Source.** [](papers/bromberger-linear-arithmetic-thesis.md) §7.1.3.

**Claim.** Dutertre & de Moura's `Backtrack()` reverts bound values *and*
restores a backed-up assignment. `HardBacktrack()` reverts only the bounds and
lets `Check()` recompute. It is more expensive per call and lands in a better
state, because

> "a good continuation point for the simplex algorithm depends not only on the
> current assignment, but also on the current tableau ... Reverting to the
> backup solution ... sometimes disconnects the assignment and the tableau."

CVC4 and veriT are reported to do the same.

**Size, measured.** QF_LRA 1607 vs. 1602; QF_LIA **6806 vs. 6683 (+123)**; and
on `convert` alone **319 vs. 202 (+117)**. `convert` is 319 files here and a
family where patching already gained 57.

**Cost.** Small, and it is a *deletion* -- stop restoring the backup assignment.
The risk is the opposite of the usual one: the cost is per backtrack and
therefore hits the instances that backtrack most, so the A/B must report gained
and lost separately per [](#complementarity), not just the net.

**Falsifier.** Run the A/B on the stratified sample plus the full `convert`
family. **If `convert` does not gain and the sample is net-negative, close it.**
The thesis's own QF_LRA number (+5) is within its stated cluster noise, so a
QF_LRA-only result decides nothing either way.

(lead-bounding-transformations)=
## 19. Bounding transformations: make branch-and-bound terminate

**Source.** [](papers/bromberger-linear-arithmetic-thesis.md) Chapter 6.

**Claim.** Branch-and-bound diverges on *partially unbounded* problems -- some
directions bounded, some not. `1 <= 3x1 - 3x2 <= 2` is the two-variable witness.
Two transformations, applied only to that class, produce an equisatisfiable
bounded problem on which branch-and-bound is complete:

- **Double-Bounded reduction**: split into an absolutely unbounded part and a
  double-bounded part, then *drop the unbounded part* -- it can shrink the
  solution set but never empty it.
- **Mixed-Echelon-Hermite transformation** (Christ & Hoenicke): shift variables
  so the coefficient matrix is lower triangular with gaps, after which every
  variable is bounded or absent.

**Size, measured.** 6806 vs. 6637 (**+169**) on the full QF_LIA division, of
which **162 are `20180326-Bromberger`** (806 files here, containing 303
partially unbounded problems), plus 1 `arctic-matrix`, 3 `cut_lemmas`, 1
`slacks`, 2 `tropical-matrix`. Overhead where not applicable is described as
almost immeasurable because the applicability test is cheap.

**Relation to existing pins.** This is the class
`gcd_uncatchable_divergent_system_hits_the_node_limit` and the
`--lia-branch-limit` divergences live in. [12e](#lead-qflia-extended-gcd) closes
one such instance with a cheap test; this closes the class, at much higher cost.

**Cost.** Large -- and it needs the multiple-tableau discipline of §7.2.4
(a *static*, never-pivoted tableau alongside the dynamic one, because the
transformation needs the original coefficient matrix that pivoting has
destroyed; five instances in total for a partially unbounded problem).

**Gated on [17](#lead-equality-basis)**, not optionally: deciding which
inequalities are bounded is done by computing an equality basis of `Ax <= 0`
(Lemma 5.5.1), which is one equality-basis computation instead of `m` linear
programs, and it is what keeps the transformation off the bounded problems it
would slow down.

**Falsifier, cheap and to be run first.** Classify the `20180326-Bromberger`,
`arctic-matrix`, `cut_lemmas`, `slacks` and `tropical-matrix` families into
bounded / absolutely unbounded / partially unbounded, and cross it with which
of them Primo currently fails. **If the partially unbounded instances Primo
fails number fewer than ~50, close this lead** -- +169 at 40 minutes does not
transfer to +169 at 60s, and the classification census is the honest way to size
it before building anything.

(lead-decision-recommendation)=
## 20. Evaluate `--lra-model-phase` on QF_LIA and consider promoting it

**Rewritten 2026-08-29 after reading the CADE 2019 paper.** The first version of
this lead, written from the thesis prose alone, said Primo had "half" of decision
recommendation and that the missing half was the stronger one. The paper's
footnote 3 says that is backwards, and the lead is now a different and cheaper
piece of work: not "add the missing criterion" but "measure the mechanism we
already have, on the fragment where it was measured to matter."

**Source.** Bromberger, Fleury, Schwarz, Weidenbach, *SPASS-SATT: A CDCL(LA)
Solver*, CADE 2019, §3 and **footnote 3**; fuller prose in
[](papers/bromberger-linear-arithmetic-thesis.md) §8.1. Full citation guidance,
including what not to cite, is in
[](h5-attribution).

**The rule, exactly** (footnote 3): `GetPhase(L) = ⊤` if the current theory
assignment satisfies `L`, **or** if `Assert(¬L)` would conflict; otherwise `⊥`.
And: "If unate propagations are enabled, then the case that `Assert(¬L)` would
return `⊥` is impossible because `L` would have been unate propagated."

**Why that changes the lead.** SPASS-SATT runs unate propagation by default, so
their measured numbers come from the *model-satisfaction* disjunct alone --
which is precisely what `--lra-model-phase` already does. So the published
figures are an estimate of what **Primo's existing H5 is worth**, not of a gap:

| | with | without |
|---|---:|---:|
| QF_LIA | 6806 | 6677 (**+129**) |
| QF_LIA `convert` | 319 | ~212 (**+116**) |
| QF_LRA | 1607 | 1596 (+11) |

Also >2x faster on 389 QF_LIA instances against 58 the other way.

**What makes this cheap.** `--lra-model-phase` is off by default and has only
ever been evaluated on QF_LRA, where the same source puts the technique at +11 --
i.e. Primo measured the mechanism on the fragment where it is worth almost
nothing. `solve_qf_lia_impl` already constructs `OnlineLraPropagator` with
`options.lra_model_phase` (`check_sat.cpp`), so **the QF_LIA evaluation needs no
new code at all** -- it is an A/B of an existing flag.

**Falsifier.** Run `--lra-model-phase` on/off over the full QF_LIA corpus with
the same-binary control the stage 4 evaluation established. **If `convert` does
not gain and the corpus is net-neutral or worse, close this lead** and leave the
flag off; the mechanism will have been measured on both fragments and lost on
both.

**There is no leftover** (corrected 2026-08-29). This lead briefly claimed that
Primo lacks unate propagation -- from `grep -rn unate src/` being empty -- and
that footnote 3's unreachability argument therefore did not transfer. Wrong:
Primo's *same-variable bound propagation* (`--lra-propagation`, **default on**)
is unate propagation under the other name Dutertre and de Moura give it. So the
`Assert(¬L)`-conflicts disjunct is unreachable here too, and `--lra-model-phase`
is the entire technique rather than a part of it. Nothing further to implement;
only to measure.

**Suspect staleness before suspecting the technique.** Their `GetPhase` is
pulled by the SAT solver at decision time and is always current; H5 pushes hints
at quiescence points and can be stale. `cb_decide` would allow the pull form if
the push form underperforms.

(convert-cluster)=
### The `convert` family: three techniques, one set of instances

Worth stating separately, because the paper answers a question this register had
left open. Three of these leads name `convert`: this one (+116),
[18](lead-hard-backtrack) (+117), and small CNF (+101). The note previously asked
whether those are the same instances. **They are, and the paper is explicit that
the three are conjunctive rather than additive:**

> "SPASS-SATT solves only two-thirds of the 319 instances if **one** of the
> following three techniques is missing: (i) recalculating simplex states during
> backtracking, (ii) decision recommendations, or (iii) the small CNF
> transformation. SPASS-SATT with all three techniques solves all instances in
> less than 2 s."

So each is individually *necessary* and none is individually sufficient, and
~106 instances are being counted three times across the three ablations. Do not
cost them separately, do not expect any one of them to deliver ~+110 on
`convert` alone, and treat `convert` as a single target requiring all three.

## Complementarity: losing on average is not the same as useless

The promotion bar -- solved count and PAR2 on the 237-file stratified sample
-- measures *average* quality, and that is the right gate for a **default**.
It is the wrong gate for deciding whether a technique is worth keeping at all.

Once there is a portfolio, a configuration earns its place by solving
instances the others do not, however badly it scores on the mean. This project
already reasons this way about *other solvers*: [](arithmetic.md) records that
Primo and z3 are "complementary rather than one dominating" with 30 files only
z3 solves against 11 only Primo does, and that "a portfolio of Primo and
OpenSMT would solve more than either". The same reasoning has not been applied
to Primo's **own** configurations, and it should be.

**What this changes in practice.** Every A/B between Primo configurations
should report, alongside solved count and PAR2:

- instances solved by the new configuration and **not** by the baseline
  ("gained"), and
- instances solved by the baseline and not the new one ("lost").

A lead with `gained > 0` is a portfolio candidate even when
`solved_new < solved_baseline`. A lead with `gained == 0` is dominated on the
sample and can be closed without further argument.

**Today's rejections survive the stronger criterion**, which is worth stating
so they are not reopened on this basis alone:

| lead | solved | gained | verdict under the portfolio criterion |
|---|---|---:|---|
| T2 row propagation on tropical | 0/10 vs 1/10 | **0** | dominated; closure stands |
| Monotone elimination | 173 vs 174 | **0** | dominated; closure stands |

Both were measured file-by-file rather than only in aggregate, so the
"gained" column is real rather than inferred. That is the habit to keep.

**Caveat worth stating once.** Complementarity measured on one sample at one
timeout is weak evidence -- a single uniquely-solved instance is often churn at
the timeout boundary rather than a genuine capability, which is exactly what
[](qf-lra-heuristics.md)'s D1 write-up found. Treat `gained == 1` as a
prompt to look at the instance, not as a portfolio case on its own.

(rejected)=
## Rejected and closed leads

Kept rather than deleted, because several are inviting and the negatives are
informative. Each names the evidence that closed it.

| lead | why it is closed |
|---|---|
| **Unguarding tropical's guarded definitions** | Sound but **27x slower** (4.13s -> 112s, 68x pivots). The guards are load-bearing: setting the flag discharges the definition at the Boolean level, so the case split is cheaper than the tableau row it avoids. [](tropical-matrix-structure.md) |
| **Row propagation on top of T1 (T2)** | 0/10 at every cap setting against the baseline's 1/10 -- a regression, not a wash. Well-controlled: the mechanism fired 2,088 times, and counters are *identical* at default/128/1024 caps because tropical never approaches even the default caps. [](qf-lra-heuristics.md) |
| ~~Monotone / one-sided variable elimination~~ | **Reopened 2026-08-05 on corpus-wide evidence -- see [](#lead-monotone). The 8-file screen that closed it was unrepresentative.** |
| **The propositional pure-literal rule on theory atoms** | **Unsound.** Counterexample verified: `(or (>= x 10) d) AND (<= x 0)` is sat, but forcing the pure-positive atom makes it unsat. Syntactic purity is not semantic independence. [](preprocessing.md)'s [](#pure-literal-trap) |
| **D1 row propagation, raised caps** (mid-band) | Rejected on a 59-file cluster sweep: no cap setting beat baseline on solved count. [](qf-lra-heuristics.md) |
| **D3 offset-equality detection** | Closed by D1's sweep -- they are the same mechanism. |
| **Periodic refactorization** | Rejected 2026-07-30 on a literature check. |
| **H7 fixed-variable elimination** | Implemented, did not clear its bar; kept opt-in. |

(lead-divisibility)=
## 21. Divisibility-aware reasoning for `div` and `mod`

**Read the size claim first.** This is *not* a performance lead on the current
corpus, and it must not be sized against the promotion bar. Census 2026-09-04:
`div`, `mod` and `abs` occur in **0 of 600** randomly sampled SMT-LIB QF_LIA
benchmarks and **0 of the 20** QF_UFLIA mutation seeds. They reach the solver
almost entirely through generated input, and they are not even in the logics
Primo supports: QF_LIA excludes "the function symbols `**`, `/`, `div`, `mod`,
and `abs`" by name and its `:extensions` clause readmits only `*` with concrete
coefficients, with QF_UFLIA saying "As in the logic QF_LIA". Primo accepts them
as a documented extension. So the payoff here is usability, not conformance and
not solved files -- and measurement removed the one operational argument too:
supporting the operators moved the real-seed mutation gate's effective yield
from about 25% to 22.8% (job 131564), because typefuzz's mutants overwhelmingly
have non-constant divisors that are nonlinear whatever the frontend accepts. It is recorded because the structure is
destroyed at a point where recovering it later is impossible, which is the same
mistake [](#lead-ite-preprocessing) documents for ITE.

**Claim.** `(= (mod t k) 0)` is the divisibility constraint `k | t`, which is a
*primitive* in the decision-procedure literature -- Cooper's quantifier
elimination introduces exactly this constraint form, and Bromberger's CutSat
family reasons over `d | aᵀx - b` directly (§2.2, §2.3.1 of
[](papers/bromberger-linear-arithmetic-thesis.md)). Primo has no divisibility
representation at all. `lower-extended-terms` encodes every `div`/`mod` as a
fresh unbounded Int quotient `q` with `0 <= t - k*q <= k-1`, which is correct
and functional but hands branch-and-bound a variable it has no reason to bound.

**Evidence, measured 2026-09-04.** Of 400 generated QF_LIA cases mixing
`div`, `mod`, `abs` and nested arithmetic with both divisor signs, Primo agreed
with Z3 on 398 and answered `unknown` on 2, both with "QF_LIA branch-and-bound
node limit reached" where Z3 answers `unsat`. The reduced witness is

```lisp
(assert (<= 3 (mod (+ (+ v1 4) (div v1 3)) 4)))
```

`(mod _ 4)` ranges over `[0,3]`, so `3 <= (mod _ 4)` pins the remainder to
exactly 3 -- decidable by inspection of the constraint's own range. The
encoding instead asks branch-and-bound to find an unbounded quotient. Sound but
incomplete, and the incompleteness is structural rather than a tuning failure.

**Two pieces, separable.**

1. **Modular coefficient normalization**, a term rewrite:
   `(mod (+ x (* k y)) k) -> (mod x k)`, and reduce literal coefficients inside
   a `mod` to their residues. Sound because `k*y ≡ 0 (mod k)`. Small, local to
   the simplification leaf, and impossible after lowering.
2. **A first-class divisibility atom** `k | t`, recognised from
   `(= (mod t k) 0)` and from the bounds the lowering leaf emits, carried into
   the theory layer so the GCD test and cut generation can use it. Medium-large:
   it reaches past preprocessing into `theory/arithmetic`.

**Cost.** Piece 1 small; piece 2 medium-large.

**Falsifier, stated in advance.** Piece 1 is justified by the fuzz gate alone:
if extending `scripts/qf_uflia_fuzz.py` to emit `div`/`mod` produces no case
where normalization changes the answer *or* the node count, it buys nothing and
should be dropped. Piece 2 needs a real trigger: **if a corpus census still
finds no benchmark family using `div`/`mod` after the operators have been
supported for a release, and the generated-input incompleteness rate stays
around the 0.5% measured above, close piece 2** and keep the quotient encoding.
Do not implement piece 2 on the strength of the literature alone -- Cooper and
CutSat introduce divisibility constraints *internally*, from quantifier
elimination and cuts, which is a different need from reading them out of input.

**Sequencing.** Piece 2 overlaps [](#lead-qflia-extended-gcd) (extended GCD for
bounded-but-not-fixed rows) and would share its machinery; do that census first.

**Related, unfixed.** `LinearPolynomial::from_term`
(`theory/arithmetic/linear_polynomial.cpp`) recurses over the term DAG, so it
belongs to [](#lead-recursion-audit).

(lead-incremental-uf-scans)=
## 22. Incremental UF: avoid repeated retained-data scans

**Status: open, not implemented (2026-09-11). Cost: small-medium.** First in the
profiling study's suggested order; split atom cursors and traversal scratch into
separate measured changes. Evidence for leads 22–25 is [](uf-z3-profiling.md),
recorded at `7a67935`: local profiling of `8573f60`, with solver sources unchanged
from qualified `fa2fe5c`, and reused SLURM timings. These are hypotheses about
that revision, not measured optimization gains or requirements for merging the
opt-in implementation.

**Claim and evidence.** Append-only registration/preparation cursors and touched
scratch could avoid work proportional to all retained atoms/terms on every
small append or check. `BooleanClausifier::register_boolean_argument_terms` and
`PreparedFlatTheory::ensure_theory_atoms` scan old atoms each query;
`simplify_extended_terms_round` initializes full-manager arrays per invocation.
On CLEARSY `0002/00126`, preparation plus reset costs 1.854 of 4.533 seconds
across 413 queries. On `adding.5`, extended-term simplification costs 1.396 seconds
across 2,354 assertion invocations. Reset cost also includes lead 25 and required
backend work; neither figure is a promised saving from scan removal alone.

**Obligations.** Keep registration complete for Boolean arguments and newly
appended atoms; retained IDs must survive pop/redeclaration. Do not turn scratch
reuse into a cache of scoped semantic facts. Preserve backend reconnection,
reobservation and root-assignment replay. Add counters showing old-atom visits
and scratch initialization, plus allocation-boundary and scoped regressions.

**Falsifier.** Close an individual change if counters show the targeted work is
already negligible, or its reduction yields no repeatable whole-session benefit
on the motivating sessions. Any unexplained lost solve blocks acceptance.

(lead-incremental-uf-distinct)=
## 23. Compact handling of large `distinct` assertions

**Status: open; design required (2026-09-11). Cost: large.** Investigate after the
cheaper scan changes in lead 22.

**Claim and evidence.** Avoiding eager pairwise expansion could reduce frontend,
atom and clause volume. `adding.5.prop1_ab_min_max` expands arities 1,051 and 73
into 554,403 pairwise constraints. Its qualified SLURM runtime is 4.997 seconds
versus Z3's 0.139, with 3.046 seconds in Primo's frontend. Local call counts
confirm term interning and disequality insertion traffic. Z3's counters do not
establish which internal representation it uses.

**Obligations.** Write an original paper-grounded design before implementation,
covering arbitrary Boolean polarity, scoped roots, congruence explanations and
models. A positive top-level special case must state its admission conditions
and preserve the general encoding elsewhere. Track avoided pairs, retained
atoms/clauses and any added theory work; independently validate actual models.

**Falsifier.** Reject a candidate if reduced encoding size merely shifts cost
into theory checks without a repeatable runtime/memory benefit on `adding`, or
if it loses previously completed native sessions without a resolved explanation.
No compact-distinct algorithm is claimed as implemented by this lead.

(lead-incremental-uf-equations)=
## 24. Incrementally sound equation elimination

**Status: open; substitution/restoration deferred (2026-09-11). Cost: large.**
The existing whole-goal equation-elimination tactic remains incompatible with
assumptions mode. This is a new design task, not an option change.

**Claim and evidence.** Sound elimination may reduce the search gap on Goel
`firewire` and `rushhour`. The qualified `firewire_tree.5.prop3_ab_core_max`
solve phase costs 18.104 of 23.456 seconds. A separate Z3 per-query diagnostic
reports 22,931 eliminated variables in the first query. Six native `rushhour`
sessions hit Primo's theory-conflict budget and account for about 63% of the
aggregate PAR2 gap. This correlation does not prove elimination will close it.

**Obligations.** Start with the deferred substitution/restoration literature in
[](assumption-incremental-lra.md), read the relevant sections, and write a UF
scope/identity/model design checklist. Future assertions, pop, redeclarations,
guarded consequences and saved model snapshots must remain sound. Instrument
eliminated/restored variables and search work. Do not persist assertion-dependent
substitutions as unconditional definitions.

**Falsifier.** First measure admissible elimination opportunities under the new
contract. Defer if they are too sparse on the motivating cases. Reject the
implementation if restoration costs erase a repeatable search/PAR2 benefit or
it introduces unresolved losses. Raising the conflict budget alone is not
evidence for this mechanism.

(lead-incremental-uf-structure)=
## 25. Share immutable EUF structure across checks

**Status: open, not implemented (2026-09-11). Cost: medium-large.** Reprofile after
lead 22 so registration improvements are not counted twice.

**Claim and evidence.** Separate immutable prepared structure from working facts
to reduce copies and allocation. Local gprof records 413 copies each of
`FlatTermIndex` and `FlatCongruenceClosure` on CLEARSY `0002/00126`; qualified
working-state construction/reconnection costs 1.140 seconds. The 373-query
`0023/00268` case spends 0.167 seconds in that phase, but its 16 gprof samples
cannot reliably rank individual functions. `firewire` spends only 0.117 seconds
there, so this lead cannot explain its search gap.

**Obligations.** Document ownership and mutation boundaries before changing
representations. Keep fresh per-query facts, valid retained SAT learning and the
adapter's callback/reason lifetime contract. Reconnect/reobserve and replay all
required assignments, including roots, after SAT, assumption-UNSAT, unknown and
cancellation. Track copied bytes, allocations, reset time and peak memory.

**Falsifier.** Close the lead if copies are a negligible residual after lead 22,
or sharing fails to improve repeated whole-session measurements and memory on
long CLEARSY sessions. Skipping required root replay is not an admissible shortcut.

All four leads require the repository's fresh solver fuzz gate, separate native
SAT-model validation, and default-preservation checks before accepting a behavior
change or a performance claim. Retain per-family gains/losses and partial query
progress alongside PAR2. The noisy statistics-on/off probe remains inconclusive;
it does not support a separate printing-overhead optimization claim.

## Adding a lead

Give it: the claim in one sentence; the evidence, with counters or timings and
whether they are local or SLURM; a cost estimate; the sequencing constraint if
it depends on another lead; and **a falsifier stated before the work starts**.
A lead without a falsifier tends to get retuned instead of closed.
