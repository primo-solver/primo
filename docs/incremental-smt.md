# Incremental SMT sessions

## Design and implementation checklist

This branch implements an original rebuild-based session baseline, not persistent
incremental SAT, theory solving, or incremental tactics. During development the
items below distinguish the implemented interface from pending SLURM qualification.

### Sources and implementation status

- Barrett, Fontaine and Tinelli, [SMT-LIB 2.7, March 2026 release](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-03-27.pdf),
  Sections 4.1.1–4.1.7 and 4.2.1–4.2.3: command modes, responses, scoped
  declarations/definitions, options, and stack operations specify the protocol.
- de Moura and Passmore, [The Strategy Challenge in SMT Solving](https://www.cl.cam.ac.uk/~gp351/moura-passmore-smt-strategy.pdf),
  Section 3: ordered goals, disjunctive subgoals and backward model converters
  underpin existing Primo tactics. This is not an incremental-context algorithm.
- Bjørner and de Moura, [Z3 Internals (draft)](https://z3prover.github.io/papers/z3internals.html),
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
  type-checking interface is future work, and this
  cost must be measured before performance acceptance.
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
