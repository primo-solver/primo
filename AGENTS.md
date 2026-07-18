# Primo Agent Guide

## Clean-room policy

Primo is a new solver, not a continuation of LLM2SMT.

The old repository may be used only for:

- understanding previous design decisions
- identifying missing tests
- comparing behavior
- extracting benchmark ideas
- writing retrospectives
- checking which development practices failed or succeeded

The local reference checkout is normally at `/tmp/llm2smt` on this workspace.
Check that path first when comparing against LLM2SMT behavior.

Do not copy source code, generated code, parser grammars, build files, comments, tests, or documentation text from LLM2SMT or from other SMT solvers.

When implementing an algorithm, use:

- the cited paper
- our own design notes
- newly written tests
- newly written code

If a design choice is inspired by LLM2SMT, document it as a retrospective note, not as an implementation source.

## Paper-grounded implementation rule

This solver is implemented from papers and original design notes.

Do not copy code from existing SMT solvers.

If a required paper is hard to access or download, stop and tell the user which paper is needed. Do not substitute a blog post, secondary summary, or guessed details for the unavailable paper.

Every nontrivial algorithmic component must cite the paper, book chapter, or design note it is based on.

Citations are not decoration. Before implementing or claiming a paper-backed
algorithm, read the relevant paper section and turn it into a local design note
or implementation checklist. The checklist must identify the actual data
structures, invariants, operations, complexity expectations, and proof
obligations being implemented. If the implementation is only a scaffold,
baseline, or ordinary textbook subset, say that explicitly and do not name it as
an implementation of the paper's optimized algorithm.

When using a paper to accelerate development:

- implement from the paper's algorithmic content, not from memory of a solver or
  from a previous codebase;
- map each paper operation to the Primo layer and C++ type that owns it;
- add tests that exercise the paper-specific invariant or asymptotic fast path,
  not only generic correctness examples;
- add benchmark counters that reveal whether the paper mechanism is actually
  being used;
- compare the implementation against the paper's stated preconditions and mark
  unsupported cases as limitations;
- if the code does not yet implement the paper mechanism, document the paper as
  motivation/future work, not as implemented behavior.

For each such component, include:

- bibliographic reference
- section or theorem used, where applicable
- implemented subset
- deviations from the paper
- known limitations
- tests derived from the algorithm

Code comments should identify algorithmic basis without overclaiming.

## Documentation synchronization rule

Documentation is part of the implementation, not a follow-up task.

Do not consider a solver change complete unless the relevant documentation is up to date. For changes to parser, terms, symbols, SAT integration, clausification, theory solving, options, build behavior, or solver control flow:

- update the nearest page under `docs/`
- describe the implemented C++ types, ownership, lifetime, invariants, and limitations
- explain whether paper-backed ideas are implemented, partially implemented, or only planned
- keep examples and supported-fragment claims synchronized with the code
- run `make docs`
- commit documentation together with the implementation or in an adjacent commit

If documentation cannot be updated in the same task, state that as a blocker in the final response.

## Goal and completion discipline

For broad solver tasks, define the goal as the full user-visible outcome, not
as the next command, the next local experiment, or a pointer to another file.
The goal is the whole thing the user is asking to get done. A performance goal
includes correctness, implementation, documentation, verification, benchmark
evaluation, result analysis, and commits.

When the user asks to use the goal primitive, create one goal whose objective
spells out the end-to-end acceptance criteria in the goal text itself. Include
the target benchmark set, baseline or reference comparison, required execution
environment, hard correctness conditions, required verification commands,
documentation expectations, and commit expectations. Do not create a goal like
`continue`, `read the plan`, `run benchmarks`, or `fix performance`.

Do not mark that goal complete until all required source, tests, docs, scripts,
and benchmark metadata are committed and the requested evaluation is summarized,
or until a concrete blocker is reached. If a blocker is reached, report the
blocker, the commands or job ids proving it, the last successful verification,
and the remaining acceptance criteria that are still incomplete.

Do not treat any of the following as completion:

- a local-only timing run when the user requested SLURM or full-benchmark
  evaluation;
- a solved-count or PAR2 improvement with known wrong answers;
- an unsound implementation that happens to improve performance;
- uncommitted source, documentation, tests, scripts, or benchmark metadata
  needed to reproduce the result;
- untracked generated implementation files;
- benchmark logs without a summarized comparison against the stated baseline or
  reference solvers;
- an active experiment whose winning or losing status has not been decided and
  committed as either accepted implementation or rejected result artifact;
- a final answer that omits commit hashes, benchmark artifact paths, job ids, or
  verification commands for a broad solver task.

Before the final response for broad solver work, run `git status --short` and
`git log --oneline -3`, then report any remaining tracked or required untracked
files. If committing is feasible, commit coherent milestones before reporting
completion. If committing is not feasible, state the exact blocker and do not
present the work as complete.

## Architectural rules

Preserve these boundaries:

