# Tactic reference

This page is the inventory of formula-valued tactics and tacticals currently in
Primo. It complements [](tactics.md), which defines their semantics, ownership,
and invariants. The inventory distinguishes four levels of modularity:

- **public builder**: declared in a public `src/smt/*.hpp` header and available
  to other SMT-layer strategy code;
- **production composite**: a named tactic assembled and used by the current
  built-in strategy;
- **internal phase**: a named tactic with its own counters, but constructible
  only as part of a larger composite;
- **session leaf**: adapted to the tactic contract for each `check-sat`, but its
  concrete solver closure is assembled by the solver-leaf module.

The names below are also the names used in
`:tactic.<name>.invocations` and `:time.tactic.<name>-us` statistics whenever a
named tactic is invoked.

## Generic tacticals

Executable builders are declared in `src/smt/tactic.hpp`; the immutable
inspection/composition layer is declared in `src/smt/strategy.hpp`.

| Builder or type | Operation | Termination and branching behavior |
|---|---|---|
| `make_goal_tactic(name, leaf)` | Lifts a single-goal transformation to `AnyOf` | Applies the leaf independently to every inconclusive alternative and preserves terminal alternatives |
| `skip()` | Identity tactic, named `skip` | Returns the same immutable `AnyOf` |
| `sequence(name, tactics)` | Ordered composition | Passes each result to the next tactic and stops when the combined result is terminal |
| `conditional(name, predicate, yes, no)` | Per-goal branch | Evaluates a read-only predicate and runs exactly one branch |
| `repeat(name, tactic, limit, ..., observer)` | Bounded fixed-point iteration | Stops on equivalence, a terminal result, a continuation decision, or the iteration bound; the optional observer sees every completed iteration |
| `first_terminal(name, scheduler, tactics)` | Portfolio fallback | Gives every arm the same original input and returns the first terminal result, or the last inconclusive result |
| `SequentialTacticScheduler` | Execution policy for `first_terminal` | Runs arms sequentially in declaration order; there is no parallel scheduler yet |

`Strategy` provides `Leaf`, `Sequence`, `Conditional`, `Repeat`, `TryInOrder`,
`TryFor`, and `Skip` nodes, plus canonical printing and a stable `fnv1a64:`
fingerprint. `compile(strategy)` lowers the immutable AST to the executable
tacticals above. Strategy construction is C++-only.

`assertion_count()` is the exported named numeric probe and `at_most(...)`
constructs a named Boolean probe. The
solver dispatcher additionally computes a `FormulaFeatureSummary`, and the
preprocessing repeat uses reachable-term counts to decide whether adaptive
passes are worthwhile. Those two analyses are currently embedded in their
respective strategies rather than exported as reusable probe objects.

## Preprocessing tactics

### Composites

| Runtime name | API status | Contents |
|---|---|---|
| `normalize-boolean` | Public typed builder | Initial or final associative flattening; the final stage first applies configured implication normalization |
| `propagate-values` | Public typed builder | Boolean-unit propagation followed by forced-theory-atom propagation |
| `solve-eqs` | Public typed builder | Solved constants, optional Real Gaussian candidates, and configured ground-unit substitution |
| `derive-equalities` | Public typed builder | Equality bridging and/or positive unit-equality consequences |
| `finite-domain-preprocess` | Public typed builder | Selected equality evaluation and congruence phases plus the inseparable symmetry, AMO, and equality-definition choreography |
| `normalize-arithmetic` | Public typed builder | Arithmetic-atom canonicalization and/or Real-equality lowering at a typed pipeline stage |
| `eliminate-monotone` | Public typed builder | Monotone-variable elimination with an ordered model-conversion record when model production is requested |
| `purify-mixed` | Public typed builder | Mandatory mixed-theory purification and its dependent comparison-only arithmetic recanonicalization |
| `preprocessing-round` | Public builder: `make_preprocessing_round(PreprocessingRoundOptions)` | Runs the selected phases below in a fixed order and deliberately completes the round even if an intermediate phase makes the formula terminal |
| `preprocessing-profile` | Production strategy leaf | Records the immutable selected `PreprocessingProfile`; it does not transform the goal |
| `preprocessing-fixed-point` | Production `Repeat` strategy node | Repeats `preprocessing-round` to ordered-assertion convergence, subject to baseline and adaptive reachable-term shrink limits |

`PreprocessingRoundOptions` is immutable after strategy construction. It
selects phases without allowing them to read command-line state.

### Named phases inside `preprocessing-round`

| Runtime name | Enabled when | Formula effect | API status |
|---|---|---|---|
| `equality-bridge` | Equality bridging is selected | Adds equality consequences recognized under supported Boolean structure | Internal phase |
| `finite-domain-evaluation` | `use_finite_domain_evaluation` is set | Simplifies enumerated finite-domain equalities | Internal phase; the current production strategy always leaves this setting off |
| `congruence-definitions` | Ground congruence generation is selected | Adds bounded congruence clauses for existing ground applications | Internal phase |
| `boolean-unit-propagation` | Always | Propagates Boolean units and records assignments needed for model reconstruction | Internal phase |
| `forced-atom-propagation` | Always | Propagates forced theory atoms and records equality/disequality facts | Internal phase |
| `solved-constant-substitution` | Solved-constant substitution is selected | Eliminates accepted acyclic definitions and records substitutions | Internal phase |
| `ground-unit-substitution` | Unguarded or guarded ground substitution is selected | Substitutes positive ground unit equalities; the guarded mode accepts only its bounded shrink criteria | Internal phase |
| `unit-equality-propagation` | Always | Adds equality consequences from positive unit equalities | Internal phase |

