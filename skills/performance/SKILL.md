---
name: smt-performance-improvement
description: Profile SMT solver performance, identify bottlenecks, propose optimizations, and validate them with benchmarks.
---

# SMT performance improvement skill

## Goal

Improve performance without breaking soundness.

## Rules

Do not optimize without a baseline measurement.
Do not change algorithmic behavior without tests.
Do not report speedups without benchmark evidence.

Use [the benchmarking skill](../benchmarking/SKILL.md) for experiment execution
and acceptance, including its separate real-corpus model-validation stage.

## Workflow

1. Select benchmark set.
2. Record baseline.
3. Profile.
4. Identify bottleneck.
5. Propose small change.
6. Implement change.
7. Run unit and regression tests.
8. Re-run benchmark.
9. Report improvement/regression.

## Common SMT solver bottlenecks

Check:

- term allocation
- hash-consing
- substitution/rewrite loops
- Boolean abstraction duplication
- clause creation
- SAT backend call overhead
- excessive assumptions or redundant clauses sent through IPASIR-UP
- repeated theory checks
- congruence closure rebuilds
- push/pop copying
- model construction over all terms instead of relevant terms only
- excessive pretty-printing/logging

## Acceptable optimizations

Prefer:

- interning
- memoization with scoped invalidation
- trail-based undo
- avoiding repeated AST traversal
- compact literal representation
- deterministic caches
- lazy model construction
- incremental congruence closure

Avoid initially:

- unsafe mutation
- global caches without clear invalidation
- parallelism
- complex heuristics without benchmark support

## Output format

Report:

1. baseline
2. profile finding
3. patch summary
4. correctness checks
5. benchmark comparison
6. remaining bottlenecks
