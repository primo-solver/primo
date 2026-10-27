# Quantifier decisions and checking evidence

This page records why particular settings were selected and what external
checks established. The [implementation chapter](../implementation/quantifiers.md)
describes current behavior. All counts below belong to their recorded revision,
not to a new evaluation of the documentation branch.

## Tuple order and round size

The run `2026-09-13-quantifiers-tuple-orders-144606` in the
{source}`ledger <docs/run-log.json>` compares six orders at `fc786f1`, on
7,590 UF files with a 60-second cutoff and a 100,000-distinct-instance budget.
References were reused by path from the recorded Z3/cvc5 run.

| Order | Solved |
| --- | ---: |
| `max` | 1867 |
| `max-colex` | 1891 |
| `sum` | 1911 |
| `leximax` | 1876 |
| `deepening` | 1864 |
| `random` | 1846 |

This comparison motivated `sum` as the default. It gained 82 files and lost 38
against `max`; it was not uniformly better. The virtual best solved 2,020,
which motivates investigating portfolios but is not an implemented portfolio
result. The experiment predates later generated-term correctness repairs.
Its ranking is historical evidence, not fresh qualification of today's default.
The cited enumeration papers justify methods and completeness conditions;
they do not establish Primo's best default configuration.

| Later question | Recorded result and boundary |
| --- | --- |
| One instance or 128 per round? | `2026-09-15-quantifiers-instances-per-round-attribution-145630`, `ff72a12`: the same binary solved 1,686 with one and 1,932 with 128. Repeated SAT/component rebuilds explain why early delivery can still be expensive. |
| Stop a round after prolonged redundant enumeration? | `2026-09-15-quantifiers-stall-cutoff-72d00d3`: 1,948 solved versus 1,932; 19 gained and 3 lost. The 65,536-tuple cutoff is selected, not a tuned optimum. It applies only after a useful lemma is queued. |
| Did the generated-term repair receive a fresh gate? | `2026-09-14-quantifiers-merge-readiness-ff72a12` records its own scope and outcomes. It does not retroactively repair earlier experiment binaries. |

Retain the comparison artifacts named in those records, including
`results/uf-quantifier-compare-144606` and its per-order summaries. The
{source}`original chronology <docs/quantifiers.md#L25-L160>` also preserves
round/stall counters and the separate cvc5 enumeration comparison.

## What a pool-guided cvc5 check proves

{source}`quantifier_pool_check.py <scripts/quantifier_pool_check.py>` checks
selected Primo **UNSAT** answers. It adds pool annotations to the original
input and runs cvc5 with `--pool-inst`. It is not a quantified SAT-model
validator. Pool terms guide cvc5's own instantiation; Primo's chosen universal
instances are not accepted as a proof.

The export contains fresh Skolem/Herbrand constants, pools, and witness lemmas.
For an existential $Q=\exists x.B(x)$, the witness condition
$Q\Rightarrow B(c)$ is satisfiability-preserving when $c$ is fresh: if $Q$ is
true, choose a witness; otherwise the implication imposes no requirement.
For false universal $Q=\forall x.B(x)$, use
$\neg Q\Rightarrow\neg B(c)$. Each group must witness only its own obligation;
the freshness and dependency conditions are essential.

{source}`The exporter <src/smt/quantifiers/pool_export.cpp>` marks top-level
existentials and negated universals. The checker locates an eligible original
assertion by quantifier kind, variable sorts and occurrence, looking through
`let` and annotations. It adds a witness instance constructed from the input's
own text and drops the corresponding printed lemma. It retains the original
assertion. Matching a different eligible assertion still permits a fresh witness
for that assertion; it can make the pools less useful without changing
satisfiability, provided the witness group is not constrained elsewhere.

Unlinked witness lemmas remain as printed. Therefore the check still relies on
the export obeying the fresh-witness contract and on correct reconstruction
and serialization of those lemmas. The script does **not** independently prove
that arbitrary exported assertions are conservative extensions. Its
annotation-stripping check establishes that adding pools preserved the original
command text; it does not establish all witness obligations. Describe these
runs as independent cvc5 corroboration under the export contract, not as proof
checking independent of every possible Primo printing/export defect.

Terms or lemmas mentioning unexportable internal constants are skipped and
counted. Losing pool hints or conservative witness lemmas can lose a
confirmation; it cannot establish one. The checker rejects scoped input and
uses logic `ALL` for pool syntax. The
{source}`checker tests <tests/python/test_quantifier_pool_check.py>` cover
annotation restoration, top-level linking, unmatched exports, command ordering,
definitions and result classification. They are not a formal verification of
the complete trust argument.

## Recorded corroborations and negative controls

`2026-09-15-quantifiers-cvc5-pool-checks`, at `3b9264a`, used 60 seconds each
for Primo and cvc5 1.3.4 with `--pool-inst`, one CPU and 4 GB per array task.
The final arrays were jobs 146068 and 146069; the ledger preserves their
commands and separate artifact directories.

Of 1,947 selected UNSAT answers, 1,601 were corroborated, 345 remained
inconclusive, and one was not checked because the Primo rerun timed out.
The record reports nine reference-less answers still unconfirmed. Of 109
reference-SAT negative controls, 100 ran without a cvc5 UNSAT result and nine
were not checked. These controls support the implementation argument but
do not prove that every export is sound. Keep the unconfirmed and unchecked
rows visible; no new run is performed by this rewrite.
