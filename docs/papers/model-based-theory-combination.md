# de Moura-Bjorner Model-Based Theory Combination

Status stamp: 2026-08-08, branch `convex-no-qflra-integration`, after the
convex EUF/LRA equality fixpoint and mixed fixed-elimination promotion.

Reference:

- Leonardo de Moura and Nikolaj Bjorner, "Model-based Theory Combination",
  Electronic Notes in Theoretical Computer Science 198(2), 2008, pp. 37-49,
  DOI: <https://doi.org/10.1016/j.entcs.2008.04.079>.

This note records what the paper says, section by section, and whether Primo
currently implements it. The checklist below is deliberately conservative:
Primo implements a complete convex equality-sharing path for its documented
QF_UFLRA subset and some model-guided scaffolding, but not the paper's full
model-based theory-combination procedure.

## Overall Status

Primo does not yet implement full `DPLL(T^M)` or full model-based Nelson-Oppen
combination. It has these related pieces:

- conservative QF_UF, QF_LRA, and narrow QF_UFLRA `get-model` support;
- `TheoryCombinationManager` ownership of the shared equality ledger for
  interface terms, equality atoms, SAT assignment snapshots, trailed arrangement
  values, and per-round equality classifications;
- a convex equality-sharing fixpoint for the documented mixed QF_UFLRA
  interface, with an exact unresolved-pair fallback;
- lazy SAT-visible equality atoms for shared Real terms;
- guarded equality clauses when EUF or LRA implies a shared Real equality;
- an opt-in `--qf-uflra-model-reconciliation` profile where
  `TheoryCombinationManager` owns a round-scoped shared-interface model
  projection and retains explained, EUF-compatible LRA equalities in the live
  closure without SAT atoms; the default path still forces such equalities;
- bounded shared-Real separation for the documented model-printing subset;
- default-off `--model-equality-branching`, which keeps surviving
  LRA-model-equal shared Real equality guesses on an active SAT-assumption trail;
- a narrow LRA model-mutation filter for that branch mode: before guessing an
  LRA-model-equal pair, Primo checks whether either strict separation is still
  LRA-consistent under the current Boolean assignment and skips the guess if so;
- a narrower explained clause path for mutation-forced LRA equalities: if both
  strict separations conflict, Primo learns a guarded interface-equality clause
  from those two conflict reasons instead of keeping the equality as a SAT
  assumption guess;
- an opt-in `--nelson-oppen-prop=online` paired-bound LRA-to-EUF propagation
  experiment that avoids all-pairs equality precreation. The mixed dispatcher
  now creates and observes a fresh SAT equality variable from `cb_propagate()`
  only for a selected paired-bound candidate published by `OnlineLraPropagator`.
  The complete final LRA equality scan now uses the checked Simplex model
  partition to reject cross-value pairs before exact probes. A previous
  candidate-only experiment reran it only after separation failure, but that
  validation condition missed congruence conflicts caused by EUF-mergeable
  pairs. Removing this scan requires the paper's missing shared-model
  validation mechanism, not only a successful pair-separation search.

The missing parts are the paper's central mechanisms. `TheoryCombinationManager`
provides shared equality ownership and, under the opt-in
`--qf-uflra-model-reconciliation` profile, owns a round-scoped projection of
the LRA model classes and reconciled internal equalities over shared Real
terms. This is not a full shared model object in the paper's sense: it does not
own component theory models or mutable tableau state.

- no generic model object carried by the DPLL(T) state;
- no full or generic `M-Propagate` transition;
- no full `M-Mutate` transition or mutable theory-model object;
- no theory-independent implementation for more than EUF plus Real LRA;
- no completeness claim for general Nelson-Oppen combination.

The first generic API hook is now present: `theory::ModelEqualityEvaluation`
records the result of a per-theory query over two shared terms. The current
QF_UFLRA coordinator implements an LRA-only adapter, `LraTheoryModelView`,
which classifies a pair as unequal in the current arithmetic model, equal with
mutation not queried, equal but avoidable by the strict-separation mutation
filter, or equal and not avoided by that filter. This is an API and
classification slice, not the paper's full model-carrying transition system. The
only explained positive-equality use currently built on top of it is the
QF_UFLRA strict-separation case where the same LRA assignment refutes both
`(< lhs rhs)` and `(< rhs lhs)`.

