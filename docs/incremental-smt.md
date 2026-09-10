# Incremental SMT sessions

The subsequent [](lexical-conformance.md) audit found quoted-name identity bugs
that can change answers. Its repair preserves token categories and canonical
names in the owned command journal. The qualification below describes the
original bounded corpus only; the repair has separate regression/fuzz evidence.

## Design and implementation checklist

### Native QF_LRA archive checks

`benchmarks/native-incremental-qf-lra.json` pins the user-provided
`sandbox/QF_LRA.tar.zst` (SHA256
`41865c487e20fedaeb1945b2175df657aff6797dcda95eaf6df7403449c2b323`).
Unlike the earlier wrapped-formula sample, these ten `hybrid_networks` inputs
are genuine incremental BMC/k-induction sessions. Their source headers attribute
the NuSMV hybrid-automata benchmarks to Alberto Griggio. The extracted files total
1,594,684,331 bytes and contain 1,515 queries; original bytes/scopes are retained.

The test workflow deliberately separates three verdicts:

1. `primo_parser_corpus` (`tests/parser_corpus.cpp`) traverses every command with
   the production stream-backed `smtlib::Parser::next_command`. It owns one binary
   `ifstream`, which outlives the borrowing parser, and only counters/query-end
   offsets besides the current command AST. It checks scope-count underflow and
   overflow, counts unsupported commands, and walks through EOF. It does not
   translate or type-check terms and does not solve. This is a test driver of the
   existing parser, not another SMT-LIB grammar implementation.
2. `scripts/check_native_incremental.py` runs each untouched file through Primo
   stdin, with a 120-second whole-session budget and 10,000-theory-conflict limit.
   A timeout or nonzero exit cannot become a pass because answers were printed.
   Partial answer observations remain separate from complete session acceptance.
3. An exact prefix through the first four original queries is replayed through
   a real pipe in 4,093-byte chunks with `--check-model`; Dolmen independently
   validates every actual SAT model in that prefix against its active context.
   This bounded model sample does not qualify later SAT queries in the full file.

At commit `c3ca55f`, SLURM array `142075` fully parsed all ten files: 325,456
commands and 1,515 queries, with no parser errors or unsupported commands.
All 40 prefix queries passed, including direct Dolmen acceptance of their ten
SAT models. All ten full solving sessions hit the 120-second cap, so none is
qualified as a completed execution. Their 165 decisive answers had no observed
known-status disagreement, but are only partial observations. The strict batch
gate therefore exits nonzero for every task. Full-session solving and model
validation remain incomplete. Per-file results and exact binary identities are
in `benchmarks/native-incremental-qf-lra-summary.json`; the run ledger records
the build job, commands, verification and retained raw artifacts. No reference
solvers were rerun, and this correctness run is not a speed comparison.

The follow-up `scripts/slurm_native_lra_comparison.sbatch` is a separate timing
diagnostic: the smallest BMC/k-induction pair (manifest indices 0 and 1), with
600 seconds per untouched native session for Primo, Z3 and cvc5. The driver
`scripts/compare_native_incremental.py` reuses the recorded parser query offsets,
checks source hashes and rechecks the adjacent original status labels. It runs
all solvers on original stdin, explicitly enables incremental mode for cvc5,
retains process output and exact binary identities, and rejects post-answer
process failures. Timed-out partial responses are not solved sessions. Unknown
source labels are not answer oracles, although completed reference runs can
provide an independent query-by-query comparison. Model checking is not timed
or silently inferred from the earlier four-query prefix sample. No matching
reference timings for this native archive were present in the recorded ledger.
This two-file development diagnostic is not a full evaluation or promotion.

