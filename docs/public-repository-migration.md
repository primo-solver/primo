# Public repository migration: draft

**Status: discussion draft, 2026-09-16.** The intended destination is a public
repository in a separate organization. Apache-2.0 is preferred provisionally;
MIT remains an alternative. No license selection, university authorization,
repository transfer, or publication is recorded by this document.

## Purpose

Make Primo a community research project that people outside the founding group
can build, understand, evaluate, and contribute to. Documentation, original
paper-grounded implementation, and reproducible correctness evidence are part
of the project's contribution standard.

The first public release should describe the supported solver accurately and
provide a reproducible starting point. New theory support and performance
improvements are separate development work, not prerequisites invented for the
migration.

## Decisions to settle

| Decision | Working proposal | What remains open |
| --- | --- | --- |
| License for Primo-owned code | Apache-2.0 | MIT is also under consideration; establish authority to license before selection. |
| Institutional rights | Resolve with the university before release | Employment, funding, contributor rights, and authority for any patent grant. |
| Public history | Preserve history if the publication audit permits | Transfer the repository or publish a curated starting revision. |
| Contributions | Contributors retain their rights and contribute under the project license | DCO sign-off is proposed; no copyright assignment or separate CLA is proposed at this stage. |
| Documentation | Maintain source and documentation in the same repository and review them together | Public hosting, release-version URLs, and CI configuration. |
| Experiment records | PostgreSQL for collected results, separate artifact storage, portable JSON/CSV snapshots | Data model, hosting and backup ownership, lossless legacy import, and coordinated writer cutover; see [](benchmark-record-storage.md). |
| Governance | Public technical decisions and an explicit route to maintainership | Initial maintainers, organization owners, review authority, and dispute resolution. |

The existing private repository's license file is not treated as a decision
about the public release. Review any permissions already granted separately.
Do not describe the release as Apache-licensed until that choice is authorized
and the repository notices have been updated consistently.

Use the standard license text after selection. Inventory third-party code,
patches, dependencies, documentation, and benchmark data separately; the choice
for Primo-owned code does not replace their terms. Decide and document whether
original documentation and examples use the same license. Check source and
binary distributions separately, including linked dependencies and notices.

