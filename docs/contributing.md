# Contributing to Primo: draft

**Status: proposed public contribution guide, 2026-09-16.** Review this guide
with [](public-repository-migration.md) before adopting it. Apache-2.0 is the
provisional preference for the public release, with MIT still under
consideration. Contribution licensing and any DCO requirement are undecided;
this draft does not obtain consent or change the repository's license.

## What we welcome

Primo is a community research solver built around correctness, understandable
implementations, and reproducible evidence. Contributions include documentation
corrections, small examples, minimized bug reports, tests, tooling, and solver
development. Documentation improvements are valuable contributions in their
own right.

For a substantial algorithm, new dependency, interface change, or change to
default behavior, discuss the problem and proposed design before committing to
a large implementation. Small fixes can go directly to a pull request. Use
the project's issue tracker once the public repository is established.

## Documentation is part of the change

Primo follows a documentation-as-code policy: documentation lives with the source,
uses the same review process, and ships with the behavior it describes. A pull
request changing behavior or an architectural contract is incomplete until its
affected documentation is updated. If no documentation change is needed, say
why in the pull request; small internal changes need not produce a new page.

Use the nearest existing page under `docs/`. Keep the README as an entry point
and [](current-status.md) as the capability map; link detailed explanations
instead of copying them into several places. Add new pages to `docs/_toc.yml`
and use the existing MyST conventions in [](index.md).

Maintain the three documentation states alongside the change:

- **Done:** update [](current-status.md) and the relevant reference page with the
  implemented scope, limitations, accepted revision, and verification evidence.
  Code awaiting required checks remains work in progress.
- **Being worked on:** update [](active-work.md) with an owner (or explicitly
  unassigned), date, branch/PR or design link, next step, blockers, and remaining
  acceptance criteria. List completed subsets without calling the whole task done.
- **Open leads:** use [](open-leads.md) for proposals and follow-ups, with their
  motivation, evidence, prerequisites, and a concrete next investigation or
  falsifier. An attractive idea alone is not active work.

Update the affected indexes when work changes state. Preserve design and experiment
history with dated outcomes, including rejected work; link to it instead of
copying it into each index. These engineering requirements already apply;
the public contribution licensing and governance choices remain draft proposals.

| Change | Documentation expected |
| --- | --- |
| Accepted input, command, CLI option, or default | Meaning, default, example, errors/unsupported cases, and affected logic or mode restrictions. |
| Representation or interface | Owning C++ types, lifetime, stable IDs versus invalidatable references, allocation points, and invariants. |
| Algorithm | Source paper/section or original design note, implemented subset, operations, proof obligations, deviations, and limitations. |
| Correctness repair | Violated invariant, why the repair restores it, and the regression that exercises the failure. |
| Performance claim | Exact revisions/options, corpus and resource limits, correctness evidence, measured results, and incomplete cases. |
| Build or dependency | Prerequisites, supported setup, reproducible commands, and applicable third-party notices. |

Scale detail to the change. A short addition to an existing explanation is
often enough. Comments should explain non-obvious invariants and reasons;
reference documentation should explain the public or cross-component contract.

Before requesting review:

1. Check that examples and supported-feature claims match the implementation.
2. Distinguish implemented behavior, opt-in experiments, and proposed work.
3. Run `make docs` with strict checking enabled. Fix warnings; do not use
   `PRIMO_DOCS_STRICT=0` as merge evidence.
4. Run behavioral examples against the candidate when their meaning changes.
   Add a regression fixture when the example protects a meaningful contract.
   The book build checks rendering and references, not solver correctness, and
   currently does not execute notebooks.
5. Ask reviewers to assess the explanation and limitations as well as the code.

Keep documentation source in MyST Markdown. Use notebooks for executable
experiments and reports. Do not commit rendered HTML or downloaded papers;
link bibliographic sources and describe the implementation in original words.

## Original implementation and algorithm design