The parser's no-lookahead invariant makes `ifstream::tellg` immediately after a
query its exact byte end. The Python driver seeks to those offsets and requires
an adjacent `(set-info :status sat/unsat/unknown)` before each query. This is an
explicit archive-specific restriction; missing statuses fail rather than being
guessed. The archive has 476 SAT, 955 UNSAT and 84 unknown status labels. Unknown
labels provide no answer oracle; completed runs involving them are marked
`complete-with-unverified-answers`, not answer-validated passes. All selected
four-query model prefixes have known statuses. Prefix extraction preserves all
preceding source bytes and introduces
no assertion or scope changes. The existing validator controls model/response
options and terminates the prefix session cleanly. No reference solvers rerun.
The command/offset smoke test covers quoted fake query text, comments, malformed
EOF, scope underflow and unsupported commands; the Python tests check status
adjacency and process-status precedence.

`scripts/slurm_native_lra_parser.sbatch` runs the three stages with four concurrent
tasks, each with an 8 GiB allocation and ten-minute outer budget. Source preparation
uses `slurm_prepare_benchmark_workspace.py --extra-target primo_parser_corpus`:
additional CMake targets share the clean build, while revision/CaDiCaL preflight
still checks the primary `primo` executable. This is correctness testing only,
not an incremental speed claim or a parser/solver algorithm change. Results are
tracked separately under `2026-09-10-native-incremental-qf-lra` in the run log.

### Direct stream parsing checklist (2026-09-10)

The stdin refinement uses the lexical/S-expression rules in SMT-LIB 2.7
Sections 3.1–3.2, with the selected extensions in [](lexical-conformance.md).
This is an original explicit-stack parser, not a parser-generator integration.

- A persistent `Lexer` borrows either a memory view or a stream buffer. Tokens
  own their spelling/category; byte offsets are relative to the entire input.
- `Parser` owns memory input or borrows a stream and exposes `next_command()`.
  It builds one AST with an explicit stack, returning immediately when the
  outer closing parenthesis is consumed. It must not request the next token
  before returning: that would deadlock a request/response client.
- The CLI passes the owned `Command` directly to `SmtSession`; no command-text
  framing/copy/reparse remains. The session's string convenience API stays.
- Work is linear in bytes plus AST size; live storage is the current token,
  open-list stack and current command AST, in addition to the existing journal.
  Stream refills may buffer available bytes, but must not require future input
  after a complete command. Input and parser lifetimes are explicit; parsers
  cannot be copied/moved while borrowing their own memory input.
- EOF between commands is normal; EOF inside a token/list and unmatched closing
  parentheses are errors. Errors terminate the CLI; resynchronization is not
  implemented. Exit does not inspect subsequent input. This is a blocking
  command-at-a-time interface, not a nonblocking `feed()`/`poll()` API.
- Qualification covers guarded stream buffers (any read after the closing
  parenthesis fails), fragmented real pipes, memory/stream equivalence,
  deep nesting, lexical policy, scoped sessions, ASan and fresh generated input.
- Streaming parse wall time includes waiting for client input. Interactive
  parse/total statistics must not be compared to offline CPU timings.

Qualification of `e24dfcd`: `make check` passes 31 CTest/140 Python tests and
`make asan-test` passes 31/31. SLURM job `141961` passes 1,000 generated sessions
against hash-matched recorded Z3/cvc5 answers, 100 live-pipe chunking seeds
(400 query answers plus 300 malformed-EOF rejection checks), and one direct
Dolmen model regression. Array `141962` completes both static scans on all
108 tracked C++ files; only existing advisories remain. No reference solver
or speed benchmark is rerun. These results qualify the stdin integration, not
full syntax conformance. The two attribute-validation gaps discovered then are
fixed by the subsequent repair in [](lexical-conformance.md), with separate
qualification. Raw artifacts and exact provenance for the streaming revision
are recorded in `docs/run-log.json` under `2026-09-10-online-parser`.

This branch implements an original rebuild-based session baseline, not persistent
incremental SAT, theory solving, or incremental tactics. The checklist describes
the implemented interface; bounded SLURM qualification and its measured costs
are recorded below, separately from future incremental-state reuse.

