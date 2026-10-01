# Tactics and preprocessing preservation map

This audit accompanies [tactics](../implementation/tactics.md) and
[preprocessing](../implementation/preprocessing.md), checked against their
owning code on 2026-10-01. The original pages remain available at their current
paths and anchors while the replacement is reviewed.

**Preserved** identifies an explanatory destination for a current mechanism.
**Superseded** names a correction or an intentionally different organization.
**Retained history** identifies material to keep in the dated archive at
cutover; it does not revalidate its experiments. Archive paths and redirects
remain part of the book-wide migration gate.

## Tactics

| Original section | Disposition and destination |
| --- | --- |
| {source}`Introduction and algorithmic basis <docs/tactics.md#L1-L33>` | Preserved in the [chapter introduction](../implementation/tactics.md) and {ref}`implemented subset <tactics-evidence>`, citing de Moura–Passmore §3 through the shared bibliography. |
| {source}`Implemented data structures <docs/tactics.md#L34-L118>` | Preserved in {ref}`representation <tactics-representation>`, {ref}`goal truth <tactics-goal-truth>`, and the [model chapter](../implementation/models.md). |
| {source}`Immutable strategy and compilation <docs/tactics.md#L119-L182>` | Preserved in {ref}`strategy identity <tactics-strategy-identity>`; explicit manager-snapshot and session-closure lifetimes qualify reuse. |
| {source}`Tacticals and probes <docs/tactics.md#L183-L204>` | Preserved in {ref}`composition <tactics-composition>` and {ref}`strategy identity <tactics-strategy-identity>`, including fixed-point output retention and cooperative deadlines. |
| {source}`Production composition <docs/tactics.md#L205-L321>` | Preserved in {ref}`session-to-engine flow <tactics-production>` and {ref}`preprocessing order <preprocessing-order>`. Prefix execution before profile selection is explicit. |
| {source}`Exhaustive Boolean splitting <docs/tactics.md#L322-L372>` | Preserved in {ref}`splitting <tactics-experiments>`: complete alternatives, deterministic selection, model ownership, exponential materialization, and the cap. |
| {source}`Sequential evaluation <docs/tactics.md#L373-L413>` | Retained history: focused manifests, repeated measurements, branch costs and results. No performance promotion is inferred by the new chapter. |
| {source}`Composition verification <docs/tactics.md#L414-L462>` | Current contracts preserved in {ref}`checks <tactics-evidence>`; exact runner and SLURM manifest remain linked. Earlier execution instructions and provenance details remain history pending the contributor-workflow extraction below. |
| {source}`Deviations, invariants and complexity <docs/tactics.md#L463-L526>` | Preserved across {ref}`composition <tactics-composition>`, {ref}`identity <tactics-strategy-identity>`, {ref}`scope <tactics-incremental>` and {ref}`paper subset <tactics-evidence>`. The blanket statement that incremental tactics are absent is superseded by the audited-leaf contract. |
| {source}`Incremental execution contract <docs/tactics.md#L527-L542>` | Preserved in {ref}`incremental use <tactics-incremental>` and its links to both assumptions-engine designs. |
| {source}`Tactic-reference catalogue <docs/tactic-reference.md#L1-L157>` | Mechanisms preserved in both chapters; the full builder/parameter/counter inventory still belongs in a separate reference page, recorded below. |
| {source}`Unrepresented transformations and portfolio limitations <docs/tactic-reference.md#L158-L192>` | Preserved in {ref}`session portfolios <tactics-experiments>`, {ref}`strategy identity <tactics-strategy-identity>` and {ref}`limitations <tactics-evidence>`: no tactic parser, learned portfolio, or isolated parallel executor. |

## Preprocessing

