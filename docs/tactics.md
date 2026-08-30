# Tactic architecture

Primo's tactic layer is the control boundary between parsed core terms,
preprocessing transformations, and complete or incomplete solving engines. It
is an internal C++ API in its first version; the existing command-line options
still select the built-in strategy.

The concrete inventory of generic tacticals, named preprocessing phases,
solver leaves, and transformations that have not yet migrated is maintained in
[](tactic-reference.md). The distinction between an independently constructible
tactic and an internal named phase is intentional: a statistics name alone does
not make a phase a portfolio arm.

## Algorithmic basis

The design starts from Leonardo de Moura and Grant Passmore, *The Strategy
Challenge in SMT Solving*, Section 3, "Foundations for Big-Step Strategies"
(LNCS 7788, 2013, DOI `10.1007/978-3-642-36675-8_2`). That section defines a
goal as an ordered formula sequence plus attributes, treats the assertions of a
goal conjunctively, introduces disjunctive subgoals, model conversion, tactics,
tacticals, and formula measures. We also use Sections 8.2--8.6 of Bjørner et
al., *Z3 Internals* for the current classification of equivalence-preserving and
equisatisfiable tactics and the corresponding model-conversion obligation.
FastSMT's learned tactic sequences, probe-based branching, and experimental
action sets are surveyed separately in [](papers/fastsmt-strategy-synthesis.md);
that paper motivates later portfolio machinery but is not the semantic basis of
the current API. Evidence from generated configurations in earlier and later
strategy-synthesis work is compared in
[](papers/tactic-configuration-evidence.md).

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

The immutable data record gives the assertion vector, optional model string,
and reconstruction value separate `shared_ptr<const ...>` ownership. Updating
one field shallow-copies the small record and allocates only that field; the
other potentially large payloads retain their identity. Empty payloads use
shared singleton values. Reasons and their short diagnostics remain inline.
There is no mutable copy-on-write path.

`smt::ModelReconstruction` is the goal attribute used by equisatisfiable
preprocessing. Its `ModelConversionChain` is one execution-ordered sequence of
typed steps: Boolean/theory assignments, directed equality substitutions,
QF_LIA substitutions, and monotone-variable eliminations. A transformation
persistently appends one step. Model builders replay the chain in reverse tactic
order, and a multi-record step such as directed substitution also replays its
own postsolve log in reverse. This is necessary for arbitrary sequential
composition: a later elimination may remove a value needed by an earlier one.
The current string model builders use a compatibility projection only when
feeding unordered EUF closure facts; Real and QF_LIA postsolve paths consume the
ordered chain directly.

Forced equality/disequality facts used by later finite-domain analysis remain
separate because they are not model conversions. The reconstruction value is
moved into immutable goal storage with `with_reconstruction`; callers receive
only a const reference. This prevents tactic arms from sharing mutable
bookkeeping while allowing assertion-only rewrites to share the untouched
payload. The chain reserves node kinds for model reconstruction only; proof and
unsat-core converters remain future work.

`smt::AnyOf` owns an immutable shared non-empty ordered vector of goals with disjunctive
semantics. It is true when any goal is true, false when every goal is false, and
otherwise inconclusive. An empty `AnyOf` is rejected: truth is represented by
one empty `Goal`, not by overloading the absence of alternatives. This avoids a
separate sat/unsat/transformed-goal return variant while retaining the formula semantics.
The vector has no mutable accessor, so construction is the only place where the
non-empty invariant needs enforcement.

`smt::TacticContext` borrows a `core::TermManager` and `util::Statistics`.
Consequently all `TermId` values stored in goals are valid only for the lifetime
of that term manager. Tactics may intern new terms but must never retain a
`Term` reference across a term-manager constructor call. The context owns a
read-only cancellation probe, the model-production capability for that
execution, and an optional cooperative steady-clock deadline. `TryFor` installs
the deadline with an RAII scope; nested scopes retain the earliest deadline and
restore the outer value on exit. Deadlines and model requests are execution
capabilities, not tactic parameters and therefore do not change strategy
identity. Every tactic checks cancellation and deadlines before invoking its
implementation; solver leaves pass cancellation to a backend-scoped SAT
terminator. Long-running leaves must additionally poll `interrupted()` at their
existing cooperative interruption points.

`smt::Tactic` is a named type-erased callable with the signature
`AnyOf(TacticContext&, const AnyOf&)`. A tactic handle and its implementation
are immutable; inputs share their immutable goal storage and outputs reuse it on
no-op paths. The same semantic type is accepted and returned. Named invocation
counters are emitted under
`:tactic.<name>.invocations`, and elapsed microseconds under
`:time.tactic.<name>-us`; both statistic names are computed once when the tactic
is constructed. Change detection is confined to tacticals such as `repeat` that
require it and immutable handle copies do not duplicate assertion vectors.

## Immutable strategy and compilation

`smt::Strategy` is an immutable shared AST compiled into the executable
`smt::Tactic` type. Its node set is deliberately closed to `Leaf`, `Sequence`,
`Conditional`, `Repeat`, `TryInOrder`, `TryFor`, and `Skip`; there is no `AllOf`
node and no user-facing parser. A leaf combines a stable identifier, canonical
key-sorted parameters supplied by its typed builder, and an immutable tactic
handle. Structural nodes own their children by value through shared immutable
records.

`Strategy::canonical()` prints a deterministic parenthesized representation.
Strings are quoted and escaped, leaf parameters are sorted and required to
have unique non-empty keys, and durations are printed as integer milliseconds.
`fingerprint()` is the `fnv1a64:` digest of that canonical text. The digest is a
compact stable inspection key, not a cryptographic or collision-free identity;
tools that require exact identity retain the canonical text. Neither surface
has a CLI or configuration-file parser in this refactor.