### Sources and implementation status

- Barrett, Fontaine and Tinelli, [SMT-LIB 2.7, March 2026 release](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-03-27.pdf),
  Sections 4.1.1–4.1.7 and 4.2.1–4.2.3: command modes, responses, scoped
  declarations/definitions, options, and stack operations specify the protocol.
- de Moura and Passmore, [The Strategy Challenge in SMT Solving](https://www.cl.cam.ac.uk/~gp351/moura-passmore-smt-strategy.pdf),
  Section 3: ordered goals, disjunctive subgoals and backward model converters
  underpin existing Primo tactics. This is not an incremental-context algorithm.
- Bjørner et al., [Z3 Internals (draft)](https://z3prover.github.io/papers/z3internals.html),
  Sections 8.2–8.3 and 8.6: incremental reuse may require translating incoming
  formulas or restoring eliminated constraints. Such reuse is **deferred** here;
  the draft is not evidence of current Z3 default behavior.

### Representation and ownership

`smt::SmtSession` owns an ordered journal of parsed context commands and stack
depths, protocol options, and the latest query outcome. Empty scopes are
compressed into a depth counter rather than allocating one frame per push.
Syntax remains at
the SMT/frontend boundary; SAT and theory code receive only core terms.
The journal is authoritative. An optional `Frontend` is a translation cache,
never a persistent preprocessed goal. At each check the session transfers that
cache and its assertion IDs into a single-query solver. Subsequent commands
reconstruct a frontend by replaying surviving commands in their original order.
Portfolio fallback arms likewise receive independent frontend generations.

No term, symbol or sort ID crosses generations. Solver goals, model conversion
chains, callbacks and engines die before their frontend. Returned typed models
own their signature and interpretation; a context mutation invalidates the
session's cached outcome, but cannot corrupt a model retained by an API caller.

`smtlib::Parser(std::istream&)` now retains a stream-backed `Lexer` and returns
one owned command from `next_command()` directly. The separate `CommandReader`
framing pass is removed. The memory constructor owns its input and uses the
same single-pass lexer/parser operations; `parse_commands()` consumes the
remaining commands. Command responses are typed and printed by the SMT-LIB
CLI output adapter (`cli::print_response`), avoiding a dependency from the
SMT-LIB library back into SMT. The CLI flushes responses before reading another command.

### Operations, costs and obligations

- Append: validate immediately against the active signature, then retain the
  command. Never allow a declaration to replace a visible definition.
- Push: record journal boundaries; the base level cannot be removed. Pop:
  truncate the journal, discard translation state, invalidate query results.
- Check: consume an unmodified translation cache or replay the journal, then
  run the ordinary preprocessing/solve/model-validation pipeline. Never reuse a
  simplified goal, SAT clauses, theory lemmas or a previous winning subgoal.
- Replaying costs the sum of frontend translation costs of surviving commands;
  retained syntax uses linear space in active input. No amortized incremental
  solving bound is claimed. Query statistics expose rebuild/query counts and
  separately accumulated command parsing/translation time.
- Parameterized definitions are validated on an isolated replay plus a synthetic
  application, because the existing frontend expands their bodies only on use.
  This catches forward references and type errors immediately without changing
  search TermId ordering. For many definitions this can incur quadratic prefix
  declaration-prefix replay costs (prior assertions are skipped); a non-mutating
  type-checking interface is future work. The preservation sample below does
  not establish a bound for definition-heavy workloads.
- Reset-assertions clears user declarations, definitions and formulas, retaining
  logic/options. Reset also restores initial protocol options and start mode.
- Each check computes a model when enabled; only get-model prints it. Models
  after unknown are not supplied by this baseline. Success printing defaults
  off. Invalid commands terminate the session; unsupported options are reported
  without pretending they were enabled.

For example, checking `x >= 0`, adding `x <= -1`, and checking again must give
SAT then UNSAT even if the first query eliminated the one-sided variable.
Popping the second assertion must restore SAT. Backward model reconstruction
does not translate a new assertion about an eliminated variable.

### Supported boundary and acceptance tests

The initial surface is repeated check-sat/get-model, push/pop, reset,
reset-assertions, exit, existing declaration/definition forms and supported
logics. Global declarations, check-sat-assuming, get-value, unsat cores, output
redirection and persistent solving are not implemented. Enabling
`:global-declarations true` returns unsupported; false remains the default.
Produce-models is a start-mode option; print-success can change immediately.

`SmtSession::execute` accepts exactly one owned parsed command (or a string
convenience input) and returns `CommandResponse`;
the response may share an immutable `CheckSatOutcome` across model queries.
Unsupported commands/options return `unsupported` and leave the context intact;
syntax/type/protocol errors print `(error "...")` and terminate with exit 2.
The legacy one-query C++ API retains its outcome-based error reporting. Existing
scripts that set produce-models after set-logic must move it before set-logic
when using the CLI. An unknown answer cannot service get-model in this baseline.

Tests must cover nested scopes, zero/overflow/underflow counts, declaration and
definition reuse, generated side constraints, stale models, monotone elimination,
solved-variable substitution, disjunctive tactics, portfolio options, and live
pipe response delivery without EOF. The one-query API rejects multi-query or
scoped scripts rather than coalescing them. Dedicated QF_UFLRA solving stays
unchanged. Acceptance requires ASan, ordinary checks/lint/docs, static review,
SLURM scoped-session differential fuzzing with per-query model checks, and a
separate five-logic single-query preservation comparison recorded in run-log.

## Running and independently validating sessions

Start `./build/primo --quiet` with no filename to read commands directly from
stdin. Submit each command below when ready; no EOF is needed between commands.
In a terminal, press Enter to deliver the typed command. Programmatic clients
must flush their writes. Use `(exit)` to finish the session.

```lisp
(set-option :produce-models true)
(set-logic QF_LIA)
(declare-const x Int)
(assert (>= x 0))
(check-sat)
(push 1)
(assert (<= x (- 1)))
(check-sat)
(pop 1)
(check-sat)
(get-model)
(exit)
```

The answers are sat, unsat, sat, followed by a model of the surviving context.
Both `primo file.smt2` and piped stdin execute commands in order. Stdin needs no
EOF to answer a complete command; startup diagnostics and per-query `--stats`
go to stderr. Signal cancellation retains the existing bounded unknown response.

`scripts/validate_incremental.py` drives the actual session interactively,
requests a model only after SAT, and writes query-indexed context snapshots.
Each reference check uses that snapshot, and Dolmen checks the **session's**
printed model, not a fresh Primo solve. All rows remain provisional until the
candidate exits successfully. It retains inputs, model responses, reference
and validator output, binary identity, and commands. This correctness workflow
does not add validation time to any benchmark measurement.

```bash
python3 scripts/validate_incremental.py \
  --solver build/primo --solver-arg=--quiet \
  --ref contrib/local/bin/z3 --dolmen dolmen \
  --out results/incremental-example \
  benchmarks/smoke/incremental/monotone_scopes.smt2
```

For generated coverage, `scripts/slurm_incremental_gate.sbatch` defines four
250-session shards. Prepare/preflight with the repository workspace helper;
supply workspace, output root and expected revision through its documented
environment variables. No jobs are submitted implicitly. The legacy real-corpus
single-query validator continues to reject incremental scripts; use this
query-aware driver explicitly. Assumptions and global declarations remain
outside this driver's supported input surface.

### Real-file streaming and scoping check

`scripts/check_incremental_corpus.py` tests the hash-pinned sample in
`benchmarks/incremental-parser-real-25.json`: five real SMT-LIB files per
supported logic, selected across families from the previous preservation sample.
These are non-incremental benchmark formulas adapted into sessions, not native
incremental benchmarks and not a speed evaluation.

The test preserves source declaration, definition and assertion command text.
It compares the ordinary file invocation with an actual stdin session, sending
commands in at most 127-byte flushed chunks. A second session checks seven
contexts: the original formula, an independent scoped Boolean constraint, a
nested contradiction, restoration after pop, redeclaration of the popped probe,
the empty context after removing all original declarations/assertions, and
replay of the entire original declaration/assertion scope. The expected answers
are derived from the file's declared SMT-LIB status and these elementary
satisfiability-preserving/contradictory transformations. The manifest pins input
hashes and agreeing historical Primo results; no fresh reference calls occur.
This oracle is not a new independent UNSAT proof or a recorded Z3 comparison.

The shared `validate_incremental.validate` accepts an optional expected-answer
sequence; without it the existing live-reference workflow is unchanged.
Answer counts, order and final process exit must all match. Every actual SAT
model is checked by Dolmen against the independently journaled active context,
including declarations and definitions. Per-query results remain provisional
until clean process exit. Original files, sent streams, query contexts, models,
validator output and provenance are retained separately. Missing hashes,
unsupported command shapes and inconclusive queries fail the gate explicitly.
The batch wrapper is `scripts/slurm_incremental_corpus.sbatch`; source preparation
and preflight still use the standard repository helper.
The theory-conflict budget is explicit (`--max-theory-conflicts`, batch override
`PRIMO_THEORY_CONFLICT_LIMIT`). Check historical conflict counts before selecting
it: the first real-file gate's 1,000-conflict budget was below the recorded 1,143
conflicts of `NEQ/NEQ033_size4.smt2`. Its ordinary file invocation returned
`unknown` before streaming was exercised; that is incomplete resource-limited
coverage, not evidence of a parser regression. Preserve the original attempt
and retry only the affected shard under a separately recorded larger budget.

The completed SLURM sample passes all 25 file invocations, 50 streamed sessions
and 200 streamed queries, with 121/121 actual SAT models accepted directly by
Dolmen. Coverage includes all five supported logics; each has five files and
40 streamed queries. Model counts are 11 for QF_UF, 29 for QF_LIA, 23 for QF_LRA,
29 for QF_UFLIA and 29 for QF_UFLRA. There are no final wrong answers, crashes,
timeouts, unknowns, parser/type errors, invalid models or missing selected rows.
The initial UF resource-limited attempt remains recorded rather than counted
as a pass. The UF-only retry uses 10,000 conflicts; other logics retain the
successful 1,000-conflict runs. Solver sources are identical across the two
test-driver revisions (`e5a634d` and `25eb7df`). Exact input/binary identities,
per-family totals and selected artifact paths are in
`benchmarks/incremental-parser-real-summary.json` and run-log entry
`2026-09-10-incremental-parser-real-corpus`. This is bounded correctness evidence,
not a general merge-readiness or full native-incremental-corpus claim.

### Definition binding invariant

A parameterized definition's free symbols refer to the declaration environment,
not the caller's let bindings. `Frontend` keeps a stack of local-scope visibility
bases during macro expansion. Arguments are built in the caller's environment;
the body can see only its own parameter scope and lets introduced inside it.
Nested definition calls establish another base and restore the caller's base
on return. This original lexical-binding implementation follows SMT-LIB 2.7
Sections 3.6.2–3.6.3 and 4.2.3; it does not clone or substitute solver source code.
The base stack adds constant work per call; lookup scans only visible local
scopes. A regression fixes a formerly SAT answer for a definition referring to
global `a` while the caller locally binds `a` to true and globally asserts
`not a`. That formula is UNSAT, independently of the caller binding.

## Qualification of the rebuild baseline

Source revision `5bc811b` passed `make check` (31 CTest and 132 Python tests),
`make asan-test` (31 CTest), `make lint`, and strict `make docs`. Whole-tree
SLURM static review covered all 109 tracked non-generated C++ files in both
configured clang-tidy and advisory include-cleaner modes. No changed file had
a diagnostic. The existing `tactic.cpp` shared-pointer analyzer warning and
transitive-header advisories remain explicit; this is not a warning-free claim.

The fresh scoped-session gate (`141595_0–3`) passed 1,000 generated sessions,
200 per supported logic: 15,000 Z3-matched answers, including 10,000 SAT models
independently accepted by Dolmen. Each model came from the live session and
was checked against its active query context. There were no wrong answers,
invalid models, crashes, parser/type errors, timeouts or unsupported rows.
This merge gate exercises generated scope/definition patterns, not arbitrary
incremental SMT-LIB corpora or a broad default-promotion campaign.

Single-query preservation used 20 fixed real inputs per logic, selected before
measurement from recorded successful runs. Two alternating baseline/candidate
rounds ran on SLURM node-03 (`141604_0–4`), with 10 seconds per input and
`--quiet --stats`. The archived `b883d6f` baseline has identical solver/build
sources to branch point `9803d1a`; no reference-solver timings were rerun.

| Logic | Solved executions, baseline → session | Baseline seconds | Session seconds | Ratio |
| --- | ---: | ---: | ---: | ---: |
| QF_UF | 40 → 40 | 27.371903 | 27.727465 | 1.0130 |
| QF_LIA | 40 → 40 | 24.285512 | 24.408226 | 1.0051 |
| QF_LRA | 40 → 40 | 25.686654 | 25.882490 | 1.0076 |
| QF_UFLRA | 40 → 40 | 19.316283 | 19.578114 | 1.0136 |
| QF_UFLIA | 40 → 40 | 20.035703 | 19.969439 | 0.9967 |
| Total | 200 → 200 | 116.696055 | 117.565734 | 1.0075 |

All executions solved, so the displayed totals also equal summed PAR2. Both
arms had zero timeout/error/unsupported/wrong-answer rows, and every shared
non-timing statistic matched. The separate post-timing model stage
(`141617_0–4`) covered all 122 candidate SAT rows, with direct Dolmen acceptance
and unchanged input/binary hashes. Model checking time is excluded above.

This establishes a usable rebuild baseline with a measured protocol cost, not
strict performance preservation. The existing comparator's 1.01 threshold
flags UF and UFLRA; its raw `needs-triage` verdicts are retained. Family-level
results are in `benchmarks/incremental-preservation-summary.json`. For example,
Goel rises 5.7%, cpachecker-induction 1.5%, and the selected Certora rows fall
1.0%; there are no lost solves. Small family samples are not corpus-wide claims.

Callgrind job `141635` profiled the selected Goel frogs input with both archived
binaries. Instructions rose from 3.473 to 3.782 billion; the new command framer
accounts for 180.7 million inclusive instructions (4.8% of the candidate total).
The EUF solve path is essentially unchanged (1.272 versus 1.270 billion).
This supports protocol framing/lifecycle cost rather than changed search as the
main explanation on that input; instruction counts are not wall-time fractions.
The framer scans command boundaries before the ordinary parser scans syntax.
Combining those scans safely, including live-pipe behavior, is future parser
work, not justification for reusing preprocessed state. A definition-heavy
stress campaign and full-corpus performance qualification are also outside this
bounded first implementation.

Exact manifests, hashes, commands, family metrics and run limitations are
recorded in [](run-log.md), entry `2026-09-09-incremental-smt-sessions`.
Artifacts live under `results/incremental-5bc811b` locally and on the recorded
SLURM workspace; full generated-query artifacts remain on SLURM. Reproduction
uses `scripts/slurm_incremental_preservation.sbatch` for separate benchmark
and replay phases, and `scripts/slurm_incremental_profile.sbatch` for diagnostic
instruction profiling. Both require explicit prepared-workspace and artifact
paths; neither submits jobs implicitly.