## Implementation Checklist

A checked box means that usable code and focused tests exist on this branch.
Items marked **partial** implement only the stated QF_UFLRA subset; they are not
claims that the corresponding paper transition is complete.

### Convex combination foundation

- [x] Purify the supported mixed formulas into EUF-owned and LRA-owned atoms,
  sharing only typed proxy/interface terms (`MixedTheoryPurificationResult`).
- [x] Identify the shared Real interface and canonicalize unordered equality
  pairs (`TheoryCombinationManager`).
- [x] Own each interface pair's optional term, SAT literal, current assignment,
  trailed equality value, and per-round classification in one manager.
- [x] Create SAT-visible interface equality atoms lazily instead of allocating
  the full shared-pair cross product.
- [x] Propagate explained EUF spanning equalities and explained LRA equalities
  through guarded SAT clauses, then re-enter SAT so both theories observe them.
- [x] Repeat EUF-to-LRA and LRA-to-EUF sharing to a convex equality fixpoint.
- [x] Preserve completeness for the documented convex EUF+LRA subset with an
  exact strict-separation check over every still-unresolved model-equal shared
  Real pair; an entailed equality cannot cross classes in a feasible model.
- [x] Keep pure QF_UF and pure QF_LRA outside the mixed coordinator.

These items implement the convex Nelson-Oppen foundation reviewed in Sections
2 and 2.1. They are required by this milestone, but are not by themselves the
paper's novel model-based transition system.

### Section 4: model-based combination

- [x] **Partial:** query concrete LRA values through `LraTheoryModelView` and
  classify a selected shared pair with `ModelEqualityEvaluation`.
- [x] **Partial:** lazily create a model-equality case-split literal, prefer its
  positive phase, and carry the guess as an IPASIR assumption
  (`ModelEqualityArrangementState`). This path is default-off.
- [x] **Partial:** postpone optional model-equality guesses until ordinary SAT
  search and the current individual-theory checks have quiesced.
- [x] **Partial:** retain surviving guesses, inspect failed assumptions, learn a
  blocking clause, and drop failed guesses on backtracking.
- [x] **Partial:** use care-pair queues, ranking, budgets, and adaptive stopping
  to reduce the pairs queried by the optional branch heuristic.
- [x] **Proxy only, not `M-Mutate`:** test both strict LRA separations to reject
  an avoidable model equality before creating its SAT atom. If both conflict,
  learn the now-entailed equality with those conflict reasons.
- [ ] Carry a set of component models `M` as part of the generic DPLL(T) search
  state.
- [ ] Implement theory-independent `M-Propagate` over arbitrary component
  models and shared sorts.
- [ ] Implement `M-Mutate` as an actual update of a component model.
- [x] **Partial:** reconcile explained LRA equalities into the live EUF closure
  through round-scoped shared state owned by the combination manager; final
  validation and model reconstruction consume the same internal equalities.
- [ ] Generalize the mechanism beyond the documented EUF plus rational-LRA
  path, including non-convex theory case splits.

### Sections 5 and 5.1: Simplex equality production

- [x] Maintain the feasible Simplex assignment `beta` in `LraTableau` and read
  it when constructing an LRA model view.
- [x] Implement `FixedEq` with bound explanations and deterministic spanning
  output in `LraTableau::entailed_equalities()`.
- [x] Implement `EqRow` with fixed-polynomial checks, exact
  `DeltaRational` comparison, bound explanations, and deterministic spanning
  output.
- [x] Aggressively remove fixed variables from the basis in mixed QF_UFLRA by
  default, exposing additional `EqRow` consequences; pure QF_LRA retains its
  separately measured default.
- [x] Check every opportunistically reported equality against an independent
  two-strict-separations entailment oracle in tableau tests.
- [ ] Implement `EqOffsetRows` and its `(x_k, value) -> x_i` index.
- [ ] Implement the more expensive `EqRows` pass before model propagation.
- [ ] Hook the first three rules eagerly into pivot/row updates as described by
  the paper. Primo currently polls `FixedEq` and `EqRow` after a feasible check
  to keep combination work off the LRA pivot hot path.
