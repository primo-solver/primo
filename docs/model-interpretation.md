# Finalized model interpretations

## Status and acceptance contract

The lexical repair in [](lexical-conformance.md) changes name representation,
not interpretation completion: the printer encodes canonical names using the
shared lexical policy and checks parameter freshness by canonical identity.
`Symbol::internal` explicitly marks generated auxiliaries; model completeness
and output filtering must never use a user's name prefix as ownership evidence.
Names containing the selected backslash/control-character extensions round-trip
in Primo but are not promised portable to strict external parsers.

This is Primo's original design and implementation contract for the general
model redesign, implemented through `8ff9991`. The
legacy QF_UFLIA builder completed by EUF representative, while its printer
deduplicated rows by evaluated arguments and could replace their results after
constants had been printed. The fixture
`benchmarks/regressions/qf_uflia_model_evaluated_tuple.smt2` exposes the mismatch.

Delivery followed: baseline evidence; typed core model/evaluator and SMT-LIB
printer; QF_UFLIA; Boolean/QF_UF; Int/Real and mixed-Real paths; removal of legacy
builders; cross-logic external validation.

The typed foundation (`core::Model`, `core::ModelEvaluator`, and
`smtlib::print_model`) is implemented and independently unit-tested. The current
QF_UFLIA collection driver validates owned `ComponentModelFragment` exports and
calls `assemble_component_model` in `smt/component_model_assembler.cpp`; its
private `FragmentModelBuilder` preserves the conversion-replay semantics below.
Boolean, QF_UF, QF_LIA, QF_LRA, QF_UFLRA and preprocessing-only paths retain
the private `ComponentModelBuilder` in `typed_model_builder.cpp`, entered through
`smt::build_typed_model`. Both adapters produce
the same immutable core representation and share its evaluator and printer.
Legacy string builders and their separate arithmetic evaluators remain removed.
See [](n-way-combination-migration.md) for the new fragment contract and its
qualification; the following evidence closes the earlier general-model redesign,
not the subsequent N-component migration.

That earlier shared implementation completed its cross-logic correctness gates:
31,000 generated case/profile runs at `6f89774`, followed by 7,000 mixed-logic
generated runs and 4,140 retained seed mutations at `3a2dd40`. These verification
commits have identical solver sources to `8ff9991`. All produced SAT models
submitted for acceptance passed independent Dolmen validation; no wrong answer,
invalid model or crash was found in the accepted gates. Resource limits are
reported separately, not counted as passes: 80 timeouts and two unknowns in the
main matrix, 108 timeouts in saved-Real semantic validation, and five mutation
timeouts. The Real replay gate additionally covers explicit monotone elimination.
The mutation campaign checks all four preprocessing modes and the QF_UFLIA
combination-off path; the generated matrix also checks mixed-Real reconciliation
off. Strict input-grammar diagnostics and rejected unrestricted mutations are
not accepted evidence. See the cross-logic and final-mutation entries in
`docs/run-log.json` for commands, seeds, per-family counts and artifacts.
The closing include-only revision `315b792` also passes a fresh 7,000-case
mixed-logic SLURM rerun, 3,487 independent SAT-model checks and all 72
witness/profile checks, with no errors or resource limits in that rerun.
Implementation, external correctness validation and review are complete;
the separately documented baseline advisories are not part of this bug fix.
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

The `primo_smtlib` library links `primo_terms` explicitly: its model printer
reads core interpretations and therefore needs the core exact-arithmetic
headers and libraries even on hosts without system-wide GMP development files.

`smt/` alone owns postsolve reconstruction and component adapters. `smtlib/`
serializes the frozen interpretation, quotes identifiers, and chooses parameter
names without capture. Public `CheckSatOutcome::model` remains serialized text;
internal solver results carry the typed model to the session boundary.

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

### Component adapter and replay checklist

The collection adapter first validates the exact accepted model token, shared
scope coverage and partition agreement, authoritative values and symbol
ownership. It identifies component-local domain labels through shared bindings
before constructing evaluated function tuples. These additional obligations
belong to `validate_component_fragment` and `assemble_component_model`, not to
the frozen legacy builder. The base-construction and replay principles below
are shared by both adapters.

The staged adapter builds its base interpretation only from the final active
assertions, accepted component facts, numeric component values and purification
aliases. It saturates their EUF classes, attaches exact numeric/Boolean anchors,
and uses distinct values for otherwise unanchored classes. Function rows are
inserted by the shared evaluator's evaluated argument tuple; conflicting rows
are errors, not printer preferences. Historical applications appearing only in
an eliminated definition are evaluated against that table, never added to it.