These fine-grained phase factories remain private. They are reachable through
the meaningful public composites above, whose typed option records are the
supported composition API. Their separate runtime names provide observability,
not permission to split operations that share an analysis or reconstruction
contract. The legacy completed-pass statistic is emitted by the repeat observer;
`preprocessing-pass` is no longer an executable tactic. Both
`:preprocessing.passes` and the compatibility counter
`:tactic.preprocessing-pass.invocations` count completed repeat iterations.

Gaussian elimination is **not** a separate tactic. It is an optional candidate
source inside `solved-constant-substitution`; syntactic candidates, Gaussian
candidates, cycle rejection, size guards, rewriting, and model reconstruction
share the same transformation boundary. Disabling solved-constant substitution
therefore also prevents that Gaussian candidate path from running.

## Solver tactics

| Runtime name | Selection | API status |
|---|---|---|
| `solver-dispatch` | Classifies each goal by declared logic and formula features | Public builder: `make_solver_dispatch(...)` |
| `boolean-then-euf` | Used for formulas with no detected equality, application, or arithmetic; tries Boolean solving and then QF_UF on the same original goal | Production `first_terminal` composite internal to the dispatcher |
| `solve-boolean` | Pure propositional conjunction | Solver-leaf module |
| `solve-qf-uf` | Equality, uninterpreted applications, or Boolean-UF predicates without arithmetic | Solver-leaf module |
| `solve-qf-lra` | Arithmetic without the mixed EUF features | Solver-leaf module |
| `solve-qf-lra-model-only` | No theory atoms, but declared Real constants require a model | Solver-leaf module |
| `solve-qf-uflra` | Both arithmetic and equality or uninterpreted-predicate features | Solver-leaf module |
| `solve-qf-lia` | Declared `QF_LIA` logic | Solver-leaf module |

`make_solver_tactic(name, terms, solve)` is the common formula adapter.
`make_session_solver_strategy` in `solver_leaves` owns all six concrete leaf
closures, their model-term selection, conversion-chain forwarding, and the
typed `SolverAnswer` boundary. `solver_engines` exposes the complete
single-goal QF_UF, QF_LRA, QF_LIA, and QF_UFLRA engine entry points and
contains their implementations and shared theory/model support. The
session supplies only its frontend, immutable solve options, statistics, and
execution context, then compiles the returned `solver-dispatch` strategy leaf.
The executable leaf borrows the frontend, statistics, and context through that
single execution; immutable solve options are held in shared value storage.
Model-term selection and the `solve-qf-lra-model-only` dispatch branch consult
the context capability. Model production is intentionally absent from the
canonical strategy parameters and fingerprint.

Every solver leaf returns a typed internal `SolverAnswer` through the
formula-valued contract:
an empty goal for `sat`, a goal containing `false` for `unsat`, and an
inconclusive goal with a typed reason for `unknown`, cancellation, resource
limits, or unsupported input. Message text is presentation-only. For an
`AnyOf`, leaves stop at the first satisfiable alternative, collapse all-unsat to
one false goal, and otherwise preserve inconclusive alternatives.

## Transformations not yet represented as tactics

The following production control step remains outside formula-level strategy
composition and must not be mistaken for an available formula tactic:

- the experimental QF_UF session portfolio, implemented by
  `execute_qf_uf_session_portfolio` over the `SessionExecutor` interface. Its
  sequential implementation reparses into fresh solver sessions and therefore
  cannot pass a `Goal` across arms. Only the first arm's typed
  `ResourceLimit` triggers fallback; cancellation and ordinary unknown stop.

These are migration candidates, not a commitment that each should become a
separate tactic. A useful portfolio boundary must have an independently useful
configuration, a stable formula/reconstruction contract, meaningful cost or
benefit variation, and its own measurements. Small implementation stages that
cannot safely be reordered should remain inside one tactic. In particular,
splitting candidate discovery from substitution merely to increase the tactic
count would expose an unsafe and unhelpful portfolio action.

## Current limitations for portfolio work

- Initial normalization executes before profile analysis, so production
  evaluates the prepared full strategy as a prefix and suffix rather than with
  one call to `compile`.
- Only sequential formula-tactic and session-portfolio executors exist.
- Strategy probes are partly embedded in `preprocessing_strategy.cpp` and
  `solver_strategy.cpp` rather than collected behind a reusable probe API.
- There is no tactic-expression language, tactic registry, parser for the
  canonical representation, or per-arm isolated statistics record. Canonical
  strategy text and fingerprints are inspection identities only.
- All alternatives in one `AnyOf` share a `TermManager`; session-level arms that
  reparse input require isolated contexts and cannot exchange `TermId` values.

These limitations define the next modularization work needed before learned or
hand-built portfolios can treat tactic pipelines as stable selectable actions.
