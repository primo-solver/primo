---
name: smt-code-review
description: Review SMT solver code for architectural violations, solver unsoundness, missing invariants, bad explanations, and insufficient tests.
---

# SMT solver code review skill

When reviewing code, prioritize correctness over style.

## Review order

1. Soundness
2. Invariant preservation
3. Layering violations
4. Incrementality and backtracking correctness
5. Explanation correctness
6. Model construction correctness
7. Test coverage
8. Performance risks
9. Code clarity

## Things to look for

### Term layer

Check:

- all terms are created through the term manager
- sorts are checked at construction
- structurally equal terms share the same `TermId`
- rewrite rules preserve sorts
- pretty-printing round-trips through the parser where expected

### SAT layer

Check:

- clauses contain only SAT literals
- SAT solving goes through the IPASIR-UP adapter
- CaDiCaL-specific calls are isolated in the backend module
- permanent clauses and assumptions are passed to the backend with correct lifetimes
- learned or theory clauses sent to the backend are not tautological unless intentionally allowed
- assumptions are handled separately from permanent clauses
- model values are queried only after SAT result is SAT

### SMT layer

Check:

- Boolean abstraction is total and deterministic
- each SMT atom has exactly one SAT variable
- polarity is handled correctly
- assertion stack push/pop restores all relevant state
- generated theory lemmas are valid

### Theory layer

Check:

- theory literals are asserted incrementally in trail order
- backtracking restores all theory state
- conflicts include enough explanation literals
- explanations are implied by the asserted literals
- disequality conflicts in EUF are explained by equality paths

### Model generation

Check:

- every declared constant has an interpretation
- every declared function has default values
- values respect equivalence classes
- model validation script accepts produced models where possible

## Output format

Use this format:

1. Summary
2. Blocking issues
3. Soundness risks
4. Missing tests
5. Suggested patch plan
6. Non-blocking cleanup

Do not rewrite large files unless explicitly asked.
