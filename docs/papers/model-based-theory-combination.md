# de Moura-Bjorner Model-Based Theory Combination

Status stamp: 2026-08-06, branch `nelson-oppen-online-lazy-ipasir-up`, current
documentation/code baseline for the default-off QF_UFLRA model-equality scaffold.

Reference:

- Leonardo de Moura and Nikolaj Bjorner, "Model-based Theory Combination",
  Electronic Notes in Theoretical Computer Science 198(2), 2008, pp. 37-49,
  DOI: <https://doi.org/10.1016/j.entcs.2008.04.079>.

This note records what the paper says, section by section, and whether Primo
currently implements it. It is a status audit, not an implementation checklist.
The current answer is deliberately conservative: Primo implements prerequisites
and a narrow QF_UFLRA scaffold inspired by the paper, but it does not implement
the full model-based theory-combination procedure.

## Overall Status

Primo does not yet implement full `DPLL(T^M)` or full model-based Nelson-Oppen
combination. It has these related pieces:

- conservative QF_UF, QF_LRA, and narrow QF_UFLRA `get-model` support;
- `TheoryCombinationManager` ownership of the shared equality ledger for
  interface terms, equality atoms, SAT assignment snapshots, trailed arrangement
  values, and per-round equality classifications;
- partial mixed QF_UFLRA final checks over EUF and Real LRA atoms;
- lazy SAT-visible equality atoms for shared Real terms;
- guarded equality clauses when EUF or LRA implies a shared Real equality;
- a QF_UFLRA-local LRA-to-EUF classifier that skips SAT equality creation only
  when the current EUF closure already has the pair equal; EUF-mergeable
  LRA-implied equalities are still forced through guarded SAT-visible equality
  clauses;
- bounded shared-Real separation for the documented model-printing subset;
- default-off `--model-equality-branching`, which keeps surviving
  LRA-model-equal shared Real equality guesses on an active SAT-assumption trail;
- a narrow LRA model-mutation filter for that branch mode: before guessing an
  LRA-model-equal pair, Primo checks whether either strict separation is still
  LRA-consistent under the current Boolean assignment and skips the guess if so;
- a narrower explained clause path for mutation-forced LRA equalities: if both
  strict separations conflict, Primo learns a guarded interface-equality clause
  from those two conflict reasons instead of keeping the equality as a SAT
  assumption guess.
- an opt-in `--nelson-oppen-prop=online` paired-bound LRA-to-EUF propagation
  experiment that avoids all-pairs equality precreation. The mixed dispatcher
  now creates and observes a fresh SAT equality variable from `cb_propagate()`
  only for a selected paired-bound candidate published by `OnlineLraPropagator`.
  The broad final all-pairs LRA equality scan remains mandatory. A previous
  candidate-only experiment reran it only after separation failure, but that
  validation condition missed congruence conflicts caused by EUF-mergeable
  pairs. Removing this scan requires the paper's missing shared-model
  validation mechanism, not only a successful pair-separation search.

The missing parts are the paper's central mechanisms. `TheoryCombinationManager`
already provides shared equality ownership for interface terms, equality atoms,
SAT literals, SAT assignment snapshots, trailed equality values, and per-round
classification flags, but it is not a shared model object in the paper's sense:
it does not own component model fragments, mutation state, or the current
reconciled model.

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

## Section-by-Section Comparison

