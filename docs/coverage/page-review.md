# Page-by-page review, 2 October 2026

This review covers the 84 pages in the current manual at `8c76deb`.
The historical book is a reference for preservation, not a second current manual.
The writer applies corrections and separate read-only reviewers check the pages
and revisions against the [review checklist](../development/documentation.md#writer-and-reviewer).

This record separates technical review from reader acceptance and solver
qualification. It does not renew old benchmark or fuzzing evidence.

## Page coverage

Every baseline page was read in full by an independent reviewer. “Corrected”
means that this review changed the page, including explicit qualifications where
implementation work remains open. “No required edits” means no required repair
was identified within the stated review scope; neither verdict certifies solver
correctness. Finding IDs are summarized below. R-001 covers historical migration
wording corrected by the writer and checked by reviewer I; R-002 is this review's
status update, also checked separately.

The 84-page baseline excludes this newly added review record.

### Introduction

| Page | Review outcome | Findings |
| --- | --- | --- |
| [Primo](../index.md) | No required edits | — |

### Using Primo

| Page | Review outcome | Findings |
| --- | --- | --- |
| [Supported input and capabilities](../capabilities.md) | No required edits | — |
| [Build and run Primo](../guide/getting-started.md) | No required edits | — |
| [SMT-LIB input and results](../guide/input.md) | Corrected | A-009 |
| [Request a model](../guide/models.md) | No required edits | — |
| [Repeated queries and scopes](../guide/sessions.md) | No required edits | — |

### Reference

| Page | Review outcome | Findings |
| --- | --- | --- |
| [Command-line options and output](../reference/options.md) | Corrected | A-001; A-012/A-013 open |
| [Effective policies and option dependencies](../reference/policies.md) | Corrected | A-004, A-010 |
| [Tactic reference](../reference/tactics.md) | Corrected | A-008 |
| [Statistics reference](../reference/statistics.md) | Corrected | A-001, A-003, A-005–A-007, A-011 |
| [SAT backend operations](../reference/backend.md) | Corrected | A-002 |

### Contributing

| Page | Review outcome | Findings |
| --- | --- | --- |
| [Correctness before performance](../development/correctness.md) | Corrected | B-004, B-006, B-010 |
| [Contributing an implementation change](../development/contributing-changes.md) | No required edits | — |
| [Fuzzing and reducing failures](../development/fuzzing.md) | Corrected | B-005, B-009 |
| [Independent model and session validation](../development/validation.md) | Corrected | B-001, B-003 |
| [Debugging and investigating regressions](../development/debugging.md) | No required edits | — |
| [Reproducible experiments](../development/experiments.md) | Corrected | B-002, B-004, B-010; B-012/B-013 open |
| [Tuning an existing configuration](../development/tuning.md) | Corrected | B-014 limitation; tool repair open |
| [Writing and checking documentation](../development/documentation.md) | Corrected | B-007 |

### Understanding Primo

| Page | Review outcome | Findings |
| --- | --- | --- |
| [From commands to an answer](../implementation/architecture.md) | No required edits | — |
| [Terms, symbols, and ownership](../implementation/core.md) | Corrected | C-002 |
| [Frontend: syntax, names, and typed terms](../implementation/frontend.md) | No required edits | — |
| [Sessions: scopes, replay, and retained engines](../implementation/sessions.md) | No required edits | — |
| [Goals, tactics, and strategies](../implementation/tactics.md) | Corrected | C-001, C-003; C-004 open |
| [Preprocessing](../implementation/preprocessing.md) | Corrected | D-001–D-005; D-006 resolved below |
| [Boolean abstraction and SAT integration](../implementation/sat.md) | No required edits | — |
| [DPLL(T): connecting Boolean search and theory reasoning](../implementation/dpllt.md) | No required edits | — |
| [EUF: congruence, explanations, and rollback](../implementation/euf.md) | No required edits | — |
| [Linear real arithmetic](../implementation/lra.md) | Corrected | E-001–E-003 |
| [Linear integer arithmetic](../implementation/lia.md) | No required edits | — |
| [Theory combination](../implementation/combination.md) | Corrected | F-001 |
| [Quantifiers: binders and instantiation](../implementation/quantifiers.md) | Corrected | F-002 |
| [Models: values, function tables, and reconstruction](../implementation/models.md) | Corrected | F-003 |

### Literature

| Page | Review outcome | Findings |
| --- | --- | --- |
| [Literature and implementation mapping](../literature.md) | No required edits | — |
| [Bibliography](../references.md) | No required edits | — |
| [Reported QF_LRA results](../papers/primo-qflra-results.md) | No required edits | — |
| [Bromberger: integer search, cubes and preprocessing](../papers/bromberger-linear-arithmetic.md) | No required edits | — |
| [Integer-model repair and MIP primal heuristics](../papers/lia-primal-heuristics.md) | No required edits | — |
| [Yices: architecture, dynamic Ackermannization, and lemma reuse](../papers/yices.md) | Corrected | G-002 |
| [Clause forgetting: correctness and termination](../papers/clause-forgetting.md) | Corrected | G-001 |
| [Polarity-aware CNF in SMT](../papers/polarity-aware-cnf.md) | No required edits | — |
| [Don't-care reasoning and relevance](../papers/relevance-filtering.md) | Corrected | G-003 |

### Open leads

| Page | Review outcome | Findings |
| --- | --- | --- |
| [Research register](../leads/index.md) | Corrected | H-001; C-004 open |
| [Library API and embedding](../leads/library-api.md) | No required edits | — |
| [Incremental memory and reference counting](../leads/incremental-memory.md) | No required edits | — |
| [Compact term storage and future payloads](../leads/term-storage.md) | No required edits | — |
| [Compact rational storage](../leads/rational-storage.md) | Corrected | H-003 |
| [Statistics instrumentation and build profiles](../leads/statistics-instrumentation.md) | No required edits | — |
| [Dynamic Ackermannization and theory-lemma retention](../leads/theory-lemmas.md) | No required edits | — |
| [Safe forgetting of theory lemmas](../leads/theory-lemma-forgetting.md) | No required edits | — |
| [Floating-point-guided LRA basis selection](../leads/lra-floating-point.md) | Corrected | H-003 |
| [Complete implied-equality basis](../leads/lra-equality-basis.md) | No required edits | — |
| [GCD reasoning with bounded variables](../leads/lia-extended-gcd.md) | Corrected | H-002, H-003 |
| [Configurable and better-guided integer patching](../leads/lia-patching.md) | Corrected | H-003, H-006 |
| [Avoiding infinitesimals in integer relaxation](../leads/lia-infinitesimals.md) | No required edits | — |
| [More selective and cheaper care-pair generation](../leads/care-pair-generation.md) | No required edits | — |
| [Reducing combination obligations with care graphs](../leads/care-guided-combination.md) | No required edits | — |
| [Move QF_UFLRA to the general component driver](../leads/real-combination-migration.md) | No required edits | — |
| [Further model-based combination transitions](../leads/model-based-combination.md) | No required edits | — |
| [Quantifier instantiation efficiency](../leads/quantifier-instantiation.md) | No required edits | — |
| [Optional NNF through quantifiers](../leads/quantifier-nnf.md) | Corrected | H-003 |
| [Exploiting directly asserted quantifiers](../leads/asserted-quantifiers.md) | Corrected | H-003 |
| [Skipping irrelevant quantified obligations](../leads/quantifier-relevance.md) | Corrected | H-003 |
| [Compact handling of large distinct assertions](../leads/compact-distinct.md) | Corrected | H-002, H-004, H-005 |
| [Equation elimination across queries](../leads/incremental-equations.md) | Corrected | H-002, H-004, H-005 |
| [Sharing immutable EUF preparation between checks](../leads/shared-euf-structure.md) | Corrected | H-002, H-005 |

### Active work

| Page | Review outcome | Findings |
| --- | --- | --- |
| [Public launch preparation](../work/publication.md) | Corrected | B-008; D-006 resolved below |
| [GitHub publication handoff](../work/github-handoff.md) | Corrected | B-008; D-006 resolved below |
| [Documentation replacement](../work/documentation.md) | Corrected | B-007 |
| [Evidence work still open](../work/evidence.md) | Corrected | B-011 |
| [Results storage](../work/results-storage.md) | Corrected | B-010 |

### Evidence and history

| Page | Review outcome | Findings |
| --- | --- | --- |
| [Publication qualification](../evidence/publication.md) | Corrected | I-002 |
| [Assignment migration evidence](../evidence/assignment.md) | No required edits | — |
| [Arithmetic engineering and retained evidence](../evidence/arithmetic.md) | No required edits | — |
| [Quantifier decisions and checking evidence](../evidence/quantifiers.md) | No required edits | — |
| [Retained documentation and evidence](../evidence/archive.md) | Corrected | R-001 |
| [Historical documentation review](../review-history.md) | Corrected | R-001 |

### Documentation review

| Page | Review outcome | Findings |
| --- | --- | --- |
| [Documentation closing review](../review.md) | Corrected | R-002; D-006 resolved below |
| [Preservation audit](../coverage/index.md) | Corrected | R-001 |
| [Frontend and session preservation map](../coverage/frontend-sessions.md) | No required edits | — |
| [Solver chapter preservation map](../coverage/solver.md) | No required edits | — |
| [Integer-arithmetic preservation map](../coverage/lia.md) | Corrected | R-001 |
| [Tactics and preprocessing preservation map](../coverage/tactics-preprocessing.md) | Corrected | I-001, I-003 |
| [Guides, practice, literature, and leads](../coverage/practice-literature.md) | Corrected | R-001 |


## Corrections and independent review

Six read-only reviewers covered nine groups; the writer applied the changes.
Each revised group returned to its original reviewer. The following summary
records the required repairs and accepted suggestions, not just broken links.
The source findings and their current dispositions are recorded below.

| Group and reviewer | Findings and disposition |
| --- | --- |
| A — Hilbert; guides and reference | A-001–A-011: correct conflict-budget coverage, backend range handling, counter/default claims, traversal cost, timer boundaries, model examples, cancellation behavior, input navigation and paper/source links. A-012/A-013 remain implementation follow-ups. |
| B — Kierkegaard; contributor guides and active work | B-001–B-011: use SAT-only model-validation inputs, describe analyzer limits, supply retained-session recipes, preserve the maintainer execution policy, qualify seed-size bounds, repair source selections and link the current ledger. B-012–B-014 remain tooling follow-ups with explicit runbook limitations. |
| C — Averroes; core, frontend, sessions and tactics | C-001–C-003: distinguish strategy labels from callable behavior, typed-model ownership from printed text, and correct implementation links. C-004 remains a configuration-identity follow-up. |
| D — Herschel; preprocessing, SAT, DPLL(T) and EUF | D-001–D-005: describe joint bounded Gauss–Jordan elimination, enabled ITE distribution, appended simplification consequences, passive result storage and the correct equality rewrite. D-006 was independently cross-checked by Avicenna; the subsequent repair and qualification are recorded below. |
| E — Avicenna; arithmetic | E-001–E-003: assign phase-refresh tracking to the online propagator and link the editable figure and actual rational fields. No required LIA chapter edits. |
| F — Kuhn; combination, quantifiers and models | F-001–F-003: retain conservative unmapped-pair fallback, distinguish propositional from equality-dependent test coverage, and select the complete model lookup. |
| G — Averroes; literature | G-001–G-003: include the clause-forgetting correctness section and select the actual Ackermann evidence and arithmetic relevance lead. |
| H — Kierkegaard; open leads | H-001–H-006: classify conflict generalization as implemented, repair historical selections, separate contributor machine choice from maintainer policy, restore mandatory fuzz/model gates, link current owners and define the delta-aware floor. |
| I — Kuhn; evidence and preservation | I-001–I-003: describe the completed archive migration, identify the historical reproducibility-tool revisions and link tactic counters to their actual reference. R-001/R-002 preserve the distinction between historical reviews, this technical review and reader/publication acceptance. |

## Verification and limits

The review compared claims with owning headers, implementations, callers,
option defaults and selected test bodies. It checked mathematical examples,
implementation status, paper attribution, source-link destinations and the
separation of architecture, procedures and evidence. Reading a test body is
not evidence that the test passed at this revision.

Primary reading included the relevant EUF and arithmetic mechanisms, theory
combination and quantifier distinctions, the strategy paper, Bromberger's
thesis selections, the MIP heuristic survey sources, Yices, clause-forgetting
conditions, polarity/relevance papers, IPASIR-UP and symmetry preconditions.
The published Primo tables were checked for transcription. This was not a new
proof audit of every citation or every paper section in the bibliography. The
original Plaisted–Greenbaum full text remains unavailable; its literature page
states that limit and identifies the primary modern account actually used.

During the original documentation review, no solver, test suite, fuzzing
campaign or benchmark was run. The subsequent D-006 qualification is recorded
separately below. Existing qualification records were inspected without renewing them; raw experiment
archives were not exhaustively reaggregated. Changed validation recipes were
checked against their tools, not executed as fresh qualification. Historical
pages were checked for preservation and link routing, not rewritten or newly
certified as current implementation descriptions. Repository visibility,
hosted settings, deployment, legal rights and the absence of secrets were not
certified by this documentation review.

`make docs-check-links` passed the full strict build of both books and the
assembled-site check: 255 HTML pages, 30,993 local links, 948 unique Git links
and zero local/Git failures. The assembler retained 1,306 historical targets,
75 redirect pages and 91 fragment routes. Browser checks at the `/primo/`
project prefix covered all 85 current pages, including this new record, and
557 rendered math containers after font loading. They found no unrendered
equations, MathJax errors, page-width overflow or Sphinx error nodes. Selected
changed sections were also inspected visually. This is a desktop check, not
an exhaustive mobile or accessibility audit.

External-host results are a separate dated observation: the current book had
four failed targets and two timeouts; the historical book had thirteen failed
targets and two timeouts. See the
[full external-link report](page-review-external-links.json) for exact URLs,
commands and access errors. These failures remain visible.

## Correctness follow-ups

These findings originated in source inspection. D-006 has since been reproduced,
repaired and qualified on SLURM. The six supporting-tool and resource-control
findings remain open; the documentation review does not repair them.

(symmetry-payloads)=
### D-006: symmetry comparison omits numeric payloads

**Resolved: reproduced on the reviewed solver, repaired and qualified on SLURM.**

The reviewed {source}`symmetry comparison key <src/smt/preprocessing.cpp#L2036-L2067>`
included term kind, sort, symbol, Boolean value and children, but omitted
`rational_value`. Numeric literals and multiplication coefficients use that
payload. Consequently the key can identify different arithmetic expressions,
invalidating the permutation-invariance test used by
{source}`UfSymmetryBreaker <src/smt/preprocessing.cpp#L5197-L5241>`.

The numeric-value regression has distinct uninterpreted values `c0`, `c1`, a
fresh constant `t` restricted to those values, and a function `h` to Real.
Create `c0` before `c1` so that its `TermId` is smaller:

```text
distinct(c0, c1)
t = c0 or t = c1
h(c0) = 0
h(c1) = 1
h(t) = 1
```

There is a model with `t = c1`. Swapping `c0` and `c1` does not preserve these
assertions, but a key that identifies numeric `0` with `1` can accept that
swap. The faulty value-precedence path adds `not(t = c1)` for the
first fixed term `t`, removing the satisfying choice. Two independent source
reviews traced the faulty insertion, including the supported
`--preprocessing-mode=basic --preprocessing-passes=0` path.

The old solver at `09749dd` returned **UNSAT** for this satisfiable input under
both default preprocessing and Basic with zero rounds. A second regression uses
different multiplication coefficients and exposes the same false answer. Z3,
cvc5 and Primo with symmetry breaking disabled returned SAT for both.

| Regression | Old default | Repaired default | Z3 and cvc5 |
| --- | --- | --- | --- |
| {project}`Different numerals <benchmarks/smoke/qf_uflra/sat_symmetry_numeric_values.smt2>` | UNSAT (wrong) | SAT | SAT |
| {project}`Different coefficients <benchmarks/smoke/qf_uflra/sat_symmetry_coefficients.smt2>` | UNSAT (wrong) | SAT | SAT |
| {project}`Equal-payload control <benchmarks/smoke/qf_uflra/sat_symmetry_equal_numeric_values.smt2>` | SAT | SAT | SAT |

The two failures also occur with Basic zero-round preprocessing and value
precedence disabled. The repaired solver returns SAT in all four CLI profiles:
default, Basic zero-round, Basic zero-round without value precedence, and Basic
zero-round without symmetry breaking. Dolmen accepts all twelve resulting
models. Reference comparison is necessary here: model validation alone would
not detect a false UNSAT answer.

The {project}`repair <src/smt/preprocessing.cpp#L2036-L2070>`
preserves the exact rational payload for every node, including
`Rational`, `Times`, `IntDiv` and `IntMod`. The design checklist follows
Déharbe et al., §3, Definitions 2–4, Theorem 5 and Lemma 6
{cite:p}`deharbe2011symmetry`: equality of comparison keys must imply identical
syntax after sort-preserving constant renaming and the permitted commutative
reordering. It must not erase interpreted arithmetic values. General symmetry
detection and a new value-precedence algorithm are outside this repair.

The {project}`regression coverage <tests/unit/preprocessing_tests.cpp#L673-L740>`
checks unequal numeric payloads, signs, fractions, large
values, coefficients and divisors; equal-payload controls must still admit
symmetry. Both restriction modes are covered. The
{project}`three SMT-LIB regressions <tests/unit/boolean_solver_tests.cpp#L1281-L1297>`
exercise default preprocessing, Basic with zero rounds and symmetry disabled:
40 direct-pass calls and 18 solver assertions, including equal-payload controls.

Qualification used the repaired snapshot `a953edd`. All 43 release CTest targets,
216 Python tests, 12 documentation examples and all 43 ASan CTest targets passed.
The fresh differential gate passed 3,000 general QF_UFLRA cases, 500 finite-choice
QF_UF cases and 500 finite-choice QF_UFLRA cases. A separate campaign passed 100
OpFuzz mutations of the three regressions and two real QF_UFLRA seeds. There were
no wrong answers, crashes, parser/type errors, unsupported results, reference
disagreements or timeouts in these gates.

Separate Dolmen replay accepted all 2,011 SAT models from the general and
finite-choice QF_UFLRA gates, 68 SAT mutant models, four SAT seed models, and the
twelve regression/profile models. The finite-choice QF_UF gate compared answers
with Z3 and cvc5; it did not include Dolmen replay. These are bounded samples:
the three generated corpora contain 2,992, 42 and 42 distinct input files,
respectively, and the mutations contain 80 distinct files.

The [qualification summary](symmetry-payload-qualification.json) and
[run ledger](../run-log.json) retain exact revisions, binary hashes, commands,
seeds, counts, resource limits and job IDs. An initial test attempt failed to
locate its fixtures from the CTest working directory; the paths were corrected
and all gates rerun on `a953edd`. That superseded attempt is recorded separately.
This closes D-006, not the six findings below or the remaining publication work.

### Supporting tools and resource controls

| Finding | Source evidence and required follow-up |
| --- | --- |
| A-012: negative `auto` budgets | {source}`parse_auto_budget <src/main.cpp#L128-L152>` uses unsigned conversion without rejecting a minus sign. Check decimal spelling and add CLI regressions; retain the documented nonnegative contract. |
| A-013: incomplete conflict-budget enforcement | Pure LRA/LIA and dedicated mixed routes do not all supply the budget to {source}`SolveTerminator <src/smt/solver_runtime.hpp#L14-L28>`. Decide and implement the intended route coverage, then test it. The reference now states the current limits. |
| B-012: analyzer binding scope | {source}`inline_lets <scripts/analyze_smt2.py#L105-L169>` can expand parallel bindings sequentially and mishandle shadowing. Repair scope-aware expansion or reject those shapes before using its output as structural evidence. |
| B-013: composition model-validation selection | {source}`run_model_validation <scripts/tactic_composition_matrix.py#L354-L389>` sends all eligible cases to an adapter that requests a model. Select successful SAT rows, require unchanged SAT answers and join their validation coverage. |
| B-014: tuning exit-status handling | Repaired in the [irace](https://github.com/primo-solver/primo/blob/a19493701d7ab31931d7466fd88a44803f41abf3/scripts/irace/target_runner.py) and [SMAC](https://github.com/primo-solver/primo/blob/a19493701d7ab31931d7466fd88a44803f41abf3/scripts/smac/qfuf_gap_tune.py) wrappers: timeout and abnormal exits override printed answers with PAR2. [Mocked-process regressions](https://github.com/primo-solver/primo/blob/a19493701d7ab31931d7466fd88a44803f41abf3/tests/python/test_tuning_process_status.py) cover both answers, nonzero exits, signals, post-answer timeout and normal wrong answers; outputs and exit codes are retained. Publication closure remains pending the required SLURM qualification. |
| C-004: numeric probe-name collisions | {source}`at_most <src/smt/strategy.cpp#L106-L113>` uses default stream precision in names. Distinct thresholds can have the same canonical strategy description. The [probe lead](../leads/index.md#reusable-tactic-probes) records the representation and test obligation. |

These are separate from an external URL that cannot be reached. The
[external-link report](page-review-external-links.json) records certificate,
access-denial, rate-limit and timeout failures without suppressing them.
