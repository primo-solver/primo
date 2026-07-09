---
name: smt-documentation
description: Write code-grounded architecture, algorithm, invariant, representation, paper-traceability, and user documentation for the SMT solver. Use when documentation should explain concrete implementation details such as term storage, ownership, memory management, hash-consing, SAT/theory interfaces, or how papers inform the code.
---

# SMT solver documentation skill

## Goal

Document the solver so that future code changes preserve the architecture, representations, paper-grounded design choices, and invariants.

Use classic Jupyter Book, matching the `llm2smt` `lra` branch documentation setup:

- author normal docs in MyST Markdown
- maintain `docs/_config.yml`, `docs/_toc.yml`, and `docs/index.md`
- use notebooks only for executable experiments and benchmark reports
- keep the documentation buildable with `make docs`
- keep `scripts/build_docs.sh` as the canonical build wrapper

## Documentation types

### Architecture docs

Explain:

- module boundaries
- ownership and lifetime of data structures
- memory management strategy
- value identity versus object identity
- mutability and aliasing rules
- hash-consing or interning strategy
- stable and unstable identifiers
- major invariants
- extension points

Architecture docs must be code-grounded. Before writing or revising an architecture page:

1. read the relevant headers and implementations
2. cite the concrete local types, methods, and files being described
3. describe what is implemented now, not the intended future design
4. include known limitations and unsupported paths

### Algorithm docs

Explain:

- Boolean abstraction
- clausification
- DPLL(T)
- IPASIR-UP/CaDiCaL SAT backend integration
- congruence closure
- model generation
- push/pop
- explanation generation

Algorithm docs must connect mechanisms to representations. For each algorithmic component, include:

- concrete state owned by the component
- input and output types
- invariants before and after each public operation
- how errors, unsupported input, and unknown results are represented
- complexity expectations when they are known or inferable
- where the implementation intentionally diverges from the paper

### User docs

Explain:

- supported SMT-LIB fragment
- build instructions
- run instructions
- options
- examples
- known limitations

### Developer docs

Explain:

- how to add a theory
- how to add an SMT-LIB construct
- how to add a regression test
- how to run fuzzing
- how to run benchmarks
- how to build the Jupyter Book documentation

## Required Representation Sections

When documenting a core subsystem, include a "Representation" section. Answer these questions explicitly when relevant:

- What are the primary C++ types and where are they defined?
- Who owns the memory?
- Are objects moved, copied, interned, or referenced by numeric ids?
- Which ids or references remain stable after insertion?
- What is hash-consed, canonicalized, or deduplicated?
- What equality notion is used: syntactic identity, semantic equality, SAT literal equality, or congruence equality?
- Which operations may allocate new state?
- Which operations are pure queries?
- What invalid states are prevented by constructors or checked dynamically?

For term documentation specifically, cover:

- term node storage
- `TermId` stability
- hash-consing keys
- sort checking
- Boolean simplification
- interaction with symbol declarations
- how applications, equalities, and Boolean connectives are represented

## Paper Traceability

Every page that mentions a paper-backed component must include a "Paper Traceability" or "Sources and Implementation Status" section:

- list the relevant papers or notes
- state which ideas are implemented now
- state which ideas are only planned or deliberately deferred
- map each implemented idea to the local module or type where it appears
- avoid implying that a paper was fully implemented when only a small interface or stub exists

Use cautious wording:

- "Primo currently uses X from paper Y in module Z."
- "Paper Y motivates future work on X; Primo does not yet implement that algorithm."
- "This is an implementation inference from the paper, not a direct claim from the paper."

## Style

Use precise technical prose.
Prefer small examples.
Prefer concrete type and file references over generic descriptions.
Avoid marketing summaries and vague architecture prose.

Every nontrivial algorithm doc should include:

- input
- output
- representation
- invariants
- ownership and lifetime
- failure modes
- one worked example
- sources and implementation status

## Output format

When documenting a feature:

1. identify what changed
2. update the nearest relevant doc
3. read the relevant source files and document the actual representation
4. add or update paper traceability if papers motivate the design
5. add examples if useful
6. check that docs do not claim unsupported features
7. run `make docs` when documentation tooling is available