| Paper section | Paper content | Primo status |
| --- | --- | --- |
| Abstract | The paper proposes reconciling candidate models from component theories instead of eagerly propagating every implied equality or eagerly adding all shared equality literals. | **Partial.** Primo has model printing and partial QF_UFLRA reconciliation, but no full model-based combination engine. |
| 1 Introduction | The motivating example is that a theory model can reveal that two shared variables have equal values. The solver can use such model equalities to guide equality sharing between theories. | **Partial scaffold.** `--model-equality-branching` can inspect the LRA model and keep surviving equality guesses between shared Real terms on an active SAT-assumption trail. Mutation-forced LRA equalities now learn a guarded equality clause from strict-separation conflict reasons, but this is still not the paper's full model-propagation transition. |
| 2 Background | Defines signatures, formulas, theories, structures, satisfaction, equality, stable infiniteness, convexity, and combination terminology. | **Conceptual only.** Primo has typed terms, EUF, LRA, and model fragments, but no explicit theory-combination contract that records stable infiniteness, convexity, or model-production obligations per theory. |
| 2.1 Nelson-Oppen Combination Method | Reviews purification, shared variables, arrangements, and equality propagation between disjoint theories. For convex theories, propagating implied equalities can avoid arrangement enumeration. | **Partial.** Primo has QF_UFLRA purification and lazy equality sharing for shared Real terms. It is not full Nelson-Oppen: it lacks general bidirectional equality propagation, full arrangements, fine-grained cross-theory explanations, and generic multi-theory support. |
| 3 Related Work | Places the approach against Shostak-style combination, delayed theory combination, Ackermannization, and dynamic Ackermannization. The paper's point is that existing alternatives can add too many literals or too many lemmas. | **Mostly not implemented as paper mechanisms.** Primo has separate EUF/Ackermann diagnostics and separate QF_UF disequality-propagation work, but these are not the model-based combination algorithm. |
| 3.1 Convex Theories | Describes equality propagation for convex theories and notes that complete enumeration of all theory-implied equalities can be expensive. | **Partial, different shape.** Primo detects some EUF- and LRA-implied shared Real equalities during mixed final checks and learns guarded clauses. It does not implement a complete eager equality-propagation engine. |
| 3.2 Delayed Theory Combination | DTC creates Boolean equality literals for shared variables and lets SAT search over arrangements. This avoids special theory-combination rules but may create a quadratic number of literals. | **Not implemented as DTC.** Default `final` mode creates shared Real equality atoms lazily when a theory implication or model-equality branch needs them. Opt-in `--nelson-oppen-prop=online` also avoids all-pairs precreation: it creates and observes only the paired-bound candidate equality atom it is about to propagate. |
| 3.3 Ackermannization | Ackermann reduction replaces UF reasoning with constraints over applications and congruence axioms. The paper treats it as useful in some cases but potentially expensive. | **Separate from this work.** Primo's Ackermann and disequality-propagation options are EUF-side mechanisms. They are not integrated as the paper's model-combination method. |
| 3.3.1 Dynamic Ackermannization | Dynamic Ackermannization adds Ackermann constraints during search, especially when disequalities between applications make argument disequalities relevant. | **Separate/partial.** This belongs to the QF_UF disequality propagation line, not to current mixed QF_UFLRA model-based combination. |
| 4 Model-Based Theory Combination | This is the core contribution. A theory solver maintains or exposes a candidate model. If two shared terms are equal in that model, the combination layer may propagate that equality. If the equality is bad for another theory, search backtracks or the model is mutated. | **Not fully implemented.** Primo has a default-off LRA-model-equality path for the QF_UFLRA coordinator. `ModelEqualityArrangementState` owns queued guesses, active IPASIR assumptions, SAT failed-assumption blocking, and adaptive rejection counters. The loop includes a narrow mutation filter that skips a model-equal pair if adding either strict separation remains LRA-consistent. If both strict separations conflict, the equality is treated as LRA-entailed and learned as a guarded clause with those conflict reasons. There is no generic `DPLL(T^M)` state and no generic model-equality propagation. |
| 5 Simplex: An Example Model-Producing Theory Solver | The paper shows how Simplex naturally provides a model: the current assignment maps arithmetic variables to rational values. Equal-valued shared variables are equality candidates. | **Partial.** Primo's LRA path has a tableau assignment and can produce values for user-declared nullary Real constants and selected shared Real terms. It is not yet a complete Simplex-backed combination API. |
| 5.1 Opportunistic Equality Propagation | The paper describes cheaper equality-detection opportunities in Simplex, such as fixed variables and row-derived equalities. | **Partial, not the paper algorithm.** Primo has LRA propagation and bound reasoning, but it does not implement the paper's opportunistic equality rules as named combination operations. Current LRA-to-EUF equality detection is still a final-check-oriented mechanism. |
| 5.2 Model Mutation | The paper describes mutating arithmetic model values, using freedom intervals, to avoid unnecessary equalities before propagating them to other theories. | **Partial filter, not the paper algorithm.** Primo does not perturb the stored Simplex assignment. In the default-off branch mode, it uses strict-separation consistency checks as a conservative proxy for "this equality can be avoided by mutation" and suppresses that optional equality guess. |
| 6 Experimental Evaluation | The paper compares model-based theory combination against DTC, Ackermannization, Yices, and MathSAT on several UF+arithmetic benchmark families. | **Not reproduced.** Primo has local and SLURM benchmark evidence in `docs/run-log.json`, including QF_UFLRA CPAchecker and sample Z3 comparisons, but it has not reproduced the paper's experiment suite. |
| 7 Conclusions | The paper emphasizes that the approach requires theories to efficiently answer whether two shared terms have equal values in the current model. | **Partial.** Primo can answer this for some LRA Real terms and can query EUF closure equality, but there is no uniform cross-theory model-query object yet. |
| 7.1 On the Significance of Ackermann's Reduction | The paper argues that model-based combination can avoid some Ackermannization costs, while dynamic Ackermannization remains useful in UF-heavy cases. | **Separate.** Primo's Ackermann-related work should stay documented as EUF-specific unless it is explicitly wired into mixed-theory model combination. |
| 7.2 Are Theories Amenable to Model-Based Combinations? | The paper asks which theories can cheaply expose useful candidate models; Simplex is the main positive example. | **Not generalized.** Primo has only the EUF plus Real LRA partial case. Other theories have no model-combination interface. |
| Acknowledgment | No algorithmic content. | **No implementation relevance.** |
| Appendix A: Presentation of `DPLL(T^M)` | Gives the abstract transition system where the SAT/theory search state includes model information and model-based propagation/mutation rules. | **Not implemented.** Primo's current mixed coordinator is an ad hoc QF_UFLRA path inside `solve_qf_uf_lra`, not a generic `DPLL(T^M)` engine. |

## Current Primo Anchors

The relevant implemented pieces are documented and tested here:

- `docs/architecture.md`: model printing, QF_UFLRA partial final checks, and
  explicit non-goals for full Nelson-Oppen.
- `docs/papers/nelson-oppen-closure-methods.md`: current design note for the
  branch and its limitations.
- `src/smt/theory_combination.hpp`: `TheoryCombinationManager`, the current
  shared equality ledger and diagnostic scaffold.
- `src/smt/check_sat.cpp`: `solve_qf_uf_lra`, lazy generated equality literals,
  mixed final checks, model reconstruction, and default-off model-equality
  branching.
- `tests/unit/qf_lra_solver_tests.cpp`: QF_UFLRA model-printing, shared-Real
  equality propagation, separation, and model-equality-branching regressions.

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
