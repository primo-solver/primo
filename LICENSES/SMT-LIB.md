# SMT-LIB benchmark attribution

The 19 imported benchmark files listed in
[the input inventory](../docs/coverage/third-party-inputs.json) are distributed
under **Creative Commons Attribution 4.0 International (CC BY 4.0)**, as declared
by the matching release's [Zenodo metadata](https://zenodo.org/api/records/16740866).
The inventory identifies every repository path, original archive member, and
SHA-256 digest. These benchmark files are not covered by Primo's MIT license.

## Collection

**SMT-LIB release 2025 (non-incremental benchmarks)**, version **2025.08.04**.
Mathias Preiner, Hans-Jörg Schurr, Clark Barrett, Pascal Fontaine, Aina Niemetz,
and Cesare Tinelli. Zenodo. DOI:
[10.5281/zenodo.16740866](https://doi.org/10.5281/zenodo.16740866).
The [SMT-LIB initiative](https://smt-lib.org/) maintains the benchmark library.

License: [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), including
its [legal terms](https://creativecommons.org/licenses/by/4.0/legalcode).
Retain appropriate credit, the license link, and indications of modifications
when redistributing or adapting these files. Preserve their original source
headers. No endorsement of Primo by the benchmark contributors is implied.

## Individual sources

The original source fields are retained verbatim in the files and inventory:

- **MathSAT:** the MathSat group, for the RandomCoupled and RandomDecoupled cases.
- **QG-classification:** the University of Birmingham quasigroup benchmark
  collection identified by the original source URL. The member
  `QG-classification/qg5/gensys_brn685.smt2` is named
  `benchmarks/regression/qf_uf/online_reason_cache.smt2` here.
- **DTP-Scheduling:** F. Maris and P. Régnier, IRIT, Université Paul Sabatier,
  Toulouse; TLP-GP automated DTP-to-SMT-LIB planning encodings.
- **check:** SMT-COMP 2006 organizers, for the two bignum cases.
- **SAL gasburner:** Bruno Dutertre, John Rushby, Maria Sorea, and Leonardo de
  Moura, SRI; translated from CVC format using CVC Lite, as recorded upstream.
- **KeYmaera:** submitted by Dejan Jovanović; source headers credit the work of
  Dejan Jovanović and Leonardo de Moura, and of André Platzer, Jan-David Quesel,
  and Philipp Rümmer. Retain those headers for the original references.

## Changes and verification

On 7 October 2026, all 19 repository files were matched byte-for-byte to
18 distinct members of this release's QF_UF, QF_LRA, and QF_UFLRA archives.
The RandomDecoupled `pb_real_40_80_60_01.smt2` member is present in two repository
locations. File names and directory layout differ; file contents are unchanged,
including their source metadata. Complete archive checksums matched those
published in the release record. The inventory records these checks and how to
repeat them. Existing per-file headers do not include a license field; the
license evidence is the release metadata.

This notice covers the listed imports. Primo's original tests and generated
fixtures retain their own provenance and licensing.
