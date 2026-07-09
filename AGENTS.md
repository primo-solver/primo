# Primo Agent Guide

## Clean-room policy

Primo is a new solver, not a continuation of LLM2SMT.

The old repository may be used only for:

- understanding previous design decisions
- identifying missing tests
- comparing behavior
- extracting benchmark ideas
- writing retrospectives
- checking which development practices failed or succeeded

Do not copy source code, generated code, parser grammars, build files, comments, tests, or documentation text from LLM2SMT or from other SMT solvers.

When implementing an algorithm, use:

- the cited paper
- our own design notes
- newly written tests
- newly written code

If a design choice is inspired by LLM2SMT, document it as a retrospective note, not as an implementation source.

## Paper-grounded implementation rule

This solver is implemented from papers and original design notes.

Do not copy code from existing SMT solvers.

If a required paper is hard to access or download, stop and tell the user which paper is needed. Do not substitute a blog post, secondary summary, or guessed details for the unavailable paper.

Every nontrivial algorithmic component must cite the paper, book chapter, or design note it is based on.

For each such component, include:

- bibliographic reference
- section or theorem used, where applicable
- implemented subset
- deviations from the paper
- known limitations
- tests derived from the algorithm

Code comments should identify algorithmic basis without overclaiming.

## Documentation synchronization rule

Documentation is part of the implementation, not a follow-up task.

Do not consider a solver change complete unless the relevant documentation is up to date. For changes to parser, terms, symbols, SAT integration, clausification, theory solving, options, build behavior, or solver control flow:

- update the nearest page under `docs/`
- describe the implemented C++ types, ownership, lifetime, invariants, and limitations
- explain whether paper-backed ideas are implemented, partially implemented, or only planned
- keep examples and supported-fragment claims synchronized with the code
- run `make docs`
- commit documentation together with the implementation or in an adjacent commit

If documentation cannot be updated in the same task, state that as a blocker in the final response.

## Architectural rules

Preserve these boundaries:

- `smtlib/` parses, prints, and type-checks SMT-LIB.
- `core/` owns sorts, symbols, terms, rewriting, substitutions, and models.
- `sat/` owns propositional variables, literals, clauses, assumptions, and the IPASIR-UP backend adapter.
- `smt/` owns Boolean abstraction, clausification, assertion stacks, and DPLL(T).
- `theory/` owns theory solvers and theory-specific explanations.
- `scripts/` contains reproducible command-line workflows.
- `benchmarks/` contains only input instances and benchmark metadata.
- `docs/` contains the MyST/Jupyter Book documentation site, design notes, literature summaries, and optional experiment notebooks.

Do not introduce cross-layer shortcuts.

In particular:

- SMT-LIB AST nodes must not be used by the SAT or theory solvers.
- SAT literals must not directly contain terms.
- Theory solvers must communicate via theory literals and explanations.
- All terms must be created through the term manager.

## Coding practices

- Use C++20.
- Keep C++ source formatted with `clang-format` using the repository `.clang-format`.
- Keep Python scripts runnable with the standard library unless a dependency is documented.
- C++ formatting is `clang-format` with the repository `.clang-format`, currently based on Google style with C++20 and a 100-column limit. Do not hand-format around it.
- C++ warnings are intentionally strict for owned code. The default CMake build treats warnings as errors; generated ANTLR sources and external headers are isolated from that policy.
- Use `make asan-test` when touching ownership, parser, term storage, SAT backend, or other memory-sensitive C++ paths.
- Run `make lint` before committing implementation changes.
- Run `make check` before committing behavior changes when feasible.
- Run the `smt-code-review` skill as a pre-commit self-review for implementation changes. At this stage, review all tracked non-generated project files, not only the diff. The review must include ordinary hygiene: unused includes, dead helpers, missing or misleading comments, narrow API surface, formatting, and lint readiness.
- If `clang-tidy` is available, run it on all tracked non-generated `.cpp` files while the project is establishing its baseline. Also run `misc-include-cleaner` as an advisory scan and inspect it for real unused includes; paired `.cpp`/`.hpp` self-includes are not automatically removable. Later, after the whole-tree baseline is clean, diff-focused clang-tidy plus periodic whole-tree sweeps is acceptable. If clang-tidy is not run, state why.
- Prefer small, coherent commits after each working milestone.

## Core invariants

### Term invariant

- Terms are hash-consed.
- Term equality is by `TermId`.
- Every term has an explicit sort.
- Boolean formulas are terms of sort `Bool`.
- Applications are represented uniformly.
- All terms are created through the term manager.

### SAT invariant

- Primo does not implement its own SAT solver.
- SAT solving is delegated through IPASIR-UP, initially backed by CaDiCaL.
- The SAT backend sees only clauses over SAT literals.
- SMT atoms are mapped to SAT variables by the SMT layer.
- The SAT backend must not inspect theory terms.
- Assumptions are distinct from permanent clauses.
- Backend-specific details must stay behind the SAT adapter.

### Theory invariant

A theory solver receives asserted theory literals.

It may return:

- consistent
- conflict clause
- propagated literal with explanation
- model fragment

### DPLL(T) invariant

- The first implementation may use lazy theory checking.
- Theory propagation is optional until the EUF checker is correct.
- Every generated theory lemma must be independently checkable by tests.
