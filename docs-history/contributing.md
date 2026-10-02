# Contributing to Primo: draft

**Status: proposed public contribution guide, updated 2026-09-24.** Review this
guide with [](public-repository-migration.md) before adopting it. Primo retains
its existing MIT license, as decided on 2026-09-23. Publication-rights checks and
any DCO requirement remain pending; this draft does not obtain contributor consent.

## What we welcome

Primo is a community research solver built around correctness, understandable
implementations, and reproducible evidence. Contributions include documentation
corrections, small examples, minimized bug reports, tests, tooling, and solver
development. Documentation improvements are valuable contributions in their
own right.

Correctness comes before performance. Solver behavior changes require fresh
fuzzing evidence before they are complete. Wrong answers, invalid models, and
crashes block acceptance; a speedup cannot compensate for them. The correctness
requirements below already apply, independently of the draft public policy.

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
the public sign-off and governance choices remain draft proposals.

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

The existing entry points are:

```bash
make build
make check
make docs
```

On the maintainer's workstation, local builds for interactive debugging are
allowed, but all solver batches, including test suites and fuzzing, run on the
remote cluster through SLURM. Run `make check` and `make asan-test` inside an
allocation. See `AGENTS.md` for the execution boundary and
`scripts/slurm_prepare_benchmark_workspace.py` for remote preparation.

`make check` includes lint, C++ tests, and Python tests; it does **not** run
fuzzing. Memory-sensitive changes also require
`make asan-test`. Read the relevant workflow under `skills/` and the repository
`AGENTS.md` for allocation-boundary testing, review, static analysis, and the
applicable fuzz gate. Those files currently define the detailed engineering
checks; this draft does not waive them.

Documentation-only prose edits normally need the strict documentation build
and factual review. Solver, parser, preprocessing, SAT, theory, model, and
behavior-changing option work needs the fresh fuzzing evidence described below.
Full evaluations and default promotions also require separate independent SAT
model validation with Dolmen; agreement on `sat` does not validate the printed
model. See [model validation](debugging.md#model-validation) and
[](experiments.md). Model validation time must not enter the timing comparison.

### Required fuzzing before merge

Run a fresh gate against the current candidate commit and the options being
changed. This applies to parsing, printing, type checking, term construction,
rewriting, preprocessing, clausification, SAT integration, solver control flow,
theory solving, generated lemmas, models, and options that alter behavior.
Rerun the applicable gate after a behavior-changing fix; evidence for an older
implementation does not qualify the new one.

| Gate | When required | Minimum coverage |
| --- | --- | --- |
| Smoke | Narrow parser/printer/core changes with low solver impact | 100 generated or mutated inputs, plus focused unit tests. Insufficient for theory, SAT, preprocessing, or default-profile changes. |
| Merge | Ordinary solver, theory, preprocessing, or SAT behavior changes | 1,000 generated or mutated inputs compared against a trusted reference. |
| Campaign | Promoting a profile, propagation mode, preprocessing transformation, theory engine, or SAT interaction to default/headline status | 3,000 generated QF_UF cases for QF_UF work, or comparable coverage for other logics, plus mutation of representative real benchmark seeds when available. |

Choose generators and seeds that exercise the changed logic, mechanism, and
options. Case counts alone do not establish coverage. Use the
[fuzzing and reduction workflow](debugging.md#fuzzing-and-reduction) for the
available generators, mutation tools, and logic-specific gates. Solver batches
run on SLURM, with maintainer help when needed.

For a QF_UF differential merge run, prepare the remote dependencies and invoke
the wrapper with an explicit count and seed from the repository root inside
a SLURM allocation:

```bash
make build contrib-fuzzsmt contrib-z3
PATH="$PWD/contrib/local/bin:$PATH" python3 scripts/fuzz_external.py \
  --fuzzer fuzzsmt --solver './build/primo --quiet' --refs z3 \
  --logic QF_UF --count 1000 --seed 20260924 --timeout 5 \
  --fuzzer-args '-ms 1 -Ms 2 -mv 1 -Mv 2 -mf 1 -mp 1 -ma 1 -Ma 2' \
  --out fuzz-results/contribution-qf-uf-merge
```

Use a new output directory for each run, and adapt the formula sizes and shapes
to the change. This example checks QF_UF answers; it does not cover other logics
or validate printed models. The current `make fuzzsmt` convenience target runs
only 20 cases, below even the smoke minimum. `make fuzz` is also a short
entry point, not a complete campaign.

Inspect the generated `summary.csv` and actual case count, not only the exit
status. The wrapper can exit successfully after skipping a missing fuzzer.
Skipped runs, unavailable references, and unexecuted cases are not passes.
Install missing tools when feasible; otherwise record the failed command and
the gate that remains incomplete.

### Triage failures and preserve evidence

Stop performance tuning on wrong answers, crashes, invalid models, rejection
of valid generated input, or untriaged reference disagreements. Preserve the
input and outputs, reproduce with the candidate and references, minimize the
failure, and add a regression for a confirmed bug. Fix correctness defects
before accepting the change. A disagreement needs investigation; the reference
solver is not automatically right. Document a demonstrated unsupported case
precisely rather than counting it as a pass.

Report timeouts and `unknown` separately from passes. Timeouts alone are not
correctness failures, but they reduce conclusive coverage. Rerun and classify
saved failures before using an old `bugs/` directory as evidence.

Record every fuzz gate and other substantial experiments in `docs/run-log.json`:
exact candidate and reference commands, revisions, generator/seed/count, input
hashes where applicable, host or job IDs, resource limits, outcomes, and artifact
locations. Separate passes from timeouts, unknown results, unsupported cases,
crashes, wrong answers, parser/type errors, invalid models, and reference
disagreements.
Preserve failure inputs and logs in the recorded artifact directory. Link the
run-log entry and summarize the gate, coverage, outcomes, and remaining gaps
in the pull request. A raw CSV without a run-log entry does not qualify a
solver change. See [](experiments.md) and [](run-log.md).

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
and authorize contributions under MIT. Any required
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
