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

`smt::Goal` is an immutable shared value containing an ordered
`std::vector<core::TermId>` of assertions. The
assertions are a conjunction. A goal with no assertions denotes `true`; a goal
containing the Boolean constant `false` denotes `false`; every other goal is
inconclusive. Transformations construct a new goal with `with_assertions`,
`with_model`, or `with_diagnostic`; unchanged goals reuse their storage. Goal
equality is structural and includes the terminal model, typed inconclusive
reason, diagnostic fields, and model-reconstruction records.

`smt::ModelReconstruction` is the goal attribute used by equisatisfiable
preprocessing. It contains removed Boolean/equality assignments, monotone Real
variables and their discharged constraints, solved QF_LIA definitions, and
forced equality/disequality facts used by later finite-domain passes. The value
is copied into a new immutable goal with `with_reconstruction`; callers receive
only a const reference to the goal-owned value. This prevents a tactic arm from
sharing mutable reconstruction vectors with another arm.

`smt::AnyOf` owns an immutable shared non-empty ordered vector of goals with disjunctive
semantics. It is true when any goal is true, false when every goal is false, and
otherwise inconclusive. An empty `AnyOf` is rejected: truth is represented by
one empty `Goal`, not by overloading the absence of alternatives. This avoids a
separate sat/unsat/residual return variant while retaining the formula semantics.
The vector has no mutable accessor, so construction is the only place where the
non-empty invariant needs enforcement.

`smt::TacticContext` borrows a `core::TermManager` and `util::Statistics`.
Consequently all `TermId` values stored in goals are valid only for the lifetime
of that term manager. Tactics may intern new terms but must never retain a
`Term` reference across a term-manager constructor call. The context owns a
read-only cancellation probe. Every tactic checks it before invoking its
implementation; solver leaves pass the same request to a backend-scoped SAT
terminator.

`smt::Tactic` is a named type-erased callable with the signature
`AnyOf(TacticContext&, const AnyOf&)`. A tactic handle and its implementation
are immutable; inputs share their immutable goal storage and outputs reuse it on
no-op paths. The same semantic type is accepted and returned. Named invocation
counters are emitted under
`:tactic.<name>.invocations`, and elapsed microseconds under
`:time.tactic.<name>-us`; both statistic names are computed once when the tactic
is constructed. Change detection is confined to tacticals such as `repeat` that
require it and immutable handle copies do not duplicate assertion vectors.

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

## Production composition

The repeated preprocessing round is implemented in `preprocessing_tactics` as
a composition of named phase tactics over each input alternative. Its immutable
`PreprocessingRoundOptions` is constructed once from the selected strategy;
the phases do not inspect command-line state. The generic `repeat` tactical
applies it to a structural fixed point, respects the existing baseline pass
limit, and consults the read-only reachable-term-count probe for configured
adaptive extra passes. Equality bridging, finite-domain evaluation, congruence
definitions, Boolean-unit propagation, forced-atom propagation, solved-constant
substitution, ground-unit substitution, and unit-equality propagation each have
their own invocation/time counters. A round deliberately finishes all selected
phases before exposing a terminal goal, preserving the established pass
ordering and reconstruction/statistics behavior. Each output owns its own
reconstruction records. The transformation implementations are unchanged.
Existing command-line options construct this built-in composition; there is no
user-facing tactic expression syntax and option defaults are unchanged.

The Boolean, QF_UF, QF_LRA, QF_LIA, and combined QF_UFLRA solvers are adapted as
formula-returning leaf tactics. A solving leaf represents `sat` by an empty
goal, `unsat` by a goal containing `false`, and `unknown` or unsupported input
by the unchanged inconclusive goal plus a diagnostic. A dispatch tactic probes
the formula features and logic, then invokes the selected leaf. The pure
Boolean path uses the generic first-terminal scheduler with QF_UF as a fallback.
The public `CheckSatOutcome` is derived after dispatch solely from the returned
goal's truth, typed reason, model, and diagnostic; there is no parallel mutable
result channel. This adaptation is isolated in `solver_tactic`, while expected
unsupported-fragment exceptions are defined in `solver_error`; other exceptions
remain serious failures and escape the tactic boundary.
The existing QF_UF preprocessing portfolio remains outside this layer because
each arm deliberately reparses the original SMT-LIB text into a fresh term
manager; `TermId` values and goals must never cross that session boundary.

The scheduler API is independent of its current execution policy. The supplied
implementation runs alternatives sequentially and in declaration order. A
future parallel scheduler can implement the same interface, but it must give
each arm an isolated context, combine only terminal results, propagate
cancellation, and define deterministic statistics before it is enabled.

The current CLI turns `SIGTERM` into a cancellation request only. It performs
no output, allocation, backend access, or statistics access in signal context.
Each SAT solver owns its terminator connection with an RAII guard, which keeps
backend state local to a solve and makes the lifetime boundary suitable for a
future isolated scheduler.

## Deliberate deviations and limitations

The paper's tactic return type separately represents `sat`, `unsat`, `unknown`,
and `fail`. Primo instead derives true, false, or inconclusive from `AnyOf` and
uses an unchanged goal for ordinary inapplicability. `InconclusiveReason`
distinguishes cancellation, resource, unsupported, applicability, and ordinary
unknown reasons without changing the logical value. Broken
contracts and backend failures use exceptions (`TacticError` or the originating
standard exception); they are not tactical alternatives.

The production schedule still operates on exactly one goal because none of the
current transformations introduces a disjunction. Goal-local reconstruction is
now sufficient for a future transformation to return alternatives without
sharing its mutable bookkeeping, but the scheduler still needs isolated term
managers and statistics before those alternatives can be executed in parallel.
Proof converters, unsat-core converters, incremental tactics, a user-facing
tactic language, and true parallel execution are not implemented.

## Invariants and complexity

- `Goal`, its assertion vector, and `AnyOf` are immutable shared values.
- `AnyOf` always contains at least one goal.
- Every assertion is a Bool-sorted `TermId` owned by the context term manager.
- A leaf tactic is not applied to an already true or false goal.
- Sequential fallback gives every arm the same original `AnyOf`, not the
  previous arm's inconclusive output.
- Fixed-point detection first compares shared storage and falls back to ordered
  goal contents. Unchanged iterations are constant time when a leaf returns its
  input; changed iterations may compare the produced assertions linearly. The
  explicit iteration bound prevents nontermination.
- The generic framework adds no whole-term-DAG scans. Such scans belong to
  explicit probes and remain visible in tactic statistics.

The principal proof obligation for every transformation leaf is that the
disjunction of its output goals is equisatisfiable with its input goal. A leaf
that changes the model vocabulary additionally owes a reconstruction operation;
the operation's record travels with that output goal. Solver leaves communicate
their logical result only through the truth represented by their returned goal.
Serious contract violations and unexpected backend failures escape as exceptions
and are reported only at the command-line boundary.

`tests/unit/tactic_tests.cpp` covers terminal classification, disjunctive
alternatives, the non-empty invariant, fixed-point repetition, conditional
selection, statistics, pristine-input fallback, and production dispatch to each
supported solver family. Existing preprocessing and solver suites cover the
migrated control flow with their prior correctness and adaptive-pass assertions.