Solved-constant steps are simultaneous updates evaluated in the interpretation
of the following stage. Replay runs backwards, with a fresh evaluator memo at
each changed stage. This retains intermediate versions without flattening a
definition map. Ground-unit rewriting and Boolean-unit propagation retain their
facts in the reduced assertions; their logs are consistency checks at the
corresponding replay stage, not constant assignments. Purification aliases are
base-model constraints and require no change to public interpretations.

Monotone Real elimination reuses the one-sided reconstruction rule described in
[](papers/monotone-variable-elimination.md): evaluate the remaining affine terms
and choose a value one unit beyond the tightest bound. Each restored variable
starts a fresh evaluation epoch. Shared discharged constraints are checked after
the batch, since a later variable can repair a constraint shared with an earlier
one. This is the existing baseline rule, not a new elimination algorithm.

The shared explicit-stack evaluator accepts a symbol resolver for construction;
its interpreted operators and lazy ITE scheduling are identical to frozen-model
evaluation. A resolver must remain stable during one evaluation epoch. Base
construction may insert a previously absent row but must never revise a returned
value. Replay changes only eliminated constants, between epochs. Cost is linear
in the evaluated DAG per epoch, plus congruence closure and exact arithmetic;
overlapping definitions in separate epochs can be revisited. This is an original
baseline, not an incremental dependency-invalidation optimization.

The session retains original typed assertion roots before preprocessing.
Debug builds and explicit production `--check-model` check those assertions
against the frozen interpretation. Structural consistency is always checked.
The flag does not request a model by itself: use SMT-LIB
`:produce-models true` and `get-model` as usual. All solver model routes use the
same evaluator. Resource limits in solving or model-separation searches can
still return unknown; contradictory component values, malformed interpretations
and failed reconstruction checks are internal errors.
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
promotion is implied by this redesign. `:time.model.reconstruction-us` measures
the shared adapter/replay builder; `:time.model.check-us` and
`:time.model.serialization-us` measure checking and printing separately. Mixed
component preparation retains its existing theory/model-separation counters.

For a host without LLVM review tools, `make contrib-clang-tools` installs pinned
clang-tidy/clang-format 18.1.8 in `contrib/local/clang-tools-env`. The remote
preparation helper can provision this optional target with
`--contrib-target contrib-clang-tools --reuse-dependencies`. The static-review
batch script checks every non-generated C++ source in the prepared tracked
snapshot and records its manifest, rather than relying on remote Git metadata.
The optional installer also snapshots the build compiler's C and C++ header
search roots for compute nodes without development headers. Review adds those
roots in compiler order to the existing compile database; it does not rebuild
the solver with the compute node's potentially different compiler. The first
static-review attempt exposed that environmental mismatch before any analysis
ran. Local CTest/ASan and independent SLURM solver gates remain separate checks.

The final whole-tree scan (99 non-generated C++ files, SLURM `134747`) completed.
Review removed four unused non-paired headers and added the direct model-error
header. One analyzer leak warning was adjudicated as retained `shared_ptr`
ownership in `Goal`, not a discarded allocation. Paired-header checks and
180 unrelated missing-direct-include suggestions remain advisory baseline debt;
the scan is not described as warning-free. Details and raw logs are indexed in
`docs/run-log.json`.

The mutation gate accepts `PRIMO_MUTATION_TOOL=opfuzz` with Primo's
`scripts/fuzz_configs/model_linear_ops.txt`. This profile changes Boolean and
order operators and affine arithmetic without introducing new terms, binders,
variable divisors, or nonlinear products. Unrestricted typefuzz remains useful
for boundary testing, but its out-of-fragment and ill-scoped mutants must be
classified separately; they are not model-validation passes. Both profiles
check every original seed first and send identical solver options to answer
comparison and independent printed-model validation.
For the supported fork, `scripts/seeded_opfuzz.py` seeds Python's random generator
before executing the installed OpFuzz launcher, since only TypeFuzz exposes a
native `--seed` flag. Every retained mutant also receives a fresh answer check
through `check_solver_seed_corpus.py`: YinYang's handling of generic nonzero
solver exits is not sufficient as the gate's acceptance oracle.

FuzzSMT's Real generator can emit products of constant expressions that Dolmen
rejects under its strict linear grammar. The semantic revalidation gate retains
the exact assertions and models but uses Dolmen's
`--force-smtlib2-logic=QF_NRA`, a semantic superset, to check their truth. This
does not establish strict QF_LRA syntax conformance. The separate
`slurm_typed_lra_reconstruction_gate.sbatch` uses the conforming Boolean/affine
generator across all preprocessing modes and explicitly tests monotone replay.