Do not copy code, tests, parser grammars, build files, comments, or documentation
from other SMT solvers or Primo's predecessor, LLM2SMT. Reference solvers are
useful behavioral oracles, not implementation sources. Integrating an approved
external dependency is a separate, explicit decision with its own provenance
and licensing review.

For a nontrivial algorithm, read the relevant primary source and write a local
design note before implementation. Identify data structures, operations,
invariants, complexity expectations, and proof obligations. Map these to Primo
types and modules, then test the distinguishing mechanism. If a required paper
cannot be accessed, report that gap rather than guessing its contents.

A baseline or partial implementation must be described as such. A citation
alone does not establish that the paper's algorithm is implemented.

Preserve the boundaries described in [](architecture.md). Create terms through
the term manager; keep SAT backend details in `sat/` and theory-specific
reasoning in `theory/`. Review every borrow/mutation/use sequence: snapshot
needed IDs before allocating operations and reacquire references afterwards.

## Testing and evidence

Use C++20 and the repository's `clang-format` configuration. Python tooling
should use the standard library unless a dependency is documented. Keep changes
focused enough to explain and review.

The existing local entry points are:

```bash
make build
make check
make docs
```

`make check` includes lint and tests. Memory-sensitive changes also require
`make asan-test`. Read the relevant workflow under `skills/` and the repository
`AGENTS.md` for allocation-boundary testing, review, static analysis, and the
applicable fuzz gate. Those files currently define the detailed engineering
checks; this draft does not waive them.

Documentation-only prose edits normally need the strict documentation build
and factual review. Solver, parser, preprocessing, SAT, theory, model, and
behavior-changing option work needs fresh differential evidence appropriate to
its risk. Benchmark comparisons also need separate correctness/model checking;
model validation time must not enter the timing comparison.

Record substantial experiments in `docs/run-log.json`: exact candidate and
reference commands, revisions, generator/seed/count, input hashes where
applicable, host or job IDs, resource limits, outcomes, and artifact locations.
Separate passes from timeouts, unsupported cases, crashes, wrong answers, and
reference disagreements. See [](experiments.md) and [](run-log.md).

The agreed replacement in [](benchmark-record-storage.md) stores separate run
directories in `primo-solver/results`, submitted through pull requests. SQLite
is an untracked query index rebuilt from an exact results commit. Large artifacts
remain separate, with hashes and locations in Git. Contributors can collect
offline without database accounts; record-format checks do not certify solver
answers. Continue using the current ledger until repository setup, tooling, and
the policy cutover are complete; the new workflow is not yet implemented.

If you cannot run an expensive or infrastructure-specific check, state what
you ran and what remains. A maintainer should arrange the remaining work and
attach evidence before merge. Lack of cluster access should not prevent a
contribution; it does not turn an unrun correctness gate into a pass.

## Review and responsibility

A pull request should explain the problem, resulting behavior, design rationale,
affected documentation, tests/evidence, and remaining limitations. Cite a
reproducer for a bug and the design note for a new algorithm. Keep unresolved
review findings visible.

AI-assisted contributions are welcome under the proposed policy. The submitting
human remains responsible for understanding the change, its provenance,
correctness, tests, and documentation. Identify substantial generated code or
reasoning in the pull request and explain how it was checked. A generated
explanation is not proof that an invariant holds. Never put credentials or
confidential benchmark inputs into public reports or tool transcripts.

The proposed contribution-rights model is that contributors retain their rights
and authorize contributions under the selected project license. Any required
employer or university authorization must be resolved. Whether to require a
Developer Certificate of Origin sign-off remains a migration decision; no
additional agreement is imposed by this draft.

## Reporting problems

For a public bug report, provide a minimal redistributable input, Primo revision,
complete options, expected and actual results, and available reproduction
commands. For performance reports, include limits and comparable reference
commands. A disagreement is evidence to investigate, not by itself proof of
which solver is wrong.

Wrong answers, invalid models, and crashes take priority over performance
tuning. If an input is confidential, describe the symptom without posting the
input; use the private reporting channel once it has been established. The
public migration must name that channel before directing users to it.