- [ ] Build the paper's expected-linear-time hash partition of all shared
  variables by equal `beta` values. The optional branch heuristic currently
  examines selected pairs instead.
- [ ] Evaluate and promote row-bound propagation specifically for its ability
  to expose additional fixed variables and reduce exact fallback probes.

### Section 5.2: freedom-interval mutation

- [x] **Prerequisite only:** `LraTableau` has its internal feasibility-preserving
  `update(nonbasic, value)` operation.
- [ ] Compute the paper's freedom interval `[L_j, U_j]` for each non-basic
  variable as a model-mutation API.
- [ ] Select a value in that interval that increases the number of model-value
  equivalence classes.
- [ ] Apply those updates to diversify the live arithmetic model before
  `M-Propagate`.
- [ ] Trail or reconstruct mutated component models across SAT backtracking.

### Evaluation and generality

- [x] Expose counters for convex rounds, equality producers, lazy atoms,
  reasons, exact fallback probes, model-equality queries, assumptions, and
  mutation-filter outcomes.
- [x] Run focused unit, ASAN, generated-fuzz, mutation-fuzz, and 45-file
  QF_UFLRA promotion gates; exact commands and results are in
  `docs/run-log.json` under commit `9481bfb`.
- [ ] Reproduce the paper's Section 6 benchmark suite and comparisons.
- [ ] Claim the full Appendix A `DPLL(T^M)` transition system. This remains an
  explicit non-goal of the current convex milestone.

## Section-by-Section Comparison

