---
name: smt-literature-survey
description: Survey SMT, SAT, DPLL(T), congruence closure, arithmetic, model construction, and solver-engineering papers for implementation-relevant ideas.
---

# SMT literature survey skill

## Goal

Extract implementation-relevant information from papers.

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

## Rules

Do not claim to have read a paper unless the paper text or notes are available.
When uncertain, mark the claim as uncertain.
Prefer primary sources over blog posts.
If a required paper cannot be downloaded or accessed, ask the user to provide it instead of relying on secondary summaries.

Separate:

- what the paper says
- what we infer for Primo