| Original section | Disposition and destination |
| --- | --- |
| {source}`Introduction and arithmetic atom canonicalization <docs/preprocessing.md#L1-L210>` | Current scheduling and the two distinct normal forms preserved in {ref}`execution order <preprocessing-order>` and {ref}`arithmetic normalization <preprocessing-extended>`, linked to [LRA](../implementation/lra.md). Dated A/B comparisons and default decisions remain history. |
| {source}`Experimental QF_UF portfolio <docs/preprocessing.md#L211-L424>` | Current typed fallback contract preserved in {ref}`session portfolios <tactics-experiments>`. Screens, family-specific observations and rejected configurations remain history. |
| {source}`Boolean unit propagation <docs/preprocessing.md#L425-L458>` | Preserved in {ref}`round order and facts <preprocessing-round>`, including retained units, sharing and model records. |
| {source}`Forced atom propagation <docs/preprocessing.md#L459-L511>` | Preserved in {ref}`facts <preprocessing-round>` and {ref}`representation <preprocessing-representation>`: congruence, known disequalities, retained facts and later finite-domain use. |
| {source}`Solved constants and Gaussian elimination <docs/preprocessing.md#L512-L638>` | Preserved in {ref}`definition elimination <preprocessing-round>`, {ref}`integer profiles <preprocessing-profiles>`, and {ref}`conversion example <preprocessing-example>`. Historical measurements remain at the old location. |
| {source}`Monotone elimination, preconditions and reconstruction <docs/preprocessing.md#L639-L725>` | Preserved in {ref}`one-sided variables <preprocessing-final>` and the [model conversion chapter](../implementation/models.md#following-a-model-back-through-preprocessing). The local attribution note remains the rule's design reference. |
| {source}`Historical monotone examples and counters <docs/preprocessing.md#L726-L841>` | Current repair rule and counter purpose preserved in {ref}`final passes <preprocessing-final>` and {ref}`evidence <preprocessing-evidence>`. Development examples and measurements remain history; their old helper locations are not current API guidance. |
| {source}`Pure literals and theory atoms <docs/preprocessing.md#L842-L905>` | Preserved with a concrete contradiction in {ref}`propagating facts <preprocessing-round>`. |
| {source}`Real-equality lowering <docs/preprocessing.md#L906-L958>` | Preserved in {ref}`normalization boundary <preprocessing-extended>`. The implementation also handles Int; the new text makes that explicit. The late-generated-equality regression is retained as a composition obligation. |
| {source}`ITE lowering <docs/preprocessing.md#L959-L1065>` | Preserved in {ref}`extended terms <preprocessing-extended>` and [integer input translation](../implementation/lia.md#input-lowering-and-integer-bounds). Growth controls are distinguished from mandatory elimination. |
| {source}`Constant ITE simplification <docs/preprocessing.md#L1066-L1137>` | Current CITE subset, cache scope, integer-safe identities, and ordering preserved in {ref}`extended terms <preprocessing-extended>`. Arena-growth test linked through {ref}`correctness checks <preprocessing-evidence>`. Timing and corpus promotion history stay at the old location. |
| {source}`Constructor versus pass work <docs/preprocessing.md#L1138-L1190>` | Preserved in [core representation](../implementation/core.md) and {ref}`final Boolean normalization <preprocessing-final>`. The current bounded second pass is explained rather than restoring unconditional constructor flattening. |
| {source}`Equality bridge <docs/preprocessing.md#L1191-L1229>` | Preserved with the diamond example and scope in {ref}`equality consequences <preprocessing-round>`. |
| {source}`Finite-domain clauses <docs/preprocessing.md#L1230-L1357>` | Preserved in {ref}`finite domains <preprocessing-finite>`: raw/post scans, distinctness, numeral restriction, observed atoms, disequal endpoints, equality definitions and growth. July probe results remain history. |
| {source}`UF symmetry breaking <docs/preprocessing.md#L1358-L1431>` | Preserved in {ref}`symmetry <preprocessing-finite>`: invariance, fixed terms, value precedence, moving-term rejection, and model-renaming justification. Historical solver comparisons remain history. |
| {source}`Implementation notes <docs/preprocessing.md#L1432-L1537>` | Current profile, fixed-point and adaptive behavior preserved in {ref}`profiles <preprocessing-profiles>` and {ref}`order <preprocessing-order>`. Historical tuning observations remain at this link. |
| {source}`Mixed-theory opportunities <docs/preprocessing.md#L1538-L1608>` | Retained research record. Current purification is explained in [combination](../implementation/combination.md); proposals are not presented as additional implemented preprocessing passes. |
| {source}`Current status <docs/preprocessing.md#L1609-L1676>` | Superseded as the current narrative by the code-checked profile/pass descriptions. Historical counts and formerly deferred work remain dated evidence. |
| {source}`Paper traceability <docs/preprocessing.md#L1677-L1701>` | Preserved and qualified in {ref}`sources <preprocessing-sources>`; bibliography identifies papers, while earlier development inspiration stays retrospective. No old solver implementation is an implementation source. |
| {source}`Structural selector design <docs/structural-auto-preprocessing.md#L1-L105>` | Selection semantics preserved in {ref}`profiles <preprocessing-profiles>`; detailed option inventory and analysis complexity remain in the design note pending the reference extraction below. |
| {source}`Structural selector evaluation <docs/structural-auto-preprocessing.md#L106-L124>` | Retained history, including the rejected default promotion. The current selector remains legacy. |

## Evidence identities

The original chapters retain the detailed experiment tables. Relevant
{source}`run-log entries <docs/run-log.json>` include:

- `2026-08-30-tactic-refactor-full-slurm-comparison` and
  `2026-08-31-tactic-refactor-infrastructure-profile`: refactoring comparison
  and investigation of tactic infrastructure costs.
- `2026-09-01-tactic-composition-slurm-short-circuit-audit` and
  `2026-09-01-tactic-composition-slurm-merge-gate`: composition coverage,
  including why a later leaf need not execute on an already settled input.
- `2026-08-12-structural-implies-benchmark-f88bc87` and
  `2026-08-17-arithmetic-canonicalization-default-off-979d911`: selector/rewrite
  and canonicalization decisions with their measured scope.
- `2026-09-06-lead16-cite-simplification-133177-133466` and
  `2026-09-07-cite-hoisting-lifetime-slurm-134014`: CITE evaluation and the
  later allocation-boundary repair. The later repair is not silently covered
  by the earlier evaluation.

Historical local runs are provenance, not instructions to run current solver
batches locally. This rewrite ran documentation checks only.

## Remaining reference and workflow extraction

```{admonition} TODO: Extract the tactic reference and composition-run procedure
:class: note

The implementation chapters now explain the mechanisms. Before replacing
the old book, extract the complete builder/parameter/counter catalogue from
the {source}`tactic reference <docs/tactic-reference.md>` into the new reference
section, including the distinction between public builders and internal round
phases. Preserve the structural selector's threshold semantics and analysis
cost in that reference.

The contributor experiment guide must explain how to select a legal composition
manifest, stage a remote run, verify the binary revision, interpret per-case
versus corpus-wide activity checks, and retain model-validation evidence.
Use the current scripts; do not copy historical local execution recipes as
current policy. This is shared with the {ref}`remaining LIA workflow extraction <lia-remaining-extraction>`.
```

This audit covers the named chapters and selector note. It does not mark every
linked literature note or open lead as migrated, and it does not assign reader
acceptance. Their file-level migration records remain in force.
