# Third-party material

Primo-owned code and original documentation use MIT. This statement does not
relicense third-party software, benchmarks, or generated documentation assets.

## Required build dependencies

| Component | Selected version | Terms and distribution boundary |
| --- | --- | --- |
| CaDiCaL | `rel-3.0.0` | MIT; downloaded separately. Preserve its [notice](LICENSES/CaDiCaL.txt) when distributing it. |
| CLI11 | `v2.6.2` | BSD 3-Clause; downloaded separately. Preserve its [notice](LICENSES/CLI11.txt) in source/binary distributions that include it. |
| GMP | `6.3.0` | LGPL v3-or-later or GPL v2-or-later, recipient's choice. The installer builds static libraries; this source snapshot does not distribute linked binaries. Review the [GMP distribution conditions](https://gmplib.org/manual/Copying) before distributing a linked binary. |

Dependency implementations and cached installations are excluded from Git and
the source archive. Their original licenses remain authoritative. The notice
download URLs and hashes are recorded in [LICENSES/sources.json](LICENSES/sources.json).

## Optional tools and patches

The retained YinYang patch includes upstream context covered by the
[YinYang MIT notice](LICENSES/YinYang.txt). Primo's added changes are MIT.
Reference solvers, fuzzers and validators installed under `contrib/local` keep
their own terms; they are not included in this source archive. Running a tool
as a reference does not authorize copying its implementation into Primo.

The documentation is built using Jupyter Book and Sphinx. Generated HTML also
contains third-party theme, JavaScript, font and style assets. Preserve embedded
notices; consult the installed packages' license metadata when redistributing
the rendered site. Publication audit must cover these generated assets as well
as tracked source files.

## Benchmark inputs

Some tracked benchmark seeds and performance cases originate in SMT-LIB.
Their source headers and provenance notes must be retained. See the
[input inventory](docs/coverage/third-party-inputs.json) for identities and
unresolved terms. Missing license headers are not an MIT grant from Primo.
Current [SMT-LIB submission guidance](https://github.com/SMT-LIB/benchmark-submission)
encourages CC BY 4.0 but also permits other licenses; it does not establish the
license of every historical input already in this repository.

Resolve the inventory's missing publication terms before public distribution,
or agree a specific removal/history treatment with the project owner. No
benchmark is silently deleted or retrospectively relabeled by this preparation.

## Papers and bibliography

Downloaded reading PDFs and raw bibliography imports stay outside the published
tree. Bibliographic records, stable publication links, and Primo's original
explanations are retained. The repository's MIT license does not apply to the
cited papers.
