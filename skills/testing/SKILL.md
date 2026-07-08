---
name: smt-testing
description: Design and implement unit, regression, differential, and SMT-LIB smoke tests for the SMT solver.
---

# SMT solver testing skill

## Goal

Add tests that would catch solver unsoundness, incompleteness in the intended fragment, parser/type errors, and model-generation bugs.

## Test levels

### Unit tests

Use for:

- term manager
- sort checker
- parser
- rewriter
- union-find
- congruence closure
- SAT clauses
- Boolean abstraction

### Regression tests

Use for:

- previously failing SMT-LIB files
- reduced fuzzing failures
- solver crashes
- wrong sat/unsat answers
- invalid models

### Differential tests

Compare result with trusted solvers:

- Z3
- cvc5

Only compare `sat`, `unsat`, and `unknown` unless model validation is implemented.

### Smoke tests

Keep tiny hand-written SMT-LIB files in `benchmarks/smoke`.

## Required test classes for QF_UF

Include:

- reflexive equality
- equality transitivity
- congruence through unary functions
- congruence through binary functions
- equality/disequality contradiction
- uninterpreted predicate abstraction
- nested function applications
- multiple uninterpreted sorts
- push/pop around conflicts
- model generation for constants and functions

## Test-generation policy

When adding a feature:

1. Add positive tests.
2. Add negative/type-error tests if applicable.
3. Add at least one SMT-LIB regression test.
4. Add a differential test if Z3/cvc5 supports the fragment.

## Output format

When asked to add tests:

- explain the missing coverage
- add minimal tests
- run the relevant test command
- report failures precisely