- `smtlib/` parses, prints, and type-checks SMT-LIB.
- `core/` owns sorts, symbols, terms, rewriting, substitutions, and models.
- `sat/` owns propositional variables, literals, clauses, assumptions, and the IPASIR-UP backend adapter.
- `smt/` owns Boolean abstraction, clausification, assertion stacks, and DPLL(T).
- `theory/` owns theory solvers and theory-specific explanations.
- `scripts/` contains reproducible command-line workflows.
- `benchmarks/` contains only input instances and benchmark metadata.
- `docs/` contains the MyST/Jupyter Book documentation site, design notes, literature summaries, and optional experiment notebooks.

Do not introduce cross-layer shortcuts.

In particular:

- SMT-LIB AST nodes must not be used by the SAT or theory solvers.
- SAT literals must not directly contain terms.
- Theory solvers must communicate via theory literals and explanations.
- All terms must be created through the term manager.

## Coding practices

- Do not stop at a partial milestone when following an explicit implementation
  plan or performance objective. Continue through implementation, verification,
  documentation, evaluation, result comparison, and commits until the objective
  is complete, or until there is an obvious roadblock such as a missing required
  paper, unavailable benchmark data, inaccessible cluster, failing dependency
  install, or a correctness failure that needs a design decision from the user.
  If a roadblock occurs, state the concrete blocker, the evidence for it, the
  last completed verification, and the exact unfinished work.
- Use C++20.
- Keep C++ source formatted with `clang-format` using the repository `.clang-format`.
- Keep Python scripts runnable with the standard library unless a dependency is documented.
- C++ formatting is `clang-format` with the repository `.clang-format`, currently based on Google style with C++20 and a 100-column limit. Do not hand-format around it.
- C++ warnings are intentionally strict for owned code. The default CMake build treats warnings as errors; generated ANTLR sources and external headers are isolated from that policy.
- Use `make asan-test` when touching ownership, parser, term storage, SAT backend, or other memory-sensitive C++ paths.
- Run `make lint` before committing implementation changes.
- Run `make check` before committing behavior changes when feasible.
- Run the fuzz gate described in "Fuzzing discipline" before presenting solver,
  parser, preprocessing, SAT, theory, model, or SMT-LIB behavior changes as
  complete. If the gate cannot run, state the exact blocker and keep the change
  out of headline/default performance claims until fuzzing is done.
- Run the `smt-code-review` skill as a pre-commit self-review for implementation changes. At this stage, review all tracked non-generated project files, not only the diff. The review must include ordinary hygiene: unused includes, dead helpers, missing or misleading comments, narrow API surface, formatting, and lint readiness.
- If `clang-tidy` is available, run it on all tracked non-generated `.cpp` files while the project is establishing its baseline. Also run `misc-include-cleaner` as an advisory scan and inspect it for real unused includes; paired `.cpp`/`.hpp` self-includes are not automatically removable. Later, after the whole-tree baseline is clean, diff-focused clang-tidy plus periodic whole-tree sweeps is acceptable. If clang-tidy is not run, state why.
- Prefer small, coherent commits after each working milestone.
- If a change is rejected after measurement, either revert it cleanly or leave no
  source changes from it, then commit the reproducibility artifact or note that
  records why it was rejected when that artifact is part of the benchmark trail.

## Solver performance and benchmark discipline

Correctness comes before performance. If a benchmark, fuzzer, or reference-solver
comparison finds wrong answers, disagreements, crashes, or unsupported behavior
being misreported as solved, stop performance tuning and fix or precisely triage
that issue first. Do not claim a performance win while wrong answers remain.

Use reference solvers only for answer comparison, benchmark triage, and
identifying performance gaps. Do not copy implementation code, generated code,
parser grammars, tests, comments, documentation text, or build logic from them.

For solver benchmark work:

- record the exact benchmark set, timeout, command line, commit, host or SLURM
  job id, and result artifact path;
- add or update an entry in `docs/run-log.json` for every nontrivial benchmark,
  fuzzing, profiling, parameter-tuning, dependency-profile, or reference-solver
  comparison run before citing it in a final response, commit message, design
  note, or retrospective;
- report solved, timeout, error, unsupported, wrong-answer, and PAR2 counts when
  the harness supports them;
- include family-level summaries for mixed benchmark sets;
- compare against the relevant previous Primo result and, when requested, fast
  reference solvers such as Z3 and cvc5;
- keep benchmark scripts and manifests reproducible and committed when they are
  part of the result.

`docs/run-log.json` is the canonical lightweight database of experimental
evidence. Do not leave important evidence discoverable only through untracked
`results/`, `fuzz-results/`, terminal scrollback, or memory. If an artifact is
too large to commit, commit the run-log entry and the script/manifest needed to
reproduce it, and store the artifact path, host, job id, summary, and whether the
artifact is local-only, SLURM-backed, stale, rejected, or current evidence for a
specific commit. If a historical artifact is discovered after the fact, add a
run-log entry with `evidence_status` set to `historical` or `needs_revalidation`
instead of silently relying on it.

When a user asks to improve performance on a named benchmark archive or corpus,
the expected loop is:

