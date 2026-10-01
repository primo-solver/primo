# Correctness before performance

A solver contribution is complete when its behavior is explained and the
required checks support it. Wrong answers, invalid models, crashes, and
unresolved reference disagreements take priority over tuning. Faster results
cannot compensate for a correctness defect.

Documentation improvements, minimized bug reports, tests, and tools are useful
contributions alongside solver development. This guide covers the engineering
checks required for those contributions.

## Start with the contract and a regression

Identify the invariant or observable behavior being changed. For a bug, retain
a reproducer and add a test that fails for the original defect. For an
algorithm, read the primary source and write a design checklist covering the
implemented subset, owned data, operations, invariants, and proof obligations.
Stop if a required paper cannot be obtained. Explain partial implementations
and deviations explicitly.

Primo is a clean-room implementation. Other solvers can provide behavioral
comparisons; their source, tests, build files, parser grammars, and documentation
must not supply implementation material. Contributions must also respect the
[layer and ownership boundaries](../implementation/architecture.md).

Update the affected documentation with the code. Keep implemented behavior,
active work, and open leads distinct. A passing unit test alone does not
establish an algorithm's invariant or qualify a new default.

(run-the-applicable-local-checks)=
## Run the applicable checks

Run commands from the repository root:

```bash
make check
make docs
make docs-next-full
```

`make check` runs formatting/lint checks, C++ tests, and Python tests. It does
**not** run fuzzing, independent model validation, or the documentation build.
For draft previews, `make docs-next` reuses the previous build; the full target
above re-reads every page for validation.
The {source}`check target <Makefile#L139>` lists those dependencies; the
{source}`test and sanitizer targets <Makefile#L22-L32>` define their commands.
Memory-sensitive C++ changes also require `make asan-test`, including changes
to parsing, term storage, ownership, or the SAT backend. Apply the repository's
code-review and static-analysis requirements in {source}`AGENTS.md` and
{source}`skills/code-review/SKILL.md`.

Documentation-only prose changes normally need factual review and the strict
build of each affected book. Execute changed examples separately; notebook
execution is disabled in the book builds. An infrastructure check that could
not run remains incomplete, with its failed command and reason recorded.

## Choose a fuzz gate

### Fuzzing and reduction sources

Robert Daniel Brummayer, *Efficient SMT Solving for Bit-Vectors and the
Extensional Theory of Arrays*, PhD thesis, Johannes Kepler University Linz,
September 2009, Chapter 4, explains why grammar-based generation must reach
solver internals with valid formulas, and how structure- and type-aware
delta debugging reduces failures while preserving the observed defect.
Section 4.4.2 covers FuzzSMT's extension beyond bit-vectors, including linear
arithmetic.

This complements Brummayer and Biere's *Fuzzing and Delta-Debugging SMT
Solvers* (SMT 2009), already recorded in the
{source}`literature notes <docs/literature.md#L355-L411>`.
Primo uses external generators and reduction tools. The gate counts below are
project policy, not thresholds established by that thesis.

### Gate size and coverage

Fresh fuzzing is required for changes to parsing, printing, type checking,
terms, rewriting, preprocessing, clausification, SAT integration, solver control
flow, theories, generated lemmas, models, and options that alter behavior.
Use the candidate revision and the exact options being claimed. Repeat the
applicable gate after a behavior-changing fix. Contributors may use any
suitable machine; acceptance depends on the coverage, results, and reproducible
evidence recorded below.

| Gate | Applicable change | Minimum coverage |
| --- | --- | --- |
| Smoke | Narrow parser/printer/core work with low solver impact | 100 generated or mutated inputs plus focused tests. Insufficient for SAT, theory, preprocessing, or default-profile changes. |
| Merge | Ordinary solver, theory, SAT, or preprocessing behavior | 1,000 generated or mutated inputs compared with a trusted reference. |
| Campaign | Promotion of a profile, propagation mode, preprocessing transformation, theory engine, or SAT interaction to default/headline status | 3,000 generated QF_UF cases for QF_UF work, or comparable coverage for other logics, plus mutation of representative real seeds when available. |

Choose formula shapes and seeds that exercise the changed mechanism. Inspect
its activation counters where available. Thousands of inputs that bypass the
relevant path are weak evidence. In particular, generated answers do not test
model printing, and one-shot inputs do not cover session scoping.

For a QF_UF differential run:

