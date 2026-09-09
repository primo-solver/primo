# Incremental SMT sessions

## Design and implementation checklist

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

`smtlib::CommandReader` frames one complete command from an input stream and
passes it to `Parser`. It understands lexical quoting/comments only, not a
second term grammar. Command responses are typed and printed by the SMT-LIB
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

`SmtSession::execute` accepts exactly one command and returns `CommandResponse`;
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
