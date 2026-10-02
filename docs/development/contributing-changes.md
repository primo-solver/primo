# Contributing an implementation change

Read the {download}`MIT/DCO contribution policy <../../CONTRIBUTING.md>`,
then begin with the affected contract and the [correctness requirements](correctness.md).
Primo is implemented from primary literature and its own design notes. Existing
solver source, tests, grammar and documentation are not implementation material.

## Add syntax or a theory

A new SMT-LIB construct needs parser/category checks, type checking, scoped
name behavior, conversion into manager-owned terms, supported-fragment updates,
and positive/negative examples. Exercise definitions, quoted names, nesting,
scope replay and models where applicable. Keep parser syntax out of SAT/theory
interfaces and update the [capability matrix](../capabilities.md).

A new theory needs an explicit supported fragment, owned state, asserted-literal
interface, explained conflicts/propagations and model export. The
[combination chapter](../implementation/combination.md) describes the component
boundary and combination conditions. A general interface does not establish
the completeness of a newly connected theory. Read the relevant primary
algorithm, map operations and invariants to Primo types, and document the
implemented subset before claiming the paper's mechanism.

## Tests and evidence

Add original tests for the new invariant or paper-specific fast path. Cover
negative admission, resource limits, backtracking, ownership and reconstruction
where the change can affect them. Add counters that distinguish the intended
mechanism from fallback behavior, then use [experiments](experiments.md) to
decide whether an optimization is accepted or rejected.

Run the repository's formatting, lint, unit/Python tests, memory checks and
static review as applicable. Solver behavior changes additionally require
[fuzzing](fuzzing.md); model-affecting work requires
[independent validation](validation.md). A passing build is not a fuzz gate.

## Review and commit

Review definitions, callers, invalidation boundaries and limitations together.
Remove dead helpers and misleading comments, keep layer interfaces narrow, and
update the nearest documentation in the same coherent milestone. Preserve
dated results separately from current behavior. Commit verified milestones;
do not leave required source, tests, scripts or metadata untracked.

Documentation changes follow the [writing and build conventions](documentation.md).
Reader acceptance and technical verification are separate review states.
