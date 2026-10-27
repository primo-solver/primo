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
notices. The build also bundles installed documentation-package notices and
their versions/hashes under `_static/licenses/index.json`, including the theme,
Bootstrap, Font Awesome, and icon notices supplied by those packages. This
inventory travels with the assembled site and HTML source-archive contents.

## Benchmark inputs

The 19 inventoried imported seeds, performance cases, and regression inputs
match members of [SMT-LIB release 2025.08.04](https://zenodo.org/records/16740866)
byte-for-byte. The release metadata declares **CC BY 4.0**. Preserve the
[SMT-LIB attribution and license notice](LICENSES/SMT-LIB.md) and each file's
original source header when distributing them. They are not relicensed under
Primo's MIT license.

The [input inventory](docs/coverage/third-party-inputs.json) records the release,
archive checksums, member paths, file hashes, original attributions, and license
basis. Repository paths were changed, but the matched file contents were not.
The absence of an embedded license field in these copies is resolved by the
identified release metadata; it is not a reason to remove the files or rewrite
history. The comparison covers the listed current files, not every historical
benchmark version or every other fixture.

## Papers and bibliography

Downloaded reading PDFs and raw bibliography imports stay outside the published
tree. Bibliographic records, stable publication links, and Primo's original
explanations are retained. The repository's MIT license does not apply to the
cited papers.
