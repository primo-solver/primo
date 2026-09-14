# Assumption-based incremental QF_LRA design

Status: implemented behind an opt-in option, with the bounded correctness gates
reported below. The default remains the journal-rebuild session.

## Algorithmic basis and deliberate limits

Cimatti, Griggio, Schaafsma and Sebastiani, *The MathSAT5 SMT Solver*,
TACAS 2013, [§§4.2–4.3](https://es-static.fbk.eu/people/griggio/papers/tacas13.pdf),
motivates scope labels and protection against incremental preprocessing hazards.
Primo uses labels as assumptions, but retains unconditional full Boolean
definitions and guard only roots. Pop permanently negates the retired label;
storage is retained until reset. This differs from MathSAT's deletion policy.
Neither contemporary-clause subsumption nor incremental variable elimination
is part of this implementation.

Bjørner and Fazekas, *Incremental Pre-processing for SMT*, CADE 2023,
[§3.2 and §5](https://kfazekas.github.io/papers/BjornerFazekas-CADE23.pdf),
distinguishes replayable substitutions from transformations needing restoration.
Primo v1 defers both mechanisms and permits only context-independent rewriting
and conservative lowering. Existing backward model conversion alone is not an
incremental preprocessing contract.

The existing [IPASIR-UP note](papers/ipasir-up.md) and backend header contracts
govern callback ownership. Backend reconnection must be tested before engine
integration; a fresh theory array alone does not establish assignment replay.

## Ownership, operations, and invariants

- `SmtSession` owns the original command journal, active assertions and scope
  boundaries. It owns a persistent SMT engine borrowing `Frontend::terms()`;
  the engine must be destroyed before the frontend.
- `SymbolTable` retains identities and storage while scope pop removes only
  name bindings. A redeclaration gets a fresh ID. Frontend definition bindings,
  declared numeric symbols and side-constraint emission state follow scopes.
  Parameterized definitions retain immediate validation and declaration-context
  semantics; caller-local bindings cannot capture their free names.
- The SMT engine owns `CadicalBackend`, `BooleanClausifier` and
  `PreparedLraTheory`. One selector is allocated lazily per nonempty pushed
  scope. Base assertions are permanent; scoped roots use `¬selector ∨ root`.
  All active selectors are assumed for each query; retired selectors are never
  reused. Full Tseitin equivalences are conservative over their fresh variables.
- Frontend and lowering side constraints are guarded with their assertion.
  Deduplication must not suppress a constraint whose previous emission was
  popped. Fresh lowerings must not share an unconstrained auxiliary by accident.
- Arithmetic preparation appends only new atoms to the unasserted base tableau.
  Every query constructs a fresh `OnlineLraPropagator` working tableau. Theory
  explanations must be globally valid over permanent atom meanings; conditional
  assertion consequences may persist only with their selector dependencies.
- The backend finishes the old callback/reason lifecycle while its owner is
  alive, disconnects, connects the new owner and reobserves all relevant IDs.
  Tests must establish delivery of permanent root assignments and recovery after
  SAT, assumption-UNSAT and cancellation. No callback may outlive its owner.
- Model snapshots own signature metadata and visible-symbol IDs. Completion and
  printing use only the active signature. Historical IDs remain meaningful in
  old snapshots after further mutation and reset.
- Tactics default to unclassified for incremental safety. Auto selects audited
  rewriting/lowering; Off uses mandatory lowering. Basic/Strong, portfolios,
  splitting and fixed-variable elimination are rejected. SAT preprocessing and
  inprocessing are disabled in this opt-in mode; proxy/selector IDs are protected.

## Complexity and observable reuse

New translation and preparation should depend on new syntax/atoms, apart from
existing definition-validation costs. Selector collection costs active depth;
retirement costs popped scopes. Working theory initialization and reconnection
cost retained tableau/atom size per query. Terms, atoms, clauses and retired
scopes may accumulate without bound until reset; automatic compaction is deferred.
Counters must expose frontend/engine creations, translated assertions, queries,
active/retired selectors, retained terms/atoms/clauses, theory resets, and phase
times. A correct answer alone is not evidence of reuse.

## Proof obligations and acceptance tests

1. The permanent clause set under active selectors is equisatisfiable with the
   journal's active assertions. Test nested/empty scopes, multi-pop, conflicts,
   shared Boolean encodings and reassertion of cached side constraints.
2. Popped names are invisible and redeclarations have independent identities.
   Test definition scope, immediate type errors and retained model snapshots.
3. No eliminated-variable assumption crosses queries. Test one-sided bounds
   followed by contradictory bounds, including after a scoped UNSAT query.
4. Verify the callback reconnection contract separately, including learned
   reasons, permanent assignments, new variables, unknown and cancellation.
5. Run check, ASan, lint, docs, whole-tree code review, tidy and advisory include
   cleaner. Record and resolve failures before integration is declared complete.
6. On the exact candidate/options, run SLURM gates of 1,000 generated scoped
   QF_LRA sessions and 1,000 native-seed mutations, with reference answers and
   separate independent SAT-model validation. Replay bounded native prefixes
   through stdin and validate their actual models with Dolmen.
7. Compare rebuild and assumptions on the two established native sessions at
   600 seconds; reuse recorded references. Report full completion, partial query
   progress, PAR2, memory and reuse separately. Preserve the existing 25-file
   default-mode sample. Timed-out/incompletely validated runs are unqualified.

Commit implementation, tests, synchronized docs and reproducibility metadata.
Record substantial experiments in `run-log.json`, collect artifacts, and audit
jobs/processes and the worktree before handoff.

## Implementation mapping and verification status

The checklist above now maps to `IncrementalLraEngine` (implemented alongside
existing arithmetic machinery in `solver_engines.cpp`) through the narrow
`IncrementalEngine` interface. Its scope vector owns selectors; its clausifier,
backend and `PreparedLraTheory` persist. Query-local propagator and terminator
connections disconnect before their owners die. A query-local conflict counter
feeds the requested resource limit without changing the ordinary engine's
counter policy. SAT active redundant/irredundant clause counts are separate from
cumulative submitted clauses; active counts exclude clauses satisfied at root
and do not measure allocated bytes of inactive backend storage.

`Frontend(true)` validates parameterized definitions using fresh internal
parameter terms in the current declaration environment, avoiding the rebuild
mode's isolated declaration-prefix replay. Definition templates still expand on
application. Validation terms are retained but invisible. Scope pop rolls back
emission and definition trails and removes symbol name bindings; it never
invalidates storage. Symbols created only in retired scopes do not get completed
unless required to interpret retained atoms. Active symbols are completed and
printed using explicit snapshot visibility.

Initial local tests cover Auto/Off, shared Boolean definitions, empty/nested
scopes, multi-pop, underflow, definition errors, numeric-to-Boolean redeclaration,
64 successive allocation-growing scopes, immutable snapshots, cancellation and
theory-conflict-budget recovery. Backend tests separately cover permanent root
assignment replay, fresh observations, assumptions and external unit reasons.
`scripts/incremental_lra_gate.py` generates affine Boolean/arithmetic sessions or
mutates literals/strictness in the first query of supplied native sessions. Its
streaming validator checks each actual SAT model independently with Dolmen and
requires successful candidate/reference exits. Native mutation preserves names
and command scopes and removes invalidated source status annotations. These are
bounded seed mutations, not full native-session qualification.

## Collected correctness evidence

Candidate `04d2e23` passed the exact Auto-profile SLURM gate: 1,000 generated
sessions and 1,000 native-seed mutations, 16,000 matching query answers and
10,802 actual SAT models independently accepted by Dolmen. Both unmodified
first-query native seeds passed before each mutation shard. There were no wrong
answers, crashes, parser/type errors, timeouts, unsupported results or reference
disagreements. The Off pipeline has unit tests and preliminary local smoke
coverage; this campaign specifically qualifies Auto.

All ten native first-four-query prefixes passed through real stdin: 40 queries
matched the hash-checked status oracles and ten actual SAT models passed Dolmen.
These checks are separate from timing and qualify only the replayed prefixes.
The unchanged default mode passed all 25 selected real files, 50 streamed
sessions, 200 queries and 121 actual SAT model checks. The QF_UF shard used the
established 10,000-conflict budget after retaining an initial incomplete attempt
with the already-known insufficient 1,000-conflict cap. Other logics used 1,000.

Local verification passed `make check`, `make asan-test`, `make lint` and
`make docs`. Stable per-translation-unit clang-tidy/analyzer and advisory
include-cleaner scans covered all 110 tracked non-generated C++ sources.
Earlier scans that overlapped source edits were discarded. The gate's failure
counter subsequently received an initialization fix and a regression proving
that a failed process invalidates provisional models, is recorded, and stops
the campaign. Generation, validation decisions and passing evidence are unchanged.

Timing counters have nested accounting: `:time.frontend-us` includes command
translation, incremental preprocessing and append/preparation work. The
preprocessing and `:time.session.append-us` counters expose those subphases;
they must not be added again to frontend time. Theory-reset and solve times
belong to the check itself. Retained clause counts describe active backend
clauses, while peak memory is measured separately by SLURM.

## Native-session comparison and decision

The two established `hybrid_networks/bmwlin` inputs ran through unchanged stdin
on SLURM with a 600-second whole-session limit, `--max-theory-conflicts=10000`
and `--stats`. Each mode used the same preserved candidate executable. Model
validation was a separate stage. The scheduler reserved two logical CPUs per
array element despite one requested task/CPU; each driver ran one solver.

| Session | Mode | Completed queries | Whole-session result | Sampled peak MiB |
| --- | --- | ---: | --- | ---: |
| BMC | rebuild | 38/101 | timeout at 600 s | 589 |
| BMC | assumptions | 101/101 | complete in 126.97 s | 1,056 |
| Induction | rebuild | 53/202 | timeout at 600 s | 448 |
| Induction | assumptions | 95/202 | timeout at 600 s | 930 |

All observed definitive answers matched the recorded source statuses. The
completed BMC session contains only UNSAT answers, so it requires no SAT model
checks. The induction runs are incomplete: their answers beyond the separately
validated prefix are diagnostic observations, and their SAT models are not fully
qualified. No full-induction or full-archive correctness/performance qualification
is claimed. Peak figures are SLURM batch-step MaxRSS samples, not isolated solver
allocations; the modes reached different query counts.

On this two-session selection, assumptions completes one session and rebuild
completes none. Mean session PAR2 is 663.49 s versus 1,200 s, assigning 1,200 s
to each timeout. Recorded Z3/cvc5 runs are reused: neither completed a session
at 600 s; their BMC/induction progress was 58/73 and 69/77 queries respectively.
Partial query counts are not speedup ratios and these historical references
do not establish full-session correctness.

The completed BMC run reports one frontend, one engine, 202 newly translated
assertions and 101 working-theory resets. At its last query it retains 100
retired selectors, 721,255 terms, 33,591 prepared atoms and 1,221,030 active
clauses, including 44,773 redundant clauses. These counters establish reuse
and expose the retained-storage cost; they are not memory estimates.

Accept the implementation as opt-in. Keep rebuild as the default and defer
compaction and QF_UFLRA assumption support. Exact commands, hashes, per-shard
seeds, jobs, resource limits, phase counters and artifact paths are recorded in
`benchmarks/assumptions-lra-summary.json` and run-log entry
`2026-09-11-assumptions-lra-qualification`. The initial development entry remains
preliminary historical evidence; the collected exact-candidate runs supersede it.

## Additional OpenSMT reference

[](opensmt-incremental-comparison.md) adds OpenSMT 2.9.2 using the same native
inputs and session limits. The Primo and existing reference timings here remain
unchanged; the new report separates completed sessions, errors, partial query
progress and model-qualification coverage.
