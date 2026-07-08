---
name: smt-delta-debugging
description: Reduce failing SMT-LIB inputs, isolate wrong-answer bugs, and turn minimized failures into regression tests.
---

# SMT delta debugging skill

## Goal

Given a failing SMT-LIB input, minimize it while preserving the failure.

## Failure kinds

Support:

- crash
- timeout
- parser error
- wrong sat/unsat answer
- invalid model
- assertion failure
- reference solver disagreement

## Workflow

1. Confirm the failure.
2. Record exact reproduction command.
3. Classify the failure.
4. Run syntactic reduction.
5. Run semantic-preserving reductions.
6. Re-check against reference solvers.
7. Save minimized file under `benchmarks/regressions`.
8. Add a regression test.
9. Explain the likely bug location.

## SMT-LIB reductions

Try:

- remove assertions
- simplify Boolean structure
- replace subterms by constants of same sort
- remove unused declarations
- reduce function arities where possible
- reduce number of sorts
- inline `let`
- eliminate redundant equalities
- shrink term depth
- shrink numerals for arithmetic
- replace complex terms by fresh constants

## Validity constraints

Preserve:

- well-sortedness
- declared-before-used symbols
- selected logic if possible
- original failure kind

## Output format

Report:

- original file
- minimized file
- original size
- minimized size
- failure predicate
- reproduction command
- suspected component
