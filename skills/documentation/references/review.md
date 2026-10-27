# Independent documentation review

Review the assigned material read-only. Read the whole affected section and
the nearby definitions it relies on, not just the diff. Consult owners and
callers in Primo; do not copy other solver implementations. Do not run solver
batches as part of a prose review. A build checks rendering, not correctness
or whether the explanation is understandable.

## Source and link pass

- Link concrete implementation references through the configured MyST
  `{source}` role. A filename in backticks is not a navigable source reference.
  Prefer the owning definition or operation; inspect the cited revision and
  line range, not just whether the URL exists. Repeated identifiers do not all
  need another link when their owner is already clear in that section.
- Check definitions and callers against claims about execution order,
  ownership, lifetime, defaults, supported inputs and result conditions.
  Qualify scaffolds and optional experiments; a callable interface does not
  establish production use.
- Check paper attribution and the implemented subset against primary sources
  for new algorithmic claims. Record inaccessible evidence as unchecked.
- Verify that an inventory is complete within its stated scope. Distinguish
  mandatory phases, optional tactics, internal helpers and public builders.

## Reader and abstraction pass

State what question each section answers, then assess its paragraphs against
that purpose:

| Section purpose | Appropriate detail | Usually belongs elsewhere |
| --- | --- | --- |
| Architecture | Responsibilities, data flow, ownership, interfaces and invariants | Syntax edge cases, exhaustive option tables, measured revisions and job IDs |
| Algorithm | Inputs, operations, conditions, examples and why the result follows | Unintroduced APIs, benchmark chronology interrupting a derivation |
| Reference | Exact signatures, parameters, defaults and supported combinations | Long tutorial arguments or migration history |
| Evidence | Revision, configuration, method, result and limitations | Claims of current capability inferred from old measurements |

Concrete code links belong at every level. A short example or implementation
detail is appropriate when it explains that section's contract; do not ban
details solely because a section is architectural. Flag unexplained changes
of purpose and suggest a precise destination and link for moved material.

Check first-use definitions, standard terminology, notation, and examples that
actually explain the operation. Flag vague actors, unexplained nouns, long
status bullets, repeated disclaimers, and prose whose generic wording conceals
a missing mechanism. Require the smallest repair that makes the meaning clear;
separate optional stylistic preferences from defects. Keep implemented behavior,
active work and research leads distinct.

## Terminology pass

Review vocabulary explicitly, including headings, tables, captions, and link
labels. Do not assume that a name is standard because it appears in code or
earlier documentation.

- Distinguish mathematical concepts, established solver terminology, and local
  implementation names. Prefer the established term in prose; keep exact C++
  identifiers in linked implementation references.
- Flag project-specific nouns that require readers to guess the actor or
  operation. For example, explain `TheoryComponent` as the theory solver
  interface instead of introducing an unexplained “component.” Check uses of
  “kernel,” “lowering,” “accumulator,” and similar labels in context; this is
  not a forbidden-word list.
- Allow established architectural conventions when their meaning is stated.
  Calling quantifier reasoning a theory solver can describe its interface role
  without claiming that quantifiers form a separate background theory or that
  the procedure is complete.
- Check that one concept keeps the same name across adjacent chapters and that
  distinct concepts do not silently share a label. In particular, distinguish
  a model choice from an entailed equality and a theory from its implementation.
- When standardness is uncertain and affects meaning, consult the cited primary
  literature or report the uncertainty. Do not invent a replacement term.

Treat terminology that changes meaning or leaves an essential operation or
actor unexplained as a required finding. A harmless, clearly defined synonym
is at most a suggestion. Give the conventional alternative or the missing
definition and explain why the repair helps; do not merely label prose “LLM
speech.” Include the terminology assessment in the review verdict, even when
no findings remain.

## Findings and re-review

For each finding, return:

- stable ID, file/heading/line, severity, and **required** or **suggestion**;
- the claim or passage, its effect on the reader, and supporting evidence;
- the smallest correction, including the destination for relocated detail.

Required findings include false/unsupported claims, missing essential
explanations, missing source navigation, and abstraction shifts that obstruct
the section's purpose. Use `high` for misleading behavior/ownership claims,
`medium` for explanatory gaps, and `low` for local link/wording defects; severity
does not replace the required/suggestion distinction.

End with `changes requested`, `ready`, or `blocked on evidence`, the inspected
scope and revision, and any unverified claims. On re-review, mark each ID
resolved or open, check the revised context for regressions, and identify the
final patch being accepted. Do not approve on the writer's assurances alone.
