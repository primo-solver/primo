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

### Allocation-boundary regressions

For dangling-reference or iterator-invalidation fixes, preserve the reduced
failure and add coverage that makes the backing storage cross a capacity
boundary during the operation under test. Prefer deterministic capacity-aware
setup through existing APIs; otherwise exercise varied arena occupancies and
record that this is weaker coverage. Insert distinct terms so hash-consing
cannot silently turn intended allocations into reuse. Do not rely on a fixed
vector growth factor or expose mutable storage solely for a test.

Run the relevant regression under ASan as well as checking its semantic result.
When feasible, confirm that it fails on the pre-fix code and passes on the fix.
Cover analogous construction branches and nested helper calls that can allocate.
A green sanitizer suite without exercising the invalidation boundary does not
establish lifetime safety; retain the static borrow/mutation/use review.

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
