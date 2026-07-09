---
name: smt-literature-survey
description: Survey SMT, SAT, DPLL(T), congruence closure, arithmetic, model construction, and solver-engineering papers for implementation-relevant ideas, and maintain paper-to-code traceability for which ideas Primo has implemented, deferred, or only used as motivation.
---

# SMT literature survey skill

## Goal

Extract implementation-relevant information from papers.
Track how each paper has been used in Primo so documentation does not blur implemented code, planned design, and general motivation.

## Survey output format

For each paper, report:

1. Citation
2. Problem addressed
3. Core algorithmic idea
4. Required data structures
5. Important invariants
6. Complexity claims
7. Engineering tricks
8. What we should implement
9. What we should not implement yet
10. Relation to Primo's architecture
11. Current Primo implementation status
12. Local files/types affected or expected to be affected
13. Open documentation gaps

## Solver implementation focus

Prefer details about:

- DPLL(T)
- SAT backend APIs
- IPASIR-UP
- CaDiCaL integration
- congruence closure
- explanation-producing union-find
- Nelson-Oppen
- Simplex for LRA/LIA
- branch-and-bound
- cutting planes
- model construction
- SMT-LIB compliance
- proof logging
- incremental solving

## Literature notes location

Write summaries to `docs/literature.md`.
For detailed per-paper notes, write or update `docs/papers/<paper-name>.md`.

## Paper-to-code traceability

For each paper-backed idea, maintain a compact traceability entry:

- `Implemented`: idea is present in code; cite local files, types, and functions
- `Partially implemented`: interface or subset exists; state the missing parts
- `Planned`: design is intended but no implementation exists
- `Deferred`: explicitly out of scope for the current scaffold
- `Not adopted`: considered but rejected; explain why

When source code exists, read it before writing the traceability entry. Do not infer implementation status from file names or TODOs alone.

Use examples like:

- "IPASIR-UP interface shape: Implemented in `src/sat/ipasir_up.hpp`; CaDiCaL is the initial backend in `src/sat/cadical_backend.*`."
- "Explanation-producing congruence closure: Planned; current equality code does not yet maintain proof forests or minimal explanations."

## Representation extraction

When a paper suggests data structures, connect them to Primo's concrete representation:

- local ownership model
- memory layout or storage container
- id/reference stability
- hash-consing or canonicalization
- incremental versus rebuild strategy
- explanation/proof data retained or omitted
- complexity tradeoffs caused by the chosen representation

## Rules

Do not claim to have read a paper unless the paper text or notes are available.
When uncertain, mark the claim as uncertain.
Prefer primary sources over blog posts.
If a required paper cannot be downloaded or accessed, ask the user to provide it instead of relying on secondary summaries.
If the user says they can provide a paper, report access failures quickly and identify the exact missing paper.

Separate:

- what the paper says
- what we infer for Primo
- what Primo currently implements
- what remains documentation-only or planned
