# Benchmark execution procedure

Moved from `AGENTS.md`; read for solver benchmark/evaluation tasks. Repository
paths below are relative to the repository root.

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

For solver performance investigations, profile before adding new solver logic
when the evidence points to a concrete slow path. If a benchmark is clearly
slower than LLM2SMT, Z3, cvc5, or another stated reference, times out while those
references are fast, or has one phase that visibly dominates runtime, run a
standard local profiler such as `gprof`, `perf`, or an equivalent available tool
on a representative slow instance. Follow the dominating cost first, whether it
is in preprocessing, parsing, SAT integration, EUF, arithmetic, or any other
theory layer. Record the command, target instance, top functions, and before/after
timing in `docs/run-log.json` or the nearest relevant design note before citing
the result.

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

For every generated SLURM batch script:

- write the batch script to a local file before copying or submitting it;
- run `python3 scripts/slurm_validate_sbatch.py path/to/job.sbatch` locally;
- run `bash -n path/to/job.sbatch` explicitly if the validator is unavailable;
- quote embedded interpreter heredocs, for example `python3 - <<'PY'`, so the
  shell cannot expand or strip Python, Perl, or Ruby source before execution;
- avoid nested `ssh 'python -c "..."'` or nested inline Python inside remote
  shell strings when a temporary script or quoted heredoc is practical;
- immediately before `sbatch`, run
  `python3 scripts/slurm_prepare_benchmark_workspace.py --check-only ...` and
  confirm that the remote binary reports the intended git commit and CaDiCaL
  version.

Local profiling is still expected when performance is unexpectedly poor. Use it
to identify bottlenecks before designing solver changes, then confirm any claimed
improvement with the requested benchmark infrastructure.