| Paper section | Paper content | Primo status |
| --- | --- | --- |
| Abstract | The paper proposes reconciling candidate models from component theories instead of eagerly propagating every implied equality or eagerly adding all shared equality literals. | **Partial.** Primo has model printing and partial QF_UFLRA reconciliation, but no full model-based combination engine. |
| 1 Introduction | The motivating example is that a theory model can reveal that two shared variables have equal values. The solver can use such model equalities to guide equality sharing between theories. | **Partial scaffold.** `--model-equality-branching` can inspect the LRA model and keep surviving equality guesses between shared Real terms on an active SAT-assumption trail. Mutation-forced LRA equalities now learn a guarded equality clause from strict-separation conflict reasons, but this is still not the paper's full model-propagation transition. |
| 2 Background | Defines signatures, formulas, theories, structures, satisfaction, equality, stable infiniteness, convexity, and combination terminology. | **Conceptual only.** Primo has typed terms, EUF, LRA, and model fragments, but no explicit theory-combination contract that records stable infiniteness, convexity, or model-production obligations per theory. |
| 2.1 Nelson-Oppen Combination Method | Reviews purification, shared variables, arrangements, and equality propagation between disjoint theories. For convex theories, propagating implied equalities can avoid arrangement enumeration. | **Implemented subset.** The documented purified QF_UFLRA path performs explained bidirectional equality sharing to a fixpoint. EUF emits a spanning set; LRA emits `FixedEq`/`EqRow` consequences and uses an exact fallback over unresolved pairs equal in the checked LRA model. This is complete for that convex interface, not generic multi-theory Nelson-Oppen. |
| 3 Related Work | Places the approach against Shostak-style combination, delayed theory combination, Ackermannization, and dynamic Ackermannization. The paper's point is that existing alternatives can add too many literals or too many lemmas. | **Mostly not implemented as paper mechanisms.** Primo has separate EUF/Ackermann diagnostics and separate QF_UF disequality-propagation work, but these are not the model-based combination algorithm. |
| 3.1 Convex Theories | Describes equality propagation for convex theories and notes that complete enumeration of all theory-implied equalities can be expensive. | **Implemented subset, different schedule.** Primo's final-check coordinator reaches a complete convex fixpoint for the supported shared Real interface. Cheap producers run first; exact strict-separation probes cover what they miss. It is not the paper's model-guided schedule. |
| 3.2 Delayed Theory Combination | DTC creates Boolean equality literals for shared variables and lets SAT search over arrangements. This avoids special theory-combination rules but may create a quadratic number of literals. | **Not implemented as DTC.** Default `final` mode creates shared Real equality atoms lazily when a theory implication or model-equality branch needs them. Opt-in `--nelson-oppen-prop=online` also avoids all-pairs precreation: it creates and observes only the paired-bound candidate equality atom it is about to propagate. |
| 3.3 Ackermannization | Ackermann reduction replaces UF reasoning with constraints over applications and congruence axioms. The paper treats it as useful in some cases but potentially expensive. | **Separate from this work.** Primo's Ackermann and disequality-propagation options are EUF-side mechanisms. They are not integrated as the paper's model-combination method. |
| 3.3.1 Dynamic Ackermannization | Dynamic Ackermannization adds Ackermann constraints during search, especially when disequalities between applications make argument disequalities relevant. | **Separate/partial.** This belongs to the QF_UF disequality propagation line, not to current mixed QF_UFLRA model-based combination. |
| 4 Model-Based Theory Combination | This is the core contribution. A theory solver maintains or exposes a candidate model. If two shared terms are equal in that model, the combination layer may propagate that equality. If the equality is bad for another theory, search backtracks or the model is mutated. | **Not fully implemented.** Primo has a default-off LRA-model-equality path for the QF_UFLRA coordinator. `ModelEqualityArrangementState` owns queued guesses, active IPASIR assumptions, SAT failed-assumption blocking, and adaptive rejection counters. The loop includes a narrow mutation filter that skips a model-equal pair if adding either strict separation remains LRA-consistent. If both strict separations conflict, the equality is treated as LRA-entailed and learned as a guarded clause with those conflict reasons. There is no generic `DPLL(T^M)` state and no generic model-equality propagation. |
| 5 Simplex: An Example Model-Producing Theory Solver | The paper shows how Simplex naturally provides a model: the current assignment maps arithmetic variables to rational values. Equal-valued shared variables are equality candidates. | **Partial.** Primo's LRA path has a tableau assignment and hashes full `DeltaRational` values into model-equality classes. The convex fallback uses those classes only to filter exact entailment candidates; there is not yet a complete Simplex-backed combination API. |
| 5.1 Opportunistic Equality Propagation | The paper describes `FixedEq`, `EqRow`, `EqOffsetRows`, and `EqRows`, plus fixed-variable removal and supporting bound propagation. | **Partial implementation.** `LraTableau::entailed_equalities()` implements explained `FixedEq` and `EqRow`; mixed QF_UFLRA removes fixed basic variables by default. `EqOffsetRows`, `EqRows`, eager pivot hooks, and mixed-path row-bound propagation remain missing. The exact unresolved-pair fallback preserves convex completeness despite those omissions. |
| 5.2 Model Mutation | The paper describes mutating arithmetic model values, using freedom intervals, to avoid unnecessary equalities before propagating them to other theories. | **Partial filter, not the paper algorithm.** Primo does not perturb the stored Simplex assignment. In the default-off branch mode, it uses strict-separation consistency checks as a conservative proxy for "this equality can be avoided by mutation" and suppresses that optional equality guess. |
| 6 Experimental Evaluation | The paper compares model-based theory combination against DTC, Ackermannization, Yices, and MathSAT on several UF+arithmetic benchmark families. | **Not reproduced.** Primo has local and SLURM benchmark evidence in `docs/run-log.json`, including QF_UFLRA CPAchecker and sample Z3 comparisons, but it has not reproduced the paper's experiment suite. |
| 7 Conclusions | The paper emphasizes that the approach requires theories to efficiently answer whether two shared terms have equal values in the current model. | **Partial.** Primo can answer this for some LRA Real terms and can query EUF closure equality, but there is no uniform cross-theory model-query object yet. |
| 7.1 On the Significance of Ackermann's Reduction | The paper argues that model-based combination can avoid some Ackermannization costs, while dynamic Ackermannization remains useful in UF-heavy cases. | **Separate.** Primo's Ackermann-related work should stay documented as EUF-specific unless it is explicitly wired into mixed-theory model combination. |
| 7.2 Are Theories Amenable to Model-Based Combinations? | The paper asks which theories can cheaply expose useful candidate models; Simplex is the main positive example. | **Not generalized.** Primo has only the EUF plus Real LRA partial case. Other theories have no model-combination interface. |
| Acknowledgment | No algorithmic content. | **No implementation relevance.** |
| Appendix A: Presentation of `DPLL(T^M)` | Gives the abstract transition system where the SAT/theory search state includes model information and model-based propagation/mutation rules. | **Not implemented.** Primo's current mixed coordinator is an ad hoc QF_UFLRA path inside `solve_qf_uf_lra`, not a generic `DPLL(T^M)` engine. |

