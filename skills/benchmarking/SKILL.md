---
name: smt-benchmarking
description: Run reproducible solver benchmarks, compare solver versions, collect statistics, and produce benchmark reports.
---

# SMT solver benchmarking skill

## Goal

Measure solver performance reproducibly.

For benchmark/evaluation work, read [the execution procedure](references/execution.md)
before running commands. It owns the profiling, SLURM, evidence and acceptance
requirements formerly in `AGENTS.md`.

For real-corpus evaluations and default promotions, use
[`primo-model-validation`](../primo-model-validation/SKILL.md) for the separate
post-run SAT-model stage. Keep speed results unchanged and report validation
coverage alongside them; a sample does not qualify a full-corpus result.

## Rules

Never report performance without:

- exact commit hash
- benchmark set
- timeout
- machine info if available
- command line
- number of solved instances
- runtime distribution
- timeout count
- error count

## Benchmark categories

Use separate benchmark sets:

- smoke: tiny correctness tests
- regression: previous bugs
- development: medium hand-picked instances
- external: SMT-LIB or other imported benchmark suites
- fuzzed: generated instances

## Metrics

Collect:

- result: sat/unsat/unknown/error/timeout
- wall-clock time
- memory if available
- number of SAT conflicts
- number of decisions
- number of theory checks
- number of theory conflicts
- number of lemmas
- number of propagations
- number of terms
- number of clauses

## Comparison mode

Compare:

- baseline commit
- current commit

Report:

- solved delta
- median runtime ratio
- geometric mean where meaningful
- regressions above threshold
- improvements above threshold

## Output format

Produce:

1. Benchmark configuration
2. Summary table
3. Regressions
4. Improvements
5. Suspicious results
6. Reproduction commands

Do not overinterpret small benchmark sets.
