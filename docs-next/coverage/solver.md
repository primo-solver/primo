# Solver chapter preservation map

The [section inventory](index.md) lists every original heading and retained
source location. This map explains the content decisions behind those rows.
The frontend/session, LIA and tactics/preprocessing groups have additional
detailed maps. Source/evidence inspection is distinct from reader acceptance.

| Original group | Current explanatory home | Retained or superseded material |
| --- | --- | --- |
| Architecture: layers, ownership, hash-consing, sorts, constructors | [Architecture](../implementation/architecture.md), [core](../implementation/core.md), [frontend](../implementation/frontend.md) | Old implementation rollout and scaffold milestones remain history. Enum spellings/defaults belong in the option reference. |
| DPLL(T), variable ownership, assignment protocol | [DPLL(T)](../implementation/dpllt.md), [SAT](../implementation/sat.md) | Shared-assignment repair evidence remains dated; a removed duplicate stack is not an open feature. SAT-private auxiliaries do not acquire theory meanings automatically. |
| EUF interface, closure, explanations, Boolean arguments and models | [EUF](../implementation/euf.md), [models](../implementation/models.md) | Preserve Boolean-domain counterexample, pure-filter precondition and generated-term preparation argument. The old bridge-atoms route is removed; `smt::Dpllt` is not presented as a second production coordinator. |
| Disequality/Ackermann experiments | EUF propagation subsection and [theory-lemma lead](../leads/theory-lemmas.md) | Bounded watcher propagation is implemented; conflict-selected permanent-clause proposals and rejected scans remain separate. |
| Boolean DAG flattening and root-polarity prototype | [Preprocessing](../implementation/preprocessing.md), SAT encoding | The second flattening point's rationale is retained; the polarity prototype is a rejected experiment, not the implemented general Plaisted–Greenbaum method. |
| Arithmetic literal language, sparse rows, normalization, simplex, rollback and SOI | [LRA](../implementation/lra.md) | Exact numerical types stay here rather than in an otherwise redundant numerics page. Milestone chronology and algorithm ablations have an [evidence summary](../evidence/arithmetic.md). |
| Fixed-variable and model-phase mechanisms | LRA's dedicated subsections | Pure/mixed defaults and rejected row freezing remain distinct. The current rule is not a full LP column-deletion presolver or a SAT decision-variable callback. |
| Integer completion | [LIA](../implementation/lia.md), [LIA map](lia.md) | Integer-slack, GCD and infinitesimal counterexamples are preserved in current explanations or their dated repair records. Unbounded completeness and stronger tests remain leads. |
| Nelson–Oppen and modular combination | [Combination](../implementation/combination.md) | General driver and dedicated Real coordinator remain distinct. Old plans are corrected where later migrations completed them; historical runtime regression and model reconciliation results remain dated. |
| Quantifier representation alternatives and algorithms | [Quantifiers](../implementation/quantifiers.md) | Superseded variable-identity proposals retain their rationale and measurements. Indexed operations, polarity, witnesses, enumeration and bounded answer conditions are current; E-matching/MBQI are not implied. |
| Quantifier evaluation, tuple orders and checker | [Quantifier evidence](../evidence/quantifiers.md) | Old performance tables keep their pre/post-repair revisions. Pool checks are UNSAT corroboration with a witness-export trust boundary, not unconditional independent proof checking. |
| Typed models, reconstruction, visible signatures | [Models](../implementation/models.md), sessions, [model guide](../guide/models.md) | Candidate-value completion and printer-side heuristics superseded by typed reconstruction remain historical. No current invalid-model claim is inferred from an old bug directory. |

## Superseded claims that must not survive as current prose

The original roadmap's unchecked parser/SAT/EUF/LRA boxes describe an early
development stage. The current implementation exists. Dedicated difference
logic remains future work. The old statement that all integer preprocessing is
disabled is replaced by route-specific policies. General combination is already
used by QF_UFLIA and quantified UF; the Real migration remains open.

Old source filenames, local prototype timings and default-promotion proposals
are preserved as historical context, not silently refreshed. Current source
links use the centralized implementation revision; historical document records
use the separately pinned archive snapshot.
