---
name: smt-code-review
description: Review SMT solver code and diffs before commit for ordinary C++ hygiene, unused includes/helpers, missing or misleading comments, lint readiness, architectural violations, solver unsoundness, bad explanations, and insufficient tests. Use after implementation work, before committing solver/parser/preprocessing/build/docs changes, and whenever the user asks for review.
---

# SMT solver code review skill

When reviewing code, prioritize correctness over style, but always do an
old-fashioned hygiene pass. The goal is to catch the small problems that make
later solver work unpleasant: spurious includes, dead helpers, unclear local
invariants, broad APIs, missing comments around non-obvious algorithms, stale
docs, and lint gaps.

## Required commands

Run the commands that apply to the change before committing:

```bash
git diff --check
make lint
make test
make docs
```

If `clang-tidy` is available and the change touched C++, run it on the changed
non-generated `.cpp` files with the current build directory:

```bash
clang-tidy -p build <changed-source.cpp>
```

If a command is not available or not applicable, say why in the final response.

## Review order

1. Soundness
2. Invariant preservation
3. Layering violations
4. Incrementality and backtracking correctness
5. Explanation correctness
6. Model construction correctness
7. Test coverage
8. Old-fashioned C++ hygiene
9. Performance risks
10. Code clarity

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

### Old-fashioned C++ hygiene

Check every changed non-generated C++ file:

- includes are necessary and self-contained; remove spurious headers
- no unused local helpers, variables, aliases, structs, or dead branches remain
- helper names state the local invariant or effect clearly
- public headers expose only what callers need
- implementation-only types stay in `.cpp` anonymous namespaces
- comments explain why and invariants, not what obvious code does
- non-obvious algorithms have a short comment naming the source paper/design
- error messages identify the unsupported input or violated invariant
- code follows existing ownership, lifetime, and exception style
- tests do not hide behavior behind overbuilt fixtures
- docs mention experimental/default-off status for risky options

Prefer small cleanup patches immediately after finding hygiene issues. Do not
leave known trivial cleanup for a future agent.

## Pre-commit gate

Before committing implementation work, perform a self-review using this skill.
The final response should mention either:

- "code review pass: no blocking issues found"; or
- the concrete issues fixed or intentionally deferred.

## Output format

Use this format:

1. Summary
2. Blocking issues
3. Soundness risks
4. Hygiene issues
5. Missing tests
6. Suggested patch plan
7. Non-blocking cleanup

Do not rewrite large files unless explicitly asked.