Compilation recursively lowers the AST to the existing tacticals. It does not
mutate the strategy and execution state remains in `TacticContext`. The
production pipeline is migrated to constructed strategies separately so this
foundation does not itself alter the established tactic ordering.

## Implemented tacticals and probes

The first layer provides leaf-goal lifting, `skip`, sequential composition,
conditional selection, bounded repetition to a structural fixed point, an
optional before/after continuation predicate, an optional equivalence relation,
and ordered first-terminal fallback. The default equivalence remains complete
structural goal equality. A repeat returns the latest equivalent output so
attribute-only results such as cancellation are not discarded. Its continuation
predicate observes every non-equivalent output, including terminal ones; its
return value controls another iteration only for inconclusive output. A
sequential scheduler implements fallback. Its interface is the
boundary for a later isolated parallel scheduler; this version does not create
threads.

Numeric and Boolean probes are immutable named read-only callables over a goal
and its context. Their stable names participate in canonical strategy printing.
The built-in `assertion-count` numeric probe and the named `at_most` Boolean
comparison demonstrate dynamic selection: a conditional evaluates its probe on
the current transformed goal received at that node, never on a captured copy of
the original input. Preprocessing-specific structural probes are added with the
preprocessing migration rather than hidden inside the generic layer.

## Production composition

The repeated preprocessing round is implemented in `preprocessing_tactics` as
a composition of named phase tactics over each input alternative. Its immutable
`PreprocessingRoundOptions` is constructed once from the selected strategy;
the phases do not inspect command-line state. The generic `repeat` tactical
applies it to an ordered-assertion fixed point, respects the existing baseline pass
limit, and consults the read-only reachable-term-count probe for configured
adaptive extra passes. Equality bridging, finite-domain evaluation, congruence
definitions, Boolean-unit propagation, forced-atom propagation, solved-constant
substitution, ground-unit substitution, and unit-equality propagation each have
their own invocation/time counters. A round deliberately finishes all selected
phases before exposing a terminal goal, preserving the established pass
ordering and reconstruction/statistics behavior. Each output carries its own
immutable reconstruction value; outputs that do not change it may share the
same backing storage. Phase adapters read assertions directly from the
immutable input and copy assertion or reconstruction containers only when that
phase must extend them; counting and unchanged append-only phases return the
shared input goal. The transformation implementations are unchanged.
Existing command-line options construct this built-in composition; there is no
user-facing tactic expression syntax and option defaults are unchanged.

The Boolean, QF_UF, QF_LRA, QF_LIA, and combined QF_UFLRA solvers are adapted as
formula-returning leaf tactics. A solving leaf represents `sat` by an empty
goal, `unsat` by a goal containing `false`, and `unknown` or unsupported input
by the unchanged inconclusive goal plus a typed reason and diagnostic. The
internal `SolverAnswer` carries the result and reason; diagnostic text is never
examined to select fallback behavior. A solver leaf processes disjunctive goals
in order, stops at the first satisfiable alternative and retains that model,
collapses an all-unsatisfiable set to one false goal, and otherwise returns the
remaining inconclusive alternatives. A dispatch tactic probes
the formula features and logic, then invokes the selected leaf. The pure
Boolean path uses the generic first-terminal scheduler with QF_UF as a fallback.
The public `CheckSatOutcome` is derived after dispatch solely from the returned
goal's truth, typed reason, model, and diagnostic; there is no parallel mutable
result channel. A typed inconclusive reason takes precedence over terminal
formula truth at this operational conversion boundary, while `truth()` itself
remains formula-only. This adaptation is isolated in `solver_tactic`, while expected
unsupported-fragment exceptions are defined in `solver_error`; other exceptions
remain serious failures and escape the tactic boundary.
The existing QF_UF preprocessing portfolio remains outside this layer because
each arm deliberately reparses the original SMT-LIB text into a fresh term
manager; `TermId` values and goals must never cross that session boundary.

The scheduler API is independent of its current execution policy. The supplied
implementation runs alternatives sequentially and in declaration order. A
terminal formula stops it, as does a typed `Cancelled` result. `Inapplicable`,
`Unsupported`, `ResourceLimit`, and ordinary `Unknown` results continue to the
next arm from the pristine original goal. Exceptions escape without attempting
another arm. A
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
- Assertion, model, and reconstruction storage is shared independently; changing
  one payload does not copy either of the other large payloads.
- `AnyOf` always contains at least one goal.
- Every assertion is a Bool-sorted `TermId` owned by the context term manager.
- A leaf tactic is not applied to an already true or false goal.
- Sequential fallback gives every arm the same original `AnyOf`, not the
  previous arm's inconclusive output.
- Generic fixed-point detection first compares shared storage and falls back to
  ordered goal contents. A composition may supply a narrower equivalence when
  its progress measure is explicit; preprocessing compares ordered assertion
  vectors and preserves the latest goal attributes. Unchanged iterations are
  constant time when a leaf returns its input; changed iterations may compare
  the produced assertions linearly. The explicit iteration bound prevents
  nontermination.
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
alternatives, the non-empty invariant, fixed-point repetition (including
attribute-preserving equivalence and terminal change observation), immutable
payload sharing, cancellation precedence, conditional selection, statistics,
pristine-input fallback, and production dispatch to each supported solver
family. Existing preprocessing and solver suites cover the migrated control
flow with their prior correctness and adaptive-pass assertions.
