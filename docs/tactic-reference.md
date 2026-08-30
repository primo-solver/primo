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
  concrete solver closure is still assembled in `solver_session.cpp`.

The names below are also the names used in
`:tactic.<name>.invocations` and `:time.tactic.<name>-us` statistics whenever a
named tactic is invoked.

## Generic tacticals

These builders are declared in `src/smt/tactic.hpp`.

| Builder or type | Operation | Termination and branching behavior |
|---|---|---|
| `make_goal_tactic(name, leaf)` | Lifts a single-goal transformation to `AnyOf` | Applies the leaf independently to every inconclusive alternative and preserves terminal alternatives |
| `skip()` | Identity tactic, named `skip` | Returns the same immutable `AnyOf` |
| `sequence(name, tactics)` | Ordered composition | Passes each result to the next tactic and stops when the combined result is terminal |
| `conditional(name, predicate, yes, no)` | Per-goal branch | Evaluates a read-only predicate and runs exactly one branch |
| `repeat(name, tactic, limit, ...)` | Bounded fixed-point iteration | Stops on equivalence, a terminal result, a continuation decision, or the iteration bound |
| `first_terminal(name, scheduler, tactics)` | Portfolio fallback | Gives every arm the same original input and returns the first terminal result, or the last inconclusive result |
| `SequentialTacticScheduler` | Execution policy for `first_terminal` | Runs arms sequentially in declaration order; there is no parallel scheduler yet |

`assertion_count_probe()` is the only exported generic numeric probe. The
solver dispatcher additionally computes a `FormulaFeatureSummary`, and the
preprocessing repeat uses reachable-term counts to decide whether adaptive
passes are worthwhile. Those two analyses are currently embedded in their
respective strategies rather than exported as reusable probe objects.

## Preprocessing tactics

### Composites

| Runtime name | API status | Contents |
|---|---|---|
| `preprocessing-round` | Public builder: `make_preprocessing_round(PreprocessingRoundOptions)` | Runs the selected phases below in a fixed order and deliberately completes the round even if an intermediate phase makes the formula terminal |
| `preprocessing-fixed-point` | Production composite assembled in `solver_session.cpp` | Repeats `preprocessing-round` to ordered-assertion convergence, subject to baseline and adaptive pass limits |

`PreprocessingRoundOptions` is immutable after strategy construction. It
selects phases without allowing them to read command-line state.

### Named phases inside `preprocessing-round`

| Runtime name | Enabled when | Formula effect | API status |
|---|---|---|---|
| `preprocessing-pass` | Always | Increments the completed-pass counter; no formula change | Internal phase |
| `equality-bridge` | Equality bridging is selected | Adds equality consequences recognized under supported Boolean structure | Internal phase |
| `finite-domain-evaluation` | `use_finite_domain_evaluation` is set | Simplifies enumerated finite-domain equalities | Internal phase; the current production strategy always leaves this setting off |
| `congruence-definitions` | Ground congruence generation is selected | Adds bounded congruence clauses for existing ground applications | Internal phase |
| `boolean-unit-propagation` | Always | Propagates Boolean units and records assignments needed for model reconstruction | Internal phase |
| `forced-atom-propagation` | Always | Propagates forced theory atoms and records equality/disequality facts | Internal phase |
| `solved-constant-substitution` | Solved-constant substitution is selected | Eliminates accepted acyclic definitions and records substitutions | Internal phase |
| `ground-unit-substitution` | Unguarded or guarded ground substitution is selected | Substitutes positive ground unit equalities; the guarded mode accepts only its bounded shrink criteria | Internal phase |
| `unit-equality-propagation` | Always | Adds equality consequences from positive unit equalities | Internal phase |

None of these phase factories is public yet. `preprocessing_tactics.cpp` creates
them through a private phase adapter, so callers can select them only through
`PreprocessingRoundOptions` and cannot independently order or portfolio them.
Their separate runtime names currently provide observability, not a complete
composition API.

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
| `solve-boolean` | Pure propositional conjunction | Session leaf |
| `solve-qf-uf` | Equality, uninterpreted applications, or Boolean-UF predicates without arithmetic | Session leaf |
| `solve-qf-lra` | Arithmetic without the mixed EUF features | Session leaf |
| `solve-qf-lra-model-only` | No theory atoms, but declared Real constants require a model | Session leaf |
| `solve-qf-uflra` | Both arithmetic and equality or uninterpreted-predicate features | Session leaf |
| `solve-qf-lia` | Declared `QF_LIA` logic | Session leaf |

`make_solver_tactic(name, terms, solve)` is the public adapter shared by these
leaves. The concrete leaf closures and their per-session dependencies are still
created in `solver_session.cpp`; there are not yet independent public factories
such as `make_qf_lra_tactic()`.

Every solver leaf returns its answer through the formula-valued contract:
an empty goal for `sat`, a goal containing `false` for `unsat`, and an
inconclusive goal with a typed reason for `unknown`, cancellation, resource
limits, or unsupported input.

## Transformations not yet represented as tactics

The following production preprocessing or control steps still run directly in
`solver_session.cpp` and must not be mistaken for available portfolio arms:

- preprocessing-profile analysis and `auto` profile selection;
- the initial and catch-up arithmetic-atom canonicalization passes;
- pre- and post-preprocessing associative Boolean flattening;
- the QF_LIA-specific and large-assertion-bypass solved-constant loops;
- finite-domain at-most-one generation, UF symmetry breaking, and finite-domain
  equality-definition generation;
- Real-equality lowering and monotone-variable elimination;
- mixed-theory purification and its comparison-only recanonicalization;
- Boolean implication rewriting; and
- the experimental QF_UF portfolio, which reparses into fresh solver sessions
  and therefore cannot pass a `Goal` across arms.

These are migration candidates, not a commitment that each should become a
separate tactic. A useful portfolio boundary must have an independently useful
configuration, a stable formula/reconstruction contract, meaningful cost or
benefit variation, and its own measurements. Small implementation stages that
cannot safely be reordered should remain inside one tactic. In particular,
splitting candidate discovery from substitution merely to increase the tactic
count would expose an unsafe and unhelpful portfolio action.

## Current limitations for portfolio work

- Individual preprocessing phases cannot yet be freely reordered or selected
  through public builders.
- Only sequential first-terminal scheduling exists.
- Strategy probes are partly embedded in `solver_session.cpp` and
  `solver_strategy.cpp` rather than collected behind a reusable probe API.
- There is no tactic-expression language, tactic registry, stable serialized
  pipeline identifier, or per-arm isolated statistics record.
- All alternatives in one `AnyOf` share a `TermManager`; session-level arms that
  reparse input require isolated contexts and cannot exchange `TermId` values.

These limitations define the next modularization work needed before learned or
hand-built portfolios can treat tactic pipelines as stable selectable actions.