## Current Primo Anchors

The relevant implemented pieces are documented and tested here:

- `docs/architecture.md`: the convex QF_UFLRA fixpoint, model printing, and
  explicit non-goals for generic model-based combination.
- `docs/papers/nelson-oppen-closure-methods.md`: current design note for the
  branch and its limitations.
- `src/smt/theory_combination.hpp`: `TheoryCombinationManager`, the current
  shared equality ledger and diagnostic scaffold.
- `src/smt/check_sat.cpp`: `solve_qf_uf_lra`, lazy generated equality literals,
  mixed final checks, model reconstruction, and default-off model-equality
  branching.
- `tests/unit/lra_tableau_tests.cpp`: `FixedEq`/`EqRow` rules, explanations,
  soundness oracle, and fixed-basis preparation.
- `tests/unit/qf_lra_solver_tests.cpp`: QF_UFLRA convex fixpoint,
  model-printing, shared-Real equality propagation, exact fallback, separation,
  and model-equality-branching regressions.

## 2026-08-04 Model-Equality Query Checklist

Bibliographic basis:

- de Moura and Bjorner, "Model-based Theory Combination", Section 4 for the
  model-based equality-sharing trigger and `M-Propagate`/`M-Mutate` transition
  shape;
- Section 5 for the Simplex assignment as the arithmetic model;
- Section 5.2 for the idea that a theory may mutate or diversify its model to
  avoid unnecessary equalities before sharing them.

Implemented subset:

- `src/theory/theory_solver.hpp` defines `theory::ModelEqualityEvaluation` and
  `theory::ModelEqualityStatus` as a backend-neutral result object over two
  `core::TermId`s.
- `src/smt/check_sat.cpp` has the current LRA adapter, `LraTheoryModelView`,
  owned by the mixed QF_UFLRA coordinator. It reads the concrete rational
  values extracted from the tableau model and, when enabled, asks the existing
  strict-separation filter whether either `(< lhs rhs)` or `(< rhs lhs)` is
  consistent with the current LRA assignment.
- `choose_lra_model_equality_branch` consumes this result object before it
  creates a SAT-visible equality atom. Model-equal pairs that the filter marks
  avoidable are skipped while still unmaterialized. Pairs that survive because
  both strict separations conflict are materialized only as needed and learned as
  guarded equality clauses using the strict-separation conflict reasons. Pairs
  whose equality is model-based but not explained remain default-off SAT
  assumption guesses managed by `ModelEqualityArrangementState`.

Data structures and invariants:

- The theory-layer status type stores only term ids and an equality/mutation
  classification. It does not depend on SAT literals, SMT-LIB ASTs, or the mixed
  coordinator.
- The LRA view is local to the SMT coordinator because it has to combine an LRA
  tableau model, the current Boolean assignment to arithmetic atoms, and the
  coordinator's branch policy.
- The mutation classification is a query result. Primo does not update the
  tableau assignment as Section 5.2's freedom-interval `update` operation would.
- A model equality classified as equal is not automatically an entailed theory
  propagation. The only current positive case is `kEqualMutationForced`, where
  LRA separately proves both strict separation orientations inconsistent and
  therefore supplies a guarded equality-clause reason.

Counters and tests:

- `:no.interface.model-equality-api-builds` counts LRA model views built for
  the optional model-equality branch loop.
- `:no.interface.model-equality-api-queries` counts pair classifications.
- `:no.interface.model-equality-api-not-equal`,
  `:no.interface.model-equality-api-equal`,
  `:no.interface.model-equality-api-mutation-unqueried`,
  `:no.interface.model-equality-api-mutation-avoidable`, and
  `:no.interface.model-equality-api-mutation-forced` split the query outcomes.
