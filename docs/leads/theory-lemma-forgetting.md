# Safe forgetting of theory lemmas

**Status: Open lead. Owner: unassigned.**

The literature and callback interfaces have been inspected. Investigation of
the effective deletion policy and its costs has not started.

Investigate whether selected theory lemmas can be made eligible for backend
deletion to control memory and Boolean propagation cost while preserving
correct answers, valid explanations, and termination.

## Literature basis and boundary

The [clause-forgetting note](../papers/clause-forgetting.md) covers the SMT
calculus of Nieuwenhuis–Oliveras–Tinelli (2006), the verified SAT framework of
Blanchette et al. (2018), and the deletion-policy experiments of Gstrein et al.
(2025). It records the exact sections read and separates sufficient termination
conditions from empirical heuristics.

Forgetting can preserve logical equivalence while losing search progress.
The theory's ability to derive a deleted lemma again is insufficient by itself.
A policy must also explain why repeated learning, deletion, and restarting
cannot cycle indefinitely in its claimed scope. Clauses still needed as live
reasons have a separate lifetime constraint.

The [current-code map](../papers/clause-forgetting.md#representation-and-current-primo-boundary)
identifies the callback flags, explanation stores, adapter boundary, and the
component route that does not preserve the callback's forgettability value.
The component dispatcher sets
`is_forgettable = true`, whereas the dedicated EUF/LRA conflict callbacks set
it false. Some current routes therefore already permit forgetting; this lead
must audit and explain existing behavior as well as any proposed extension.
Start by auditing those routes and the effective CaDiCaL configuration.
The SAT backend continues to own clause reduction and watch maintenance.

## First investigation and decision

- Attribute clause memory, reason retention, and repeated theory work on a
  fixed-atom QF_UF workload and long incremental sessions. Keep theory conflicts,
  propagation reasons, and final CDCL learned clauses separate in the accounting.
- Classify which clauses are semantically redundant, which remain live reasons,
  and which depend on scoped assertions. Preserve original constraints and guards.
- Write an explicit progress argument for any candidate policy, including
  restarts and the outer SMT refinement loop. Record unsupported atom-generation
  or theory-search cases instead of applying a finite-atom theorem to them.
- Only if memory or propagation cost justifies a change, design an opt-in
  experiment through supported backend controls. Follow the literature note's
  [design obligations](../papers/clause-forgetting.md#design-obligations-for-the-open-lead),
  relevant correctness gates, and a reproducible comparison before promotion.

Reject a candidate if it cannot preserve the callback/progress invariants,
if it merely moves memory into another store, or if additional rederivation
and repeated conflicts erase the measured benefit.

This refines the deletion question in
[theory-lemma retention](theory-lemmas.md#which-theory-lemmas-should-survive).
Coordinate memory attribution with [incremental memory](incremental-memory.md);
compact term storage and reference counting address different owners.
