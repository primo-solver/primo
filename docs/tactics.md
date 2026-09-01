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

The preprocessing API exposes typed immutable builders for
`normalize-boolean`, `propagate-values`, `solve-eqs`, `derive-equalities`,
`finite-domain-preprocess`, `normalize-arithmetic`, `eliminate-monotone`, and
`purify-mixed`. Each builder owns a meaningful transformation boundary rather
than exposing small implementation helpers as portfolio actions. Gaussian
candidate generation therefore remains a `solve-eqs` setting, and the
finite-domain builder keeps equality evaluation, congruence generation,
symmetry constraints, raw/post-round at-most-one clauses, and equality
definitions in one composite. Its immutable input snapshot records the
pre-round assertion state needed by delayed symmetry and clause emission; this
preserves the established earlier/later formula dependency.

`PreprocessingProfile` is the immutable output of profile analysis over the
initially normalized goal. It selects the off, basic, finite-domain-basic,
compact-clause-basic, single-assertion, strong, large-input-bypass, or QF_LIA
path and retains the finite-domain evidence needed by delayed emission. The
analysis does not update statistics or change the goal; it may intern the same
candidate clauses that a selected finite-domain tactic will later emit.
Observability is recorded only after the profile value has been selected.

`make_preprocessing_strategy` maps that profile and immutable option values to
an inspectable `Strategy`. The prepared full structure includes initial
normalization, the selected profile, QF_LIA or large-input equation handling
when applicable, an adaptive `Repeat`, finite-domain processing, arithmetic
normalization, optional monotone elimination, mandatory purification, and final
Boolean normalization in the established order. Production executes the
initial-normalization prefix before profile analysis and the remaining suffix
afterward because profile selection must inspect the transformed goal.

The repeated preprocessing round is implemented in `preprocessing_tactics` as
an exact composition of those public builders over each input alternative. Its
immutable `PreprocessingRoundOptions` is constructed once from the selected
strategy; the leaves do not inspect command-line state. The generic `repeat`
tactical applies it to an ordered-assertion fixed point, respects the existing
baseline pass limit, and consults the read-only reachable-term-count probe for
configured adaptive extra passes. Its observer reports every completed
iteration, including the fixed-point iteration, so the historical
`:preprocessing.passes` counter no longer needs a formula-valued no-op tactic.
The round deliberately finishes all selected leaves before exposing a terminal
goal, preserving the established pass ordering and reconstruction/statistics
behavior. Each output carries its own immutable reconstruction value; outputs
that do not change it may share the same backing storage. The transformation
implementations are unchanged. Existing command-line options construct this
built-in strategy; there is no user-facing tactic expression syntax and
option defaults are unchanged.

The `solver_engines` module contains and exposes the complete single-goal QF_UF,
QF_LRA, and combined QF_UFLRA engines plus the QF_LIA wrapper over its arithmetic
engine. `solver_leaves` owns their per-session
formula-valued adapters, model-term selection, and construction of the typed
`solver-dispatch` strategy. The session supplies frontend-owned resources and
compiles that returned strategy; it no longer contains engine bodies or
assembles the individual leaves.
A solving leaf represents `sat` by an empty
goal, `unsat` by a goal containing `false`, and `unknown` or unsupported input
by the unchanged inconclusive goal plus a typed reason and diagnostic. The
internal `SolverAnswer` carries the result and reason; diagnostic text is never
examined to select fallback behavior. A solver leaf processes disjunctive goals
in order, stops at the first satisfiable alternative and retains that model,
collapses an all-unsatisfiable set to one false goal, and otherwise returns the
remaining inconclusive alternatives. A dispatch tactic probes
the formula features and logic, then invokes the selected leaf. The pure
Boolean path uses the generic first-terminal scheduler with QF_UF as a fallback.
When model production is requested for QF_UFLRA, a goal that still contains EUF
structure and has declared Real constants stays on the mixed leaf even if an
earlier transformation removed its last arithmetic atom. This preserves the
mixed model-conversion chain: routing that residual goal to the QF_UF leaf would
print an uninterpreted placeholder for a Real constant eliminated by monotone
preprocessing instead of reconstructing its numeric witness.
The public `CheckSatOutcome` is derived after dispatch solely from the returned
goal's truth, typed reason, model, and diagnostic; its typed reason also crosses
the session-portfolio boundary. There is no parallel mutable result channel. A
typed inconclusive reason takes precedence over terminal
formula truth at this operational conversion boundary, while `truth()` itself
remains formula-only. This adaptation is isolated in `solver_tactic`, while expected
unsupported-fragment exceptions are defined in `solver_error`; other exceptions
remain serious failures and escape the tactic boundary.
The QF_UF preprocessing portfolio remains outside formula tactics because each
arm deliberately reparses the original SMT-LIB text into a fresh term manager;
`TermId` values and goals must never cross that session boundary.
`SessionExecutor` is the isolated execution interface and
`SequentialSessionExecutor` is its only implementation. The first
basic-preprocessing/polarity arm falls back only on a typed `ResourceLimit`
from its theory-conflict budget. Cancellation and ordinary unknown results stop
the session portfolio, exceptions escape, and diagnostic messages are never
examined for control flow. A future parallel executor must give every arm a
separate frontend, SAT backend, statistics object, and cancellation scope.

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