- `:no.interface.model-equality-forced-*` counters split the strict-separation
  explained path into candidate, created/existing atom, learned clause, recheck,
  reason-literal, already-true, and assigned-false counts.
- Unit regressions in `tests/unit/qf_lra_solver_tests.cpp` assert both the
  mutation-disabled assumption path and mutation-enabled skip path go through
  the query API. `qf_uf_lra_model_equality_forced_by_lra_gets_explained_clause`
  disables broad final Nelson-Oppen propagation and checks that a paired LRA
  bound equality is learned through the model-equality forced-clause path without
  entering the SAT-assumption trail.

Deviations and limitations:

- No generic `DPLL(T^M)` state carries per-theory model objects yet.
- No generic `M-Propagate` implementation exists for model equalities.
- No freedom-interval mutation is implemented; the current mutation result is a
  conservative strict-separation consistency proxy.
- No EUF or non-LRA theory model implements this API yet.
- No completeness claim changes. The supported behavior remains the documented
  experimental QF_UFLRA branch heuristic.

## Next Implementation Direction

The next Nelson-Oppen online design should follow the paper's model-based
principle: generate equality candidates from a current theory model, a cheap
theory implication, or a care-ranked relevance filter before creating SAT
equality atoms. It should not start by materializing the full same-sort shared
pair cross product.

The concrete post-2026-08-05 performance plan is tracked in
`docs/qf-uflra-model-guided-performance.md`. That note records the Z3-stat
diagnosis from the current SLURM artifact and turns the paper mechanisms into a
QF_UFLRA implementation checklist. Its first target is consumer-aware
LRA-to-EUF equality handling. The attempted shortcut that carried EUF-mergeable
LRA-implied equalities internally was rejected after the 2026-08-05 full
QF_UFLRA run found wrong answers. The live conservative rule is to skip only
EUF-already-true equalities; EUF-mergeable LRA equalities are classified but
still forced through guarded SAT-visible equality clauses. A future internal
carry path needs a separate proof and stronger real-seed fuzz coverage.

For the current CaDiCaL backend, the concrete rule is:

1. Keep the online producer's paired-bound candidates separate from the full
   shared-pair cross product.
2. For CaDiCaL-backed online search, use the backend-neutral
   `sat::ExternalPropagator` and `IpasirUpBackend::new_observed_var()` path to
   test selected fresh equality atoms at controlled callback boundaries before
   relying on them in the mixed dispatcher.
3. Stream the theory-owned explanation if the equality is entailed; use SAT
   assumptions or guarded clauses for model-equality guesses.
4. Record the full shared-pair count, selected candidate count,
   lazy-created/observed atom count, delivered propagation count, disagreement
   conflicts, and skipped mutation-avoidable candidates.

The dispatcher follow-up has landed for the paired-bound source only:
`--nelson-oppen-prop=online` no longer precreates equality atoms before search,
but it still does not turn arbitrary LRA model equalities into entailed theory
propagations.

The next narrow step after the paired-bound dispatcher has also landed for the
QF_UFLRA branch path: a mutation-forced LRA model equality now uses the same
strict-separation conflicts that made mutation impossible as its SAT antecedent.
This direction is still not a full `DPLL(T^M)` implementation until the solver
has a generic mutable theory-model object and theory implementations that can
produce explained model-equality propagations from this query interface.

## Documentation Claim To Use

Use this wording when summarizing the current state:

> Primo implements prerequisites and a narrow experimental QF_UFLRA
> model-equality assumption loop inspired by de Moura and Bjorner's
> model-based theory-combination paper, including a strict-separation
> consistency filter for avoidable LRA model equalities. Mutation-forced LRA
> equalities can learn guarded clauses from strict-separation conflict reasons;
> other guesses stay SAT assumptions managed by `ModelEqualityArrangementState`.
> The opt-in online
> paired-bound producer exercises the SAT callback path without all-pairs
> equality precreation and lazily creates only the selected equality atom it is
> about to propagate. Primo does not yet implement the full `DPLL(T^M)`
> procedure, a mutable generic theory-model object, or complete Nelson-Oppen
> model-equality propagation.

Avoid saying that Primo implements "the de Moura-Bjorner algorithm" without the
qualification above.
