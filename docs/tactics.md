# Tactic architecture

Primo's tactic layer is the control boundary between parsed core terms,
preprocessing transformations, and complete or incomplete solving engines. It
is an internal C++ API in its first version; the existing command-line options
still select the built-in strategy.

## Algorithmic basis

The design starts from Leonardo de Moura and Grant Passmore, *The Strategy
Challenge in SMT Solving*, Section 3, "Foundations for Big-Step Strategies"
(LNCS 7788, 2013, DOI `10.1007/978-3-642-36675-8_2`). That section defines a
goal as an ordered formula sequence plus attributes, treats the assertions of a
goal conjunctively, introduces disjunctive subgoals, model conversion, tactics,
tacticals, and formula measures. We also use Sections 8.2--8.6 of Bjørner et
al., *Z3 Internals* for the current classification of equivalence-preserving and
equisatisfiable tactics and the corresponding model-conversion obligation.

This is an original Primo implementation of those interfaces. No source or API
definition was copied from Z3 or another solver.

## Implemented data structures

`smt::Goal` owns an ordered `std::vector<core::TermId>` of assertions. The
assertions are a conjunction. A goal with no assertions denotes `true`; a goal
containing the Boolean constant `false` denotes `false`; every other goal is
inconclusive. Goal equality is structural and includes the terminal model and
diagnostic fields, so fixed-point tacticals can detect every observable change.

`smt::AnyOf` owns a non-empty ordered vector of goals with disjunctive
semantics. It is true when any goal is true, false when every goal is false, and
otherwise inconclusive. An empty `AnyOf` is rejected: truth is represented by
one empty `Goal`, not by overloading the absence of alternatives. This avoids a
separate sat/unsat/residual return variant while retaining the formula semantics.

`smt::TacticContext` borrows a `core::TermManager` and `util::Statistics`.
Consequently all `TermId` values stored in goals are valid only for the lifetime
of that term manager. Tactics may intern new terms but must never retain a
`Term` reference across a term-manager constructor call. The context also owns
the cancellation probe used by future schedulers.

`smt::Tactic` is a named type-erased callable with the signature
`AnyOf(TacticContext&, AnyOf)`. Input is passed by value so a composition can
move assertion vectors forward without a required deep copy. The same semantic
type is accepted and returned. Named invocation and elapsed-microsecond counters
are emitted under `:tactic.<name>.*`. Change detection is confined to tacticals
such as `repeat` that require it, rather than copying every tactic input merely
for instrumentation.

## Implemented tacticals and probes

The first layer provides leaf-goal lifting, `skip`, sequential composition,
conditional selection, bounded repetition to a structural fixed point, an
optional before/after continuation predicate, and ordered first-terminal
fallback. A sequential scheduler implements fallback. Its interface is the
boundary for a later isolated parallel scheduler; this version does not create
threads.

Numeric probes and Boolean predicates are ordinary read-only callables over a
goal and its context. The initial built-in numeric probe reports assertion
count. Preprocessing-specific structural probes are added with the preprocessing
migration rather than hidden inside the generic layer.

## Deliberate deviations and limitations

The paper's tactic return type separately represents `sat`, `unsat`, `unknown`,
and `fail`. Primo instead derives true, false, or inconclusive from `AnyOf` and
uses an unchanged goal for ordinary inapplicability. A diagnostic distinguishes
resource or applicability reasons without changing the logical value. Broken
contracts and backend failures use exceptions (`TacticError` or the originating
standard exception); they are not tactical alternatives.

The current framework carries a final model string on a terminal goal, but the
existing preprocessing reconstruction records have not yet moved into `Goal`.
Until that migration is complete, tactic alternatives must not independently
perform model-changing preprocessing. Proof converters, unsat-core converters,
incremental tactics, a user-facing tactic language, and true parallel execution
are not implemented.

## Invariants and complexity

- `AnyOf` always contains at least one goal.
- Every assertion is a Bool-sorted `TermId` owned by the context term manager.
- A leaf tactic is not applied to an already true or false goal.
- Sequential fallback gives every arm the same original `AnyOf`, not the
  previous arm's inconclusive output.
- Fixed-point detection compares ordered goal contents. One repeat iteration is
  linear in the size of the tactic output in addition to the wrapped tactic's
  own cost; the explicit iteration bound prevents nontermination.
- The generic framework adds no whole-term-DAG scans. Such scans belong to
  explicit probes and remain visible in tactic statistics.

`tests/unit/tactic_tests.cpp` covers terminal classification, disjunctive
alternatives, the non-empty invariant, fixed-point repetition, conditional
selection, statistics, and pristine-input fallback.