## Experimental exhaustive Boolean splitting

`make_experimental_split_bools` is the first production-reachable tactic that
generates more than one formula goal. It is default-off and is enabled only by
`--experimental-split-bools=k`. Its purpose is to exercise and measure the
multi-goal boundary before adding a parallel executor; it is not presented as
a solver optimization.

For each current inconclusive goal, the tactic performs one iterative
depth-first traversal of the reachable term DAG. It first selects Bool-sorted
constants and uninterpreted applications in encounter order, then equality,
`<=`, and `<` atoms in encounter order, stopping after `k` distinct terms.
Logical connectives and Boolean values are not candidates. This priority makes
the experiment split propositional symbols when they exist while still making
pure arithmetic inputs eligible. Selection therefore observes all preceding
transformations. A visited vector indexed by `TermId` makes discovery linear in
the reachable DAG rather than exponential in shared syntax.

If `b_0, ..., b_(m-1)` are selected, the tactic returns the false-first
lexicographic sequence

```text
(G and not b_0 and ... and not b_(m-1)), ...,
(G and b_0 and ... and b_(m-1)).
```

Equivalently, the disjunction of these `2^m` conjunctions is `G`, so the tactic
needs no model-conversion step: a model of a selected branch is already a model
of the input. This is the ordinary exhaustive case split instantiated in the
disjunctive-goal semantics from de Moura and Passmore, Section 3, cited above;
there is no paper-specific search heuristic in the implementation. Every
branch retains the input goal's model-reconstruction chain and attributes.

The initial executor is sequential. `solver-dispatch` walks alternatives in
their stored order, invokes the appropriate solver leaf for one goal at a time,
stops at the first satisfiable branch and keeps that branch's model, collapses
an all-unsatisfiable product to one false goal, and otherwise retains the
inconclusive branches. No threads or shared mutable result channel are added.
All branches share the session's term manager and statistics object.

The eager representation costs `O(2^m (n + m))` `TermId` storage for `n` input
assertions. Both the typed builder and CLI reject `k > 16`; this is an
engineering guard against accidental allocation, not a logical restriction.
If only `m < k` candidates exist, the tactic emits `2^m` goals. A later parallel
executor may consume the same immutable `AnyOf`, but must define cancellation,
winner selection, and deterministic statistics before it replaces this
sequential policy.

### Sequential evaluation

The initial local evaluation used existing SMT-LIB division benchmarks that
declare Boolean-valued symbols. The available corpora contain 910 such QF_UF
files and 900 such QF_UFLRA files. A stratified 17-case QF_UF and 10-case
QF_UFLRA screen selected the focused manifests
`benchmarks/experimental-split-bools-qfuf.txt` and
`benchmarks/experimental-split-bools-qfuflra.txt`. Each focused case was then
run five times on one pinned CPU, alternating the order of `k = 0, 1, 2, 3`,
with a 10-second per-run limit. Times below are medians; decisions and conflicts
are deterministic totals from the corresponding run.

| benchmark | answer | `k=0` | `k=1` | `k=2` | `k=3` | search observation |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Goel firewire-tree.3 | sat | 0.274 s | 0.270 s | 0.309 s | 0.344 s | first satisfiable branch stops after 1, 1, 2, and 3 solver calls; decisions stay 13,774 |
| Goel hanoi.2 | unsat | 0.285 s | 0.339 s | 0.453 s | 0.671 s | at `k>=2`, all branches need zero SAT decisions/conflicts, versus 11,148/28 at `k=0` |
| Goel sokoban.3 | unsat | 1.503 s | 1.595 s | 1.818 s | 2.207 s | at `k>=2`, all branches need zero SAT decisions/conflicts, versus 19,819/2 at `k=0` |
| NEQ016 size 7 | unsat | 2.996 s | 2.976 s | 3.005 s | 2.983 s | one hard branch dominates; total decisions/conflicts stay 72,900/10,312 while calls grow 1, 2, 4, 8 |
| SEQ020 size 4 | sat | 0.081 s | 0.084 s | 0.094 s | 0.139 s | `k=3` increases decisions/conflicts from 9,448/256 to 14,060/581 |
| MathSAT coupled 20/500 | unsat | 0.022 s | 0.027 s | 0.041 s | 0.065 s | conflicts fall from 168 to 39 at `k=3`, but eight sequential calls dominate |
| MathSAT coupled 40/1200 | unsat | 0.077 s | 0.098 s | 0.127 s | 0.156 s | conflicts fall from 1,198 to 162 at `k=3`, but wall time doubles |
| MathSAT decoupled 40/80 | sat | 0.031 s | 0.037 s | 0.043 s | 0.045 s | the first satisfiable branch occurs after 1, 1, 2, and 3 calls; `k=1` initially worsens search |