1. establish a reproducible current Primo baseline;
2. identify cases solved quickly by reference solvers but not by Primo;
3. profile representative slow Primo cases locally when the user permits local
   profiling;
4. implement a correctness-preserving change from papers or local design notes;
5. run focused tests and smoke checks;
6. run the requested full benchmark environment, such as SLURM;
7. compare solved counts, wrong answers, family summaries, and PAR2 against the
   baseline;
8. commit accepted source, docs, tests, scripts, and required artifacts, or
   revert and record a rejected experiment.

Repeat that loop while the goal remains open and there is no blocker. Do not
stop merely because one iteration is disappointing, one optimization regresses,
or results are worse than expected.

Be proactive about standard SMT performance engineering. When a fast reference
solver is far ahead, do not wait for the user to name basic techniques one by
one. Inspect the benchmark shape, compare current stats to reference behavior,
and actively pursue the traditional safe targets first: simplification,
equation solving, ITE preprocessing, local-context Boolean simplification,
unit/value propagation, symmetry breaking, and ordinary profiler evidence. If a
technique has already been discussed in the thread or documented as a likely
gap, treat it as part of the active triage queue until it has either been
implemented and evaluated or explicitly rejected with evidence.

If the user requests SLURM evaluation, use the workstation only for development,
profiling, smoke tests, and debugging. Headline benchmark claims must come from
SLURM artifacts. If SLURM is unavailable, state that as a blocker and include
the failed access command or job state.

Local profiling is still expected when performance is unexpectedly poor. Use it
to identify bottlenecks before designing solver changes, then confirm any claimed
improvement with the requested benchmark infrastructure.

## Fuzzing discipline

Fuzzing is a correctness gate, not an optional cleanup. Any change that affects
parsing, type checking, term construction, rewriting, preprocessing,
clausification, SAT integration, DPLL(T) control flow, theory solvers, model
construction, generated lemmas, or solver options that alter behavior must run a
fresh fuzz gate against the current commit before it is described as complete.

Use the smallest gate that matches the change risk:

- Smoke gate: for narrow parser/printer/core rewrites with low solver impact,
  run at least 100 generated or mutated inputs locally if the generator is
  installed, plus the relevant unit tests. This gate is not sufficient for
  default-profile, theory, SAT, or preprocessing changes.
- Merge gate: for ordinary solver/theory/preprocessing/SAT behavior changes,
  run at least 1000 generated or mutated inputs against a trusted reference, or
  a stronger existing scripted gate such as `make fuzzsmt` when it covers the
  touched logic. Prefer SLURM when runtime is nontrivial.
- Campaign gate: before promoting a performance profile, online propagation
  mode, preprocessing transformation, theory solver, or SAT interaction to
  default/headline status, run at least 3000 QF_UF FuzzSMT-style cases for QF_UF
  work, and a comparable corpus for other supported logics. Also run a
  seed-mutation campaign such as YinYang/typefuzz on real benchmark seeds when
  available.

Every fuzz run must record:

- commit hash;
- command line and reference solver command;
- generator, seed, count, timeout, host or SLURM job id;
- summary counts for pass, wrong answer, crash, parser/type errors, timeout,
  unsupported, and reference disagreement;
- artifact directory containing generated failures.

Record the same information in `docs/run-log.json`. A fuzz run without a
run-log entry is not acceptable evidence for completing a solver change, even if
the raw `summary.csv` exists.

Wrong answers, crashes, parser rejections of valid generated input, invalid
models, and reference disagreements block performance work until fixed or
precisely triaged. Timeout-only fuzz results do not block correctness, but they
must be summarized separately from pass results.

Stale `bugs/` directories are not evidence either way. Before relying on an old
bug corpus, rerun each saved file against the current binary and references,
classify the outcome, and either add a regression test, document an unsupported
feature, or delete/ignore it as stale generated output. Do not report "fuzzing
passes" if unclassified old bug files are still being treated as active.

If a fuzzing dependency is missing locally or on SLURM, install it when feasible.
If it cannot be installed, report the failed install/access command as the
blocker and say exactly which gate remains incomplete.

## Core invariants

### Term invariant

- Terms are hash-consed.
- Term equality is by `TermId`.
- Every term has an explicit sort.
- Boolean formulas are terms of sort `Bool`.
- Applications are represented uniformly.
- All terms are created through the term manager.

### SAT invariant

- Primo does not implement its own SAT solver.
- SAT solving is delegated through IPASIR-UP, initially backed by CaDiCaL.
- The SAT backend sees only clauses over SAT literals.
- SMT atoms are mapped to SAT variables by the SMT layer.
- The SAT backend must not inspect theory terms.
- Assumptions are distinct from permanent clauses.
- Backend-specific details must stay behind the SAT adapter.

### Theory invariant

A theory solver receives asserted theory literals.

It may return:

- consistent
- conflict clause
- propagated literal with explanation
- model fragment

### DPLL(T) invariant

- The first implementation may use lazy theory checking.
- Theory propagation is optional until the EUF checker is correct.
- Every generated theory lemma must be independently checkable by tests.