```bash
make build contrib-fuzzsmt contrib-z3
PATH="$PWD/contrib/local/bin:$PATH" python3 scripts/fuzz_external.py \
  --fuzzer fuzzsmt --solver './build/primo --quiet' --refs z3 \
  --logic QF_UF --count 1000 --seed 20260924 --timeout 5 \
  --fuzzer-args '-ms 1 -Ms 2 -mv 1 -Mv 2 -mf 1 -mp 1 -ma 1 -Ma 2' \
  --out fuzz-results/contribution-qf-uf-20260924
```

Use a new output directory for each run. Adapt the shape/size parameters to the
change; this command is a QF_UF starting point, not a gate for every component.
The current `make fuzzsmt` shortcut runs only 20 cases. `make fuzz` is a short
mutation entry point. Neither automatically meets the table's thresholds.
See the {source}`convenience recipes <Makefile#L40-L53>` and
{source}`20-case recipe <Makefile#L141-L142>`.

Inspect the wrapper's `summary.csv`, actual generated count, and available
references. The {source}`external-fuzzer wrapper <scripts/fuzz_external.py#L99-L160>`
can return success after a missing-fuzzer
skip, and its generic YinYang/STORM wrappers are placeholders. Use
{source}`scripts/yinyang_fuzz.sh` for the implemented YinYang workflow. A zero exit code
or requested case count is not by itself evidence that those tests ran.

Logic-specific workflows live in `scripts/`; the existing book's debugging
and finite-domain-fuzzing chapters describe their coverage. Install missing
dependencies when feasible. For long campaigns or inaccessible infrastructure,
record the remaining gate and arrange a maintainer handoff before merge.

## Investigate every failure

Preserve the original input, candidate/reference commands and versions, and
stdout/stderr. Reduce the input using a predicate that retains the same
failure, then add a regression for a confirmed bug. Do not reduce a wrong
answer into an unsupported-input error and call the defect reproduced.
The {source}`minimizer entry point <scripts/minimize_smt2.py>` accepts the
failure predicate; the {source}`fuzzer's classify function <scripts/fuzz_external.py#L43-L67>`
shows how differential outcomes are separated.
Check the minimizer's output: without ddSMT it copies the original input and
returns success, so that outcome is not a successful reduction.

Classify wrong answers, crashes, parser/type errors on valid input, invalid
models, and reference disagreements separately. A disagreement requires
investigation; an external solver is not automatically correct. Demonstrated
unsupported cases need an explicit limitation, not a pass label.

Report timeouts and `unknown` separately. They are inconclusive results and
reduce coverage; a timeout alone is not proof of a correctness failure. Rerun
saved failures against the candidate before relying on an old `bugs/` directory.

## Validate models independently

Agreement on `sat` does not validate the model Primo prints. The internal
`--check-model` check is useful but shares implementation with Primo.
Full evaluations and default promotions require a separate correctness stage
covering every successful candidate SAT row for each claimed profile.

Use {source}`scripts/validate_model.py` with `--require-dolmen-pass` for supported
single-query inputs; it delegates to the
{source}`Dolmen validator <scripts/validate_qfuf_models_with_dolmen.py>`.
Use {source}`scripts/validate_incremental.py` for scoped sessions. Follow
{source}`skills/primo-model-validation/SKILL.md` for exact invocation and coverage
requirements. Preserve the measured binary, input hashes, options, and query
identity. Do not replace session models with models from fresh independent
Primo queries.

Record direct Dolmen passes, invalid models, changed answers, solver errors,
timeouts, unsupported inputs, and missing rows. A validator timeout or unsupported
input leaves coverage incomplete. Model-production and validation time must
stay outside the original runtime/PAR2 measurements. Model checking does not
certify UNSAT answers.

## Attach reproducible evidence

Every fuzz gate and substantial experiment needs a record in
{source}`docs/run-log.json`. Include the run ID, candidate commit and binary identity,
reference versions and commands, logic/corpus/manifest, generator and seed,
actual counts, options, timeout and other resource limits, host or SLURM job
ID, outcome counts, and artifact locations. Preserve failure inputs and logs.
The separate Git-results repository and generated SQLite index remain planned;
the existing ledger is still authoritative.
The {source}`recording contract <docs/run-log.md>` defines the required metadata.

Use the maintained [fuzzing](fuzzing.md), [validation](validation.md),
[debugging](debugging.md), [experiment](experiments.md), and [tuning](tuning.md)
procedures alongside these acceptance requirements. The
[statistics reference](../reference/statistics.md) explains counter meanings.

The pull request should state what changed, the violated or preserved
invariant, the documentation updated, checks run, linked run records, and
remaining limitations. Missing checks stay visible until completed. Do not
promote performance claims while correctness failures or required qualification
remain unresolved.
