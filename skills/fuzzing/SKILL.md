---
name: smt-fuzzing
description: Generate, run, reduce, and triage SMT-LIB fuzz tests for solver fragments such as QF_UF and QF_LIA.
---

# SMT solver fuzzing skill

## Goal

Find wrong answers, crashes, assertion failures, nontermination, invalid models, and parser bugs.

## Fuzzing modes

### Generator-based fuzzing

Generate random well-sorted SMT-LIB formulas for a fixed logic.

Start with:

- QF_UF
- QF_LIA only after arithmetic is implemented

### Differential fuzzing

For each generated instance:

1. Run Primo.
2. Run Z3.
3. Run cvc5 if available.
4. Compare results.

Classify:

- same result
- solver crash
- timeout
- Primo `sat` vs reference `unsat`
- Primo `unsat` vs reference `sat`
- reference disagreement
- unknown involved

### Metamorphic fuzzing

Apply transformations expected to preserve satisfiability:

- reorder assertions
- duplicate assertions
- add `(assert true)`
- rename symbols
- replace `t = t` with `true`
- split conjunctions
- wrap with push/pop

## QF_UF generation rules

Generate:

- uninterpreted sorts
- constants
- functions of arity 0, 1, 2, occasionally 3
- predicate symbols
- equalities
- disequalities
- Boolean combinations
- shallow and deep terms

Respect:

- sort correctness
- declared symbols only
- bounded term depth
- bounded assertion count

## External fuzzers

Support wrappers for:

- Yin-Yang / OpFuzz / TypeFuzz
- FuzzSMT
- STORM, later
- Murxla, later if an API exists

Do not require external fuzzers to be installed for normal tests.

## Failure handling

For every failure:

1. Save original input.
2. Save solver outputs.
3. Run reducer if available.
4. Save minimized instance in `benchmarks/regressions`.
5. Add expected behavior.
6. Create a short bug note.

## Output format

Report:

- seed
- logic
- number of generated instances
- number of crashes
- number of disagreements
- number of timeouts
- path to saved failures
- exact command to reproduce
