# Finalized model interpretations

## Status and acceptance contract

This is Primo's original design for the staged general model redesign. It is
an implementation checklist, not a claim that migration is complete. The
existing QF_UFLIA builder completes by EUF representative, while its printer
deduplicates rows by evaluated arguments and can replace their results after
constants have been printed. The fixture
`benchmarks/regressions/qf_uflia_model_evaluated_tuple.smt2` exposes the mismatch.

Delivery order: baseline evidence; typed core model/evaluator and SMT-LIB
printer; QF_UFLIA; Boolean/QF_UF; Int/Real and mixed-Real paths; removal of legacy
builders; complete external validation. The overall work remains open until
all stages and the cross-logic gates pass.

The typed foundation (`core::Model`, `core::ModelEvaluator`, and
`smtlib::print_model`) is implemented and independently unit-tested. No solver
engine has migrated yet; therefore it does not yet repair the latest witness.
The baseline eight-file external check reports seven historical witnesses
passing and the evaluated-tuple fixture failing; see the model-redesign entry
in `docs/run-log.json`.

## Data and ownership

`core::ModelValue` contains a sort ID and a Boolean, exact rational, or
uninterpreted-domain index. Sort IDs distinguish Int from Real and distinct
uninterpreted universes. `core::Model` owns a snapshot of the signature and
constant/function interpretations. Each function has a total default and a
finite map from typed argument tuples to typed results. Construction rejects
ill-sorted values and contradictory duplicate rows. Its public surface is
read-only after construction; neither evaluation nor serialization completes it.

The model owns no AST, SAT objects, term references, or component caches. The
signature snapshot allows printing after the original term manager is destroyed.
An evaluator borrows a live const term manager while evaluating its IDs; IDs
are meaningful only in that manager. An evaluator's memo belongs to one frozen
model and cannot survive replacing it with a different interpretation.

`smt/` alone owns postsolve reconstruction and component adapters. `smtlib/`
serializes the frozen interpretation, quotes identifiers, and chooses parameter
names without capture. Public `CheckSatOutcome::model` remains serialized text;
internal solver results will carry the typed model to the session boundary.

## Evaluation and proof obligations

Basis: the original term and conversion contracts in [](architecture.md) and
[](tactics.md), with ordinary structural evaluation of Primo's typed core DAG.
No optimized model-construction algorithm from another solver is claimed.

- Equality compares same-sort semantic values, not printed strings.
- Arithmetic computes exactly from children; applications consult only the
  function's finalized tuple table and default.
- ITE evaluates its condition and selected branch only. Boolean connectives,
  comparisons and every currently supported arithmetic term kind are covered.
- An explicit stack and memo evaluate each demanded DAG node once. Cost is
  linear in visited nodes and edges apart from exact arithmetic and tuple
  hashing; stack/memo space is linear. No recursive descent over model terms.
- Int division/modulo use the existing normalized positive-divisor term
  contract; unsupported zero divisors are not silently assigned a value.

Reconstruction must preserve authoritative component values and accepted
equalities/disequalities, and replay typed conversions in reverse execution
order with intermediate definition versions intact. Required definitions are
dependencies, not independently completable leaves. Provisional choices must
remain separate from authoritative and derived values. Replacing a provisional
choice invalidates its derived dependants and application keys before freezing.
Application result slots are distinct from argument-key dependencies: valid
equations such as `x = f(x)` must not be mistaken for cyclic substitution logs.
The worklist must either finish or identify a stalled dependency/conflicting
authoritative assignment, never stop at an arbitrary number of rounds.

In particular, one `(SymbolId, evaluated arguments)` has one result. This is a
construction invariant, not a row-selection preference. No output is emitted
until constants and tables agree. Relevant roots come from assertions,
component assignments and conversions, plus visible declarations for defaults;
unrelated arena terms cannot dictate the interpretation.

## Checking, errors and verification

The session retains original typed assertion roots before preprocessing.
Debug builds and explicit production `--check-model` check those assertions
against the frozen interpretation. Structural consistency is always checked.
Failures are internal errors with no model and nonzero CLI exit, not unsupported
input or unknown. Model-free solving avoids this work.

Tests must cover typed equality, duplicate tuples, defaults, selected ITE
branches, exact arithmetic, deep DAGs, signature lifetime, quoting, definition
chains, purification aliases, cyclic UF equations, input-order changes, and
forced arena growth. The latest and historical witnesses must receive external
printed-model validation; internal checking does not independently verify the
evaluator or serializer. Four preprocessing modes and nondefault solver paths
are part of the profile matrix.

Each migrated logic receives a fresh merge gate. Closure requires at least
3000 generated cases per affected logic, applicable real-seed mutation gates,
independent validation of produced SAT models, no unresolved correctness
failures, and resource limits separately reported. Heavy verification runs on
the requested SLURM route. Exact commands, commits, profiles, job IDs and
artifacts belong in `docs/run-log.json`. Local checks include `make check`,
`make asan-test`, `make lint`, `make docs`, and static review. Reconstruction and
validation timing must be distinguished from solve timing. No performance
promotion is implied by this redesign.
