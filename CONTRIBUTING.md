# Contributing to Primo

Start with the [correctness guide](docs/development/correctness.md) and
[implementation-change guide](docs/development/contributing-changes.md).
Documentation, minimized examples, tests, and tools are welcome contributions.
Discuss substantial algorithms or changes to defaults before implementation.

## License and sign-off

Primo-owned contributions are submitted under the [MIT license](LICENSE).
New contributions after public launch require certification under the
[Developer Certificate of Origin 1.1](https://developercertificate.org/).
Add your own sign-off with `git commit -s`; the name and email must identify the
contributor certifying their right to submit the work. A sign-off is a commit
message trailer, not a cryptographic signature or copyright assignment. There
is no separate CLA. Do not sign on someone else's behalf or rewrite historical
commits merely to satisfy this new process.

The DCO check applies to new pull requests once enabled at launch. Existing
history is covered by the separate publication-rights review. Maintainers are
subject to the contribution requirements too.

## Correctness and review

Source, tests, and affected documentation belong in the same pull request.
Read primary literature and write original implementation notes/code/tests;
do not copy these from other solvers. See [AGENTS.md](AGENTS.md) for the
clean-room policy and architectural boundaries.

Run formatting, lint, unit tests, and relevant documentation examples. Solver
behavior changes also need fresh fuzzing against the candidate revision:
normally at least 1,000 generated/mutated inputs compared with a trusted
reference, with stronger gates for default promotions. See the correctness
guide for narrow smoke gates, model checks, and required provenance. A passing
CI job does not replace the fuzz gate. Contributors may use any suitable
machine; maintainers can run expensive qualification on their infrastructure.

Report wrong answers, invalid models, and crashes before performance results.
Include the revision, command, limits, expected/actual result, and a shareable
minimized input. Use [security reporting](SECURITY.md) for vulnerabilities or
sensitive details; never post confidential benchmark inputs publicly.

The project owner is the initial maintainer. Soundness-sensitive changes need
maintainer review of the invariant, regression, fuzzing and model evidence.
Record technical disagreements and decisions in the issue or pull request.
New maintainers are appointed by the owner after sustained reviewed contributions.

Discuss the work respectfully: give evidence, welcome corrections, credit
contributors, and avoid harassment or personal attacks. Maintainers may remove
abusive content and restrict participation. Ordinary questions and conduct
concerns that are safe to share can be raised with the repository maintainers;
do not include another person's private information in public discussions.
