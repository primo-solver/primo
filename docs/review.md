# Documentation closing review

**Status: page-by-page technical review completed; correctness follow-up blocks
publication readiness. Consolidated reader acceptance remains pending.**

The replacement manual is now the main book. `make docs-serve` previews the
assembled current and historical books at `http://localhost:8000`. Publication
is a separate operation; changing the source tree does not deploy a website.

## Coverage and review record

The [2 October page-by-page review](coverage/page-review.md) covers all 84 pages
of the current manual at its baseline revision, records the corrections and
lists unresolved source findings. In particular, the
{ref}`symmetry comparison omits numeric payloads <symmetry-payloads>`; two
independent source reviews traced a potentially unsound restriction. No solver
reproduction or repair is claimed. The earlier qualification sample does not
clear this finding.

The [preservation audit](coverage/index.md) accounts for 91 old pages, 1,150
section headings, 126 supporting entries, and 1,306 original URL targets.
The old book is retained as editable source in `docs-history/` and is built
under <a href="archive/original/index.html">archive/original</a>. It preserves the final
old book, including later corrections; all targets from the original
compatibility snapshot were checked against that book before the move.

```{csv-table}
:file: review.csv
:header-rows: 1
:widths: 18, 12, 14, 28, 28
```

The ledger records technical inspections at their original revisions. Its
stages do not automatically advance after a successful build. Research leads
remain proposals; accepting their descriptions does not approve implementation.
The [earlier page-by-page review record](review-history.md)
retains the detailed chronology and literature-reading notes.

## Closing corrections

- The frontend chapter now distinguishes core terms, theory literals, and SAT
  literals/clauses. Parser nodes do not cross these solver interfaces.
- The model guide distinguishes the rebuild engine from the assumptions engine:
  the latter constructs a model for `--check-model`; SMT-LIB `get-model` still
  requires `:produce-models true`.
- The statistics reference documents session counters and the incremental
  combined model-check/printing timer.
- The dependency introduction includes GMP.
- Current guides use the main book paths. Shared prose, examples, assets and
  bibliography are available without the former draft tree.

## Technical verification

The [publication qualification record](evidence/publication.md) records the
SLURM checks, bounded fuzzing and model validation. Both books build strictly;
local targets, pinned source links and historical routes are checked together.
Browser inspection covered the project URL prefix, equations, navigation,
favicon and an archived route. External-host access failures remain explicit
in the link report. Technical checks do not substitute for reader acceptance.

## Acceptance review

Read the introduction/capability map, user guides, implementation chapters,
contributor procedures and evidence boundaries as one candidate. Check that
examples explain the observable behavior, mathematical notation is introduced,
source links identify the owning code, and paper citations distinguish the
implemented subset from future work. Inspect equations, figures, navigation,
search, old fragment routing, and the project-site URL prefix in the rendered
site. Record the reader's acceptance explicitly after that review.

[Publication preparation](work/publication.md) records verification and external
requirements. [Evidence work](work/evidence.md) retains unresolved historical
mappings and quantifier-checking limits. No full-corpus performance claim follows
from the documentation build or the publication qualification sample.
