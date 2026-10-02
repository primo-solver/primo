# Research register

An open lead is a question with an implementation or measurement obligation,
not a promised feature. Recheck a historical profile before using it to rank
work. Each investigation must name the input set, correctness conditions,
observable mechanism, and result that would reject the proposal.

The dedicated lead pages in the navigation cover library lifetime, compact
storage, statistics, theory lemmas, arithmetic, combination, and quantifiers.
This register also accounts for the remaining numbered entries in the
{source}`original register <docs/open-leads.md>`. Their full dated reasoning,
negative results and evidence remain preserved; current dispositions follow.

## Remaining investigations

| Original lead | Current question and completion obligation |
| --- | --- |
| 1: arithmetic pure-theory filtering | Determine which arithmetic literals can be omitted under relevance/polarity conditions. The original premise was corrected; preserve that correction and prove validity at theory/model boundaries before implementing. |
| 2 and 6: decision ordering | Investigate backend-supported decision/justification choices. LRA cannot directly replace SAT's variable-selection policy. Define the layer boundary and measure effective decisions before attributing a speedup. |
| 4: adaptive early pruning | Investigate arithmetic check frequency from observed usefulness. Existing EUF propagation backoff is not evidence that the arithmetic proposal is implemented. |
| 5: ground relevance | Extend the [relevance literature](../papers/relevance-filtering.md) beyond the separate quantified-obligation lead. Prove that omitting an assertion remains sound under backtracking and model completion. |
| 8 and H6: difference logic | Design a dedicated graph solver and admission route before claiming `QF_IDL`/`QF_RDL` support. The old negative-cycle example is a future test, not an executable capability example. Static precision bounds require the graph algorithm's assumptions; they do not remove general rational overflow checks. |
| 11: unconstrained terms | Prove occurrence, invertibility, reconstruction and incremental-restoration conditions. A syntactic one-parent shortcut is not by itself a sound elimination criterion. |
| R1: recursion depth | Audit input-sized recursion throughout parsing, rewriting, purification, model construction and tactic execution/destruction. Iterative parsing does not establish whole-solver stack safety. Reproduce remaining paths at the current revision and add bounded regressions before repair. |
| 12b/12c: integer inference | Investigate integer-preserving elimination and stronger Diophantine/cube methods. Keep these distinct from the implemented GCD test, patching, cuts, bounded branching and conflict generalization. |
| 13: mixed Int/Real | Define typing/coercions, admission, preparation, combinations and models. Mixed-tableau APIs alone do not make `QF_LIRA` a frontend-only task or establish completeness. |
| 14: integer monotone elimination | Extend the Real transformation only after an integer reconstruction proof, strict-bound treatment and incremental restrictions are specified. Measure actual Int opportunities. |
| 16: remaining ITE work | CITE simplification is implemented; further compression/reconstruction proposals need separate paper mappings and measurements. Do not relabel the whole old proposal as completed. |
| 18: alternate backtracking | Compare the proposed arithmetic policy with current bounds-only rollback and the SOI exception first. Restoring/recomputing assignments is a choice with invariant and cost obligations, not an automatic missing feature. |
| 19: integer bounding transformations | Investigate termination on partially unbounded integer problems using the Bromberger thesis conditions. Current bounded search can return `unknown`; a branch budget is not a completeness argument. |
| 21: divisibility-aware `div`/`mod` | Current preprocessing expands the supported constant-divisor operations. Stronger divisibility reasoning needs tests and relevant inputs; the old zero-occurrence sample does not establish absence from every future corpus. |
| 22: remaining traversal scratch | Atom/preparation cursors were accepted. Reprofile remaining full-manager scratch initialization; preserve fresh scoped facts and count touched versus retained objects. |
| 23–25: retained UF costs | See [compact distinct](compact-distinct.md), [incremental equation elimination](incremental-equations.md), and [immutable EUF sharing](shared-euf-structure.md). |
| H4, H11 and residual arithmetic directions | Batched assignment updates, preprocessing fill-in selection and related rational/row-layout experiments remain hypotheses. Compare with the current implementation and retain the old rejected prototypes before proposing a replacement. |

Every row retains its original numbered section in the
[section inventory](../coverage/index.md). Missing primary sources block
implementation of the corresponding paper mechanism. None of these rows
authorizes a default change without fresh correctness and performance evidence.

