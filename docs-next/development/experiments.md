# Reproducible experiments

An experiment needs a question, a fixed input set, exact configurations, and a
decision criterion. The [correctness gate](correctness.md) applies before
performance conclusions. Use [statistics](../reference/statistics.md) to check
that the mechanism under investigation actually ran.

## Prepare the comparison

Record candidate and baseline commits, dirty-tree status, build type, compiler,
CaDiCaL signature, options, input manifest, expected answers, time/memory limits,
and output directory. Check each executable's `--version`; a workspace's Git
revision does not establish which binary was executed. Preserve an exact baseline
binary or build it from an isolated checkout.

The supplied SLURM workflows are reproducible execution options; contributors
can use another adequate test environment while recording the same provenance.
For the maintainer's cluster, use
{source}`workspace preparation <scripts/slurm_prepare_benchmark_workspace.py>`
before submitting jobs. Read its `--help` for connection and staging options.
It synchronizes tracked files; ignored seed corpora must be staged **after each
sync**. Check dependencies and corpus paths on the execution host, not only on
the submission host. Do not substitute a symlink for an in-workspace corpus
when the harness resolves paths relative to that workspace.

Use one output directory per run. Confirm both baseline and candidate execute
correctly through the actual harness before launching the full set. Record
array size and expected rows per shard; a missing shard is not a timeout row.

## Select inputs and interpret outcomes

{source}`analyze_smt2.py <scripts/analyze_smt2.py>` distinguishes declarations,
applications, definitions, and expanded structure:

```bash
python3 scripts/analyze_smt2.py --json path/to/input.smt2
```

Text search alone cannot establish which theory or transformation a file uses.
Keep corpus exclusions explicit: parse rejection, unsupported constructs,
missing files and reference disagreements cannot silently disappear from the
denominator. An SMT-LIB `:status` is an expected answer, not an independent
proof. Inspect both answer text and process exit status, including multi-query
response counts and abnormal exits after an answer was printed.

Run speed measurements without independent validation in the timed interval.
Then [validate SAT models separately](validation.md), using the same binary,
options and inputs. Stop tuning on a wrong answer, invalid model, crash or
untriaged disagreement. Report timeouts and `unknown` separately from passes.

## Tactic-composition checks

The {source}`composition manifest <benchmarks/tactic-composition-matrix.json>`
defines legal profiles, their logic sets, and required mechanism counters.
The {source}`driver <scripts/tactic_composition_matrix.py>` compares references,
checks answers, and independently validates models. A reproducible invocation
inside the chosen test environment is:

```bash
python3 scripts/tactic_composition_matrix.py \
  --manifest benchmarks/tactic-composition-matrix.json --root . \
  --solver ./build/primo --refs contrib/local/bin/z3 contrib/local/bin/cvc5 \
  --dolmen contrib/local/bin/dolmen --timeout 10 --dolmen-timeout 20 \
  --repeat 1 --out results/composition-review
```

{source}`The SLURM matrix <scripts/slurm_tactic_composition_matrix.sbatch>`
adds generated cases by logic. Do not require every tactic on every input:
composition can stop after a terminal result. A per-input requirement checks
that the intended route was reached on that case; an aggregate requirement
checks that a tactic was exercised somewhere in the eligible corpus. Preserve
both checks. Report inconclusive rows accepted by `--allow-inconclusive`
separately; that switch does not turn them into successful solves.

## Summarize before deciding

```bash
python3 scripts/summarize_benchmark_shards.py --timeout 30 \
  --run baseline=results/baseline --run candidate=results/candidate \
  --baseline baseline --candidate candidate
```

Inspect expected/completed rows, duplicate inputs, solved counts, PAR2,
recoveries, losses, decisive-answer disagreements, family results, and common
solved timings. PAR2 charges an unsolved instance twice the specified timeout;
it must use the same input set and limit on both sides. For native sessions,
whole-session completion, partial query progress and per-query time are
different measurements. Keep them separate.

An aggregate loss can still reveal a useful portfolio configuration if it
solves inputs the others miss. Record that complementarity, but do not promote
the configuration to default on that basis alone. Record rejected experiments
as rejected; remove their source changes before closing the investigation.

## Evidence storage

The authoritative ledger remains {source}`docs/run-log.json <docs/run-log.json>`.
Follow the {source}`recording contract <docs/run-log.md>`: stable run ID,
timestamps, kind/scope, exact commit and commands, generator/seed/count,
references, environment/job IDs, limits, artifact paths, summary counts,
evidence status, and limitations. Commit metadata and reproducibility scripts
with the change relying on them. Do not add a solver run entry for a prose-only
documentation build.

Git-based run directories and a derived SQLite query index are an agreed
[future storage design](../work/results-storage.md). They are not current
services. Large raw artifacts may remain outside Git, but the ledger must make
them identifiable and retrievable. A record marked current describes its own
measured revision and scope, not every subsequent commit.