Relevant decision references are the [Apache-2.0 license, especially section
3](https://www.apache.org/licenses/LICENSE-2.0), the
[MIT license](https://opensource.org/license/mit), and the
[Developer Certificate of Origin](https://developercertificate.org/).
These references inform the pending choice; they do not establish rights in
Primo or authorize a release.

## Contribution model

The proposed contributor-facing guide is [](contributing.md). It makes
documentation part of the pull request, scales validation to the change, and
keeps solver correctness and clean-room requirements explicit. It is a draft,
not an additional contribution agreement.

Before opening contributions publicly:

- Agree on contribution licensing and any sign-off requirement. Explain what a
  sign-off certifies and how to provide one; distinguish it from a cryptographic
  commit signature.
- Publish a short root `CONTRIBUTING.md` that points to the maintained guide,
  rather than duplicating its requirements.
- Publish conduct and security-reporting policies and identify working contact
  channels. Define how wrong answers, invalid models, and confidential inputs
  are reported.
- Define who reviews soundness-sensitive changes, how reviewers resolve
  disagreement, and how regular contributors become maintainers.
- Keep expensive cluster qualification accessible through maintainers. External
  contributors should be able to submit a useful change without an account on
  the founding group's infrastructure.

## Documentation as code

Primo already authors documentation in `docs/` using MyST Markdown, builds it
with Jupyter Book, and stores experiment metadata in `docs/run-log.json`.
`scripts/build_docs.sh`, invoked by `make docs`, rebuilds all pages and treats
Sphinx warnings as errors by default. These are existing mechanisms.

The public workflow should extend them as follows:

1. Source, tests, and affected documentation are reviewed in the same pull
   request and merged together. Separate commits within that request are fine.
2. Reviewers check factual accuracy, examples, limitations, and algorithmic
   justification as well as whether the documentation builds.
3. Required CI builds the book. Executable examples get explicit test coverage
   where they assert solver behavior; a successful book build alone does not
   check them. Notebook execution is currently disabled in `docs/_config.yml`.
4. Publish documentation for released versions and identify development
   documentation clearly. Include the source revision so claims can be traced
   to the code being described.
5. Keep user guides, reference material, design explanations, and dated
   experimental evidence distinguishable. Historical results remain attached
   to their measured revisions.

CI, hosting, versioned publication, and additional example checks are proposed
work. Writing this plan does not establish that they are configured. Keep the
existing MyST/Jupyter Book tooling unless a concrete limitation warrants a
change.

## Migration milestones

Each milestone needs a named owner, evidence links, and a recorded outcome
before it is marked complete. Owners and dates are not yet assigned.

### 1. Establish publication rights and inspect the material

- Resolve university and contributor authorization and select the license.
- Inventory dependency and data provenance and required notices.
- Audit all history, branches, tags, attachments, and hosted artifacts intended
  for publication for secrets and material without redistribution permission.
  Ignore rules do not remove previously tracked material.
- Review private infrastructure details in scripts and experiment records.
  Preserve scientifically useful provenance while documenting any redaction;
  do not rewrite historical evidence to imply that a public rerun occurred.
- Record the history decision. If publishing a new starting revision, retain a
  private archive and a public provenance mapping sufficient to identify the
  qualified source snapshot, without exposing material removed by the audit.

### 2. Make the repository usable by an outside contributor

- Reconcile the README, current-status page, and theory documentation on the
  exact release candidate. Remove obsolete branch language and describe
  quantified UF and incremental-mode limitations consistently.
- Verify a clean build without local caches or private infrastructure. Document
  supported platforms, prerequisites, dependency versions, and download checks.
- Provide small examples for solving, models, sessions, and quantified input,
  with expected outcomes and explicit unsupported boundaries.
- Adopt the contribution guide and add issue and pull-request templates,
  citation metadata, and release notes.
- Review the source archive contents. The current `make archive` workflow omits
  Markdown/MyST sources; decide the public release packaging explicitly so
  documentation sources remain available and editable alongside each release.
- Replace the shared experimental ledger with the database and artifact storage
  proposed in [](benchmark-record-storage.md). Preserve historical IDs,
  provenance, and evidence limitations. Keep schema/tools and reviewed reports
  in Git, with immutable exports for published results.

### 3. Configure the organization and verification workflow

- Choose the organization/repository names, owners, recovery arrangements, and
  least-privilege access. Set required reviews and status checks.
- Run build/tests and the documentation gate in public CI. Provide scheduled or
  maintainer-run sanitizer, static-analysis, fuzzing, and benchmark jobs as
  appropriate; record which checks are required for each change category.
- Keep execution of untrusted contributions separate from privileged runners,
  publication credentials, and cluster access.
- Assign maintainers to run expensive qualification and attach evidence to the
  exact candidate being reviewed. Existing correctness gates remain in force.
- Configure documentation previews and publication. Enable repository/edit
  links only after their destination exists.
- Assign database and artifact-storage ownership; test concurrent ingestion,
  offline recovery, backups/restoration, and portable exports before retiring
  the existing ledger. Builds must not require service access.

### 4. Qualify and publish the first release

- Verify the exact candidate using the applicable existing checks and gates.
  Do not describe old measurements as fresh qualification of changed behavior.
- Publish selected reproducibility artifacts with stable locations, input and
  executable hashes, commands, limits, and separate correctness/timing results.
  Record inaccessible datasets or incomplete validation as limitations.
- Tag the source and publish release notes, documentation, archives, checksums,
  and the required license notices.
- Update clone URLs, badges, documentation links, automation, and package
  references; verify the public path with a fresh checkout.
- Audit remaining jobs/processes and record the release revision and remaining
  known issues before the announcement.

The transfer mechanics should be checked against
[GitHub's transfer documentation](https://docs.github.com/en/repositories/creating-and-managing-repositories/transferring-a-repository)
and [visibility-change documentation](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/managing-repository-settings/setting-repository-visibility)
at execution time. Review settings, integrations, retained credentials, and
historical logs as part of the migration.

## Completion boundary

This draft is complete when the plan and proposed contribution guide are
reviewable and build with the documentation. The migration itself is complete
only when rights and policy decisions are recorded, publication checks pass,
and an outside user can access and reproduce the released project. Pending
license and governance choices remain visible until decided.