## Reusable tactic probes

The [conditional-tactic interface](../implementation/tactics.md#conditional-predicates-and-probes)
already accepts custom predicates and named numeric/Boolean probes. The missing
piece is a useful shared catalogue beyond `assertion_count()` and `at_most()`;
production selection currently calls richer analyses directly.

- Expose suitable existing measurements as named probes: reachable-term count,
  conjunct structure, theory features, and finite-domain properties. Distinguish
  assertion roots, conjunct occurrences, and unique terms explicitly.
- Add reusable Boolean combinations and numeric comparisons. Give composed
  probes stable identities including their parameters; canonical printing
  currently records names, not the callable implementations. Repair the
  {source}`numeric-limit formatting <src/smt/strategy.cpp#L106-L113>`:
  default stream precision can give distinct `at_most` thresholds the same
  name. Specify a locale-independent representation that distinguishes the
  supported limits, including a policy for non-finite values, and test the
  resulting canonical descriptions. This affects configuration identity;
  no solver-answer error has been established by the documentation review.
- Express appropriate profile choices through conditional strategies, preserving
  logic admission, model reconstruction, and incremental restrictions. A size
  heuristic must not replace a solver's applicability check.
- Specify evaluation cost and lifetime. Read the current goal and executing
  context, avoid formula mutation, and define cache validity across transformed
  goals and different term managers. The existing
  {source}`reachable-term counter <src/smt/formula_metrics.cpp#L11-L32>`
  initializes scratch storage for the whole manager, so repeated scans can be
  costly even for small surviving formulas.

First expose existing decisions without changing defaults. Check probe values
and selected branches after transformation and splitting, then compare with the
current selectors on the supported UF and arithmetic benchmark sets. Record
probe cost, branch counts, answers, validated models, solved counts, and PAR2;
run the preprocessing fuzz gate before accepting behavior changes. Reject a
selection policy whose analysis cost outweighs its benefit or which regresses
correctness. No performance improvement is claimed for this proposal.

## Implemented or closed investigations

| Original entry | Disposition |
| --- | --- |
| 2b: dispatch regression | Repaired at the recorded historical revision; retain the diagnosis in the evidence archive. |
| 3: least-violated leaving rule | Implemented as an option; the original comparison did not justify promotion. See [LRA](../implementation/lra.md). |
| 3b and 7: occurrence/row layout | The current LRA chapter describes the implemented sorted rows and occurrence maintenance. Hash-set and earlier indexed-removal prototypes remain rejected history. |
| 9: Real monotone elimination | Implemented, optional. Later full-corpus measurements differ from the early small-sample rejection; see [arithmetic evidence](../evidence/arithmetic.md). |
| 10: model-unit equality producer | Implemented as an optional producer. Legacy remains default; this is not an unimplemented algorithm. |
| 12a/12f | Integer-safe preprocessing and patching have current explanations. The old claim that all integer preprocessing is disabled is superseded by the route-specific policies. |
| 12d: integer conflict generalization | Implemented and enabled by default. {ref}`Integer explanations <lia-explanations>` collect the reasons used by the search; {source}`generalized_lia_lemma <src/smt/solver_engines.cpp#L8334-L8360>` checks the result and retains the coarse blocking clause as a fallback. |
| 15: bridge atoms | Removed. Do not restore it as an available alternative or open migration task. |
| 20: model-phase evaluation | Evaluated on arithmetic divisions; current defaults remain off. Any promotion proposal needs new, route-specific evidence. |
| Shared assignment ownership and rollback markers | Implemented/repaired; theory undo logs remain separate. See [DPLL(T)](../implementation/dpllt.md). |

## Low-level and cross-cutting leads

SAT literal exhaustion remains a low-priority hardening question: specify the
backend-compatible limit, reject before overflow, and test negation/indexing
boundaries with an artificial small capacity. No overflow was observed in the
documentation review. Keep compact literal storage while investigating; see
[SAT representation](../implementation/sat.md).

General polarity-aware encoding remains open beyond the existing root handling;
its use conditions, incremental upgrades and theory/model obligations are in
[the literature note](../papers/polarity-aware-cnf.md). The rejected root-only
prototype is not the same experiment.

Unresolved experiment identities and checking coverage belong in
[evidence work](../work/evidence.md), not in the implemented manual.