Across the five QF_UF focus cases, the sum of per-case medians changes by
`+2.4%`, `+10.5%`, and `+23.4%` for `k=1`, `k=2`, and `k=3`. Across the three
QF_UFLRA cases it changes by `+24.0%`, `+61.7%`, and `+105.0%`. The split leaf
itself takes at most about 2.1 ms on these cases; the regressions come from
repeated sequential solver setup and branch execution, not from strategy or
goal-materialization overhead.

The search behavior is nevertheless useful for the experiment. Two hardware
instances cross a sharp threshold where two Boolean assignments eliminate all
SAT search, and mixed arithmetic instances discard most conflicts. Sequential
execution cannot exploit that reduction because unsatisfiable inputs must run
all `2^k` branches. This is evidence that branch-level instrumentation and a
truly isolated parallel executor are worth testing, but it is not evidence for
enabling splitting as an optimization. The option therefore remains
experimental and default-off. A `k=3` model replay validated all three
satisfiable focused cases with Dolmen; the other five were unsatisfiable, and
no answer disagreed with benchmark status.

## Composition verification

Primo tests tactic composition only through legal production profiles. There
is no test-only strategy injector or user-facing tactic parser, and the matrix
does not reorder leaves whose dependencies are encoded by the production
preprocessing strategy. `benchmarks/tactic-composition-matrix.json` is the
single manifest for the named profiles and their applicable logics: defaults,
off/basic/strong/auto preprocessing, direct and guarded ground substitution,
Gaussian elimination disabled, monotone elimination, and Boolean splitting at
`k=1` or `k=2`.

`primo_tactic_composition_tests` pins each profile's canonical structure and
fingerprint, checks QF_LIA's substitution-only restriction, and executes real
preprocessing and solver leaves over SAT and UNSAT goals. The public CLI layer
is `scripts/tactic_composition_matrix.py`. It runs every applicable
profile/case pair twice for the deterministic local corpus, compares answers
with Z3 and cvc5, compares all non-timing statistics between repetitions, and
sends every SAT model through Dolmen with fallback disabled. Counters for
entry leaves that must run on every case are checked per execution. Counters
for later leaves are required at least once per applicable logic across the
corpus: a sequence correctly short-circuits when an earlier transformation
proves the goal unsatisfiable, so absence on that individual case is not a
composition failure. Its summary and per-run stdout, stderr, statistics,
model-checking output, solver version, tested binary commit, and workspace
commit are durable artifacts. The binary version is authoritative because an
archive-mode SLURM workspace intentionally preserves its dependency checkout
and may therefore have a stale `.git/HEAD`; the preflight independently
requires the binary's embedded revision to match the submitted local commit.
Run the complete local layer with:

```bash
make tactic-composition-test
```

`scripts/slurm_tactic_composition_matrix.sbatch` is the merge gate. Its sixteen
array tasks generate four 250-case shards for each supported logic, then run
every profile applicable to each of the resulting 1,000 unique formulas.
Timeout and ordinary unknown results are counted as inconclusive; wrong
answers, reference disagreements, crashes, malformed statistics, missing
per-case entry counters, missing corpus-level activity for later leaves, and
any model that does not pass Dolmen directly fail the gate. Each QF_UF shard
also runs the tracked residual-domain seed and requires
`:preprocessing.adaptive-passes` to become nonzero, so constructing an adaptive
strategy without exercising its continuation policy is not counted as
coverage. The generated cases and row-level summaries are retained per task.
This matrix is a correctness and composition-coverage gate, not a performance
claim; timing statistics are recorded but deliberately excluded from the
repeatability comparison.

## Deliberate deviations and limitations

The paper's tactic return type separately represents `sat`, `unsat`, `unknown`,
and `fail`. Primo instead derives true, false, or inconclusive from `AnyOf` and
uses an unchanged goal for ordinary inapplicability. `InconclusiveReason`
distinguishes cancellation, resource, unsupported, applicability, and ordinary
unknown reasons without changing the logical value. Broken
contracts and backend failures use exceptions (`TacticError` or the originating
standard exception); they are not tactical alternatives.

The default production schedule still operates on exactly one goal. The
default-off experimental Boolean splitter can introduce bounded disjunctive
alternatives, which are currently executed sequentially. Goal-local
reconstruction prevents those alternatives from sharing mutable bookkeeping,
but a parallel scheduler still needs a defined statistics and cancellation
policy before it can be enabled.
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
