# de Moura-Bjorner Model-Based Theory Combination

Status stamp: 2026-08-01, branch `nelson-oppen-closure-methods`, current
documentation/code baseline after commit `ba889e6`.

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
- `TheoryCombinationManager` bookkeeping for interface equality pairs;
- partial mixed QF_UFLRA final checks over EUF and Real LRA atoms;
- lazy SAT-visible equality atoms for shared Real terms;
- guarded equality clauses when EUF or LRA implies a shared Real equality;
- bounded shared-Real separation for the documented model-printing subset;
- default-off `--model-equality-branching`, which keeps surviving
  LRA-model-equal shared Real equality guesses on an active SAT-assumption trail.

The missing parts are the paper's central mechanisms:

- no generic model object carried by the DPLL(T) state;
- no generic per-theory API for model equality queries;
- no full `M-Propagate` transition;
- no `M-Mutate` transition;
- no theory-independent implementation for more than EUF plus Real LRA;
- no completeness claim for general Nelson-Oppen combination.

## Section-by-Section Comparison

| Paper section | Paper content | Primo status |
| --- | --- | --- |
| Abstract | The paper proposes reconciling candidate models from component theories instead of eagerly propagating every implied equality or eagerly adding all shared equality literals. | **Partial.** Primo has model printing and partial QF_UFLRA reconciliation, but no full model-based combination engine. |
| 1 Introduction | The motivating example is that a theory model can reveal that two shared variables have equal values. The solver can use such model equalities to guide equality sharing between theories. | **Partial scaffold.** `--model-equality-branching` can inspect the LRA model and keep surviving equality guesses between shared Real terms on an active SAT-assumption trail, but it is still optional SAT branching, not the paper's full model-propagation transition. |
| 2 Background | Defines signatures, formulas, theories, structures, satisfaction, equality, stable infiniteness, convexity, and combination terminology. | **Conceptual only.** Primo has typed terms, EUF, LRA, and model fragments, but no explicit theory-combination contract that records stable infiniteness, convexity, or model-production obligations per theory. |
| 2.1 Nelson-Oppen Combination Method | Reviews purification, shared variables, arrangements, and equality propagation between disjoint theories. For convex theories, propagating implied equalities can avoid arrangement enumeration. | **Partial.** Primo has QF_UFLRA purification and lazy equality sharing for shared Real terms. It is not full Nelson-Oppen: it lacks general bidirectional equality propagation, full arrangements, fine-grained cross-theory explanations, and generic multi-theory support. |
| 3 Related Work | Places the approach against Shostak-style combination, delayed theory combination, Ackermannization, and dynamic Ackermannization. The paper's point is that existing alternatives can add too many literals or too many lemmas. | **Mostly not implemented as paper mechanisms.** Primo has separate EUF/Ackermann diagnostics and separate QF_UF disequality-propagation work, but these are not the model-based combination algorithm. |
| 3.1 Convex Theories | Describes equality propagation for convex theories and notes that complete enumeration of all theory-implied equalities can be expensive. | **Partial, different shape.** Primo detects some EUF- and LRA-implied shared Real equalities during mixed final checks and learns guarded clauses. It does not implement a complete eager equality-propagation engine. |
| 3.2 Delayed Theory Combination | DTC creates Boolean equality literals for shared variables and lets SAT search over arrangements. This avoids special theory-combination rules but may create a quadratic number of literals. | **Not implemented as DTC.** Primo creates shared Real equality atoms lazily when a theory implication or model-equality branch needs them; it does not eagerly add all pair literals. |
| 3.3 Ackermannization | Ackermann reduction replaces UF reasoning with constraints over applications and congruence axioms. The paper treats it as useful in some cases but potentially expensive. | **Separate from this work.** Primo's Ackermann and disequality-propagation options are EUF-side mechanisms. They are not integrated as the paper's model-combination method. |
| 3.3.1 Dynamic Ackermannization | Dynamic Ackermannization adds Ackermann constraints during search, especially when disequalities between applications make argument disequalities relevant. | **Separate/partial.** This belongs to the QF_UF disequality propagation line, not to current mixed QF_UFLRA model-based combination. |
| 4 Model-Based Theory Combination | This is the core contribution. A theory solver maintains or exposes a candidate model. If two shared terms are equal in that model, the combination layer may propagate that equality. If the equality is bad for another theory, search backtracks or the model is mutated. | **Not fully implemented.** Primo has only a default-off LRA-model-equality assumption loop for the QF_UFLRA coordinator. There is no generic `DPLL(T^M)` state, no generic model-equality propagation, and no model mutation. |
| 5 Simplex: An Example Model-Producing Theory Solver | The paper shows how Simplex naturally provides a model: the current assignment maps arithmetic variables to rational values. Equal-valued shared variables are equality candidates. | **Partial.** Primo's LRA path has a tableau assignment and can produce values for user-declared nullary Real constants and selected shared Real terms. It is not yet a complete Simplex-backed combination API. |
| 5.1 Opportunistic Equality Propagation | The paper describes cheaper equality-detection opportunities in Simplex, such as fixed variables and row-derived equalities. | **Partial, not the paper algorithm.** Primo has LRA propagation and bound reasoning, but it does not implement the paper's opportunistic equality rules as named combination operations. Current LRA-to-EUF equality detection is still a final-check-oriented mechanism. |
| 5.2 Model Mutation | The paper describes mutating arithmetic model values, using freedom intervals, to avoid unnecessary equalities before propagating them to other theories. | **Not implemented.** Primo uses LRA model values for model printing, separation orientation, and optional equality guesses. It does not mutate the LRA model to reduce equality candidates. |
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
  interface-equality registry and diagnostic scaffold.
- `src/smt/check_sat.cpp`: `solve_qf_uf_lra`, lazy generated equality literals,
  mixed final checks, model reconstruction, and default-off model-equality
  branching.
- `tests/unit/qf_lra_solver_tests.cpp`: QF_UFLRA model-printing, shared-Real
  equality propagation, separation, and model-equality-branching regressions.

## Documentation Claim To Use

Use this wording when summarizing the current state:

> Primo implements prerequisites and a narrow experimental QF_UFLRA
> model-equality assumption loop inspired by de Moura and Bjorner's
> model-based theory-combination paper. It does not yet implement the full
> `DPLL(T^M)` procedure, generic model mutation, or complete Nelson-Oppen
> model-equality propagation.

Avoid saying that Primo implements "the de Moura-Bjorner algorithm" without the
qualification above.
