# Don't-care reasoning and relevance

**Status: Literature reviewed; dynamic relevance filtering is not implemented
in Primo.** This survey supports an open investigation, not an implementation
commitment. It distinguishes Boolean support selection from checking whether
the selected theory and quantified obligations can hold together.

## De Moura and Bjørner: relevance during search

Leonardo de Moura and Nikolaj Bjørner, *Relevancy Propagation*,
MSR-TR-2007-140 (2007),
[primary report](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/tr-2007-140.pdf).
Read §§1–3, especially §§3.1–3.2.

The report explicitly motivates relevance for quantifier instantiation as well
as arithmetic and bit-vectors. It retains the formula and the mapping from
Tseitin variables to its nodes. A relevant true disjunction needs one true
child; a false one needs all children. Equivalence needs both operands;
an ITE needs its condition and selected branch.

Relevance bits, an undo list, and child-to-parent watch lists react to assignment
and relevance events. An atom reaches a theory solver only when both conditions
hold. The SAT solver's ordinary search rules stay intact. The report gives no
general end-to-end complexity bound or quantified-completion theorem.

**Primo inference:** preserve structural support in the SMT layer; consider
online watches only after a candidate-based prototype. The proposed consumer
is `QuantifierComponent`, not a new SAT assignment stack. The unresolved work
is the completion contract and invalidation on new instances and scopes.

## Sebastiani: shorter assignments and ghost literals

Roberto Sebastiani, *Lazy Satisfiability Modulo Theories*, JSAT 3 (2007),
141–224, [author's paper](https://disi.unitn.it/rseba/papers/JSAT-2007.pdf).
Read §6.9 and §§7.2.1–7.2.2; compare §6.10 on pure-literal filtering.

Section 6.9 removes literals while retaining propositional entailment of the
input. The smaller assignment may be theory-consistent even when the original
one is inconsistent. Section 7.2.2 calls assignments to atoms occurring only
in already satisfied clauses *ghost literals*. Occurrence checks can avoid
such decisions and unnecessary theory calls. Original-clause satisfaction
tracking supports early completion; this differs from static polarity filtering.

**Primo inference:** test support reduction without requiring a smallest or
prime implicant. Defer SAT decision-policy changes. Clause occurrence lists
would belong to the SMT/SAT integration, with a separate satisfaction invariant;
their update cost must be measured. The survey does not supply a quantified
model certificate or resolve Primo's definitional-clause boundary.

## Niemetz, Preiner and Biere: filtering complete candidates

Aina Niemetz, Mathias Preiner and Armin Biere, *Turbo-Charging Lemmas on Demand
with Don't Care Reasoning*, FMCAD 2014, 179–186,
[author-hosted paper](https://fmv.jku.at/papers/NiemetzPreinerBiere-FMCAD14.pdf).
Read §§II–III and §IV.A–B, Figures 5–6 and Tables I–II.

The paper extracts partial candidates before consistency checking in a
quantifier-free bit-vector/array procedure. One method traverses the formula
DAG, selecting controlling inputs and the active ITE branch; a cost estimate
chooses among alternatives. Another maintains a solver for the negated
abstraction, fixes inputs by assumptions, and uses failed assumptions to
identify a sufficient subset. The latter requires another solver, input maps,
and refinement synchronization. Its overhead can outweigh the saved work in
the reported experiments; those results do not measure quantified UF.

**Primo inference:** start with structural traversal at the existing complete
candidate boundary. Defer a dual solver and cost heuristics. A visited-node
table suggests work proportional to visited DAG nodes and edges per candidate;
that is our design expectation, not a runtime bound established for Primo.
The unresolved mapping is from selected quantified atoms to guarded nested
obligations, rather than the paper's function applications.

## Sebastiani: what a partial assignment must establish

Roberto Sebastiani, *Are You Satisfied by This Partial Assignment?* (2020),
[primary preprint](https://arxiv.org/abs/2003.04225).
Read §3, Definitions 1–2, Proposition 1, Properties 4–5 and Example 3;
§4, Definitions 3–4.

For non-CNF formulas, evaluating to true under three-valued evaluation is
sufficient for entailment but can miss smaller entailing assignments.
The former is polynomial; general entailment checking is co-NP-complete.
Tseitin definitions can obscure short supports. Projecting auxiliary variables
also differs from fixing a single auxiliary assignment for all completions.
The quantified variables in this analysis are propositional variables; this
is not a first-order quantifier-instantiation procedure.

**Primo inference:** use structural justification as a sufficient test, with
signed term IDs, selected children and a visited set. Do not promise globally
minimal supports or add a general entailment oracle initially. Projection and
the distinction between semantic truth and an arbitrary SAT label must appear
in the design proof and tests. No implementation is derived from this note yet.

## Current owners and implementation status

| Mechanism | Primo status and source |
| --- | --- |
| Formula DAG and atom-to-literal mapping | **Implemented:** {source}`term manager <src/core/term_manager.hpp>` and {source}`BooleanClausifier::encode <src/smt/clausifier.cpp#L322-L432>`. These supply representation, not relevance tracking. |
| Complete-candidate quantifier scheduling | **Implemented:** {source}`ComponentDispatcher <src/smt/component_dispatcher.cpp#L132-L150>` waits for a complete assignment before requesting quantifier work. |
| Quantifier obligations and completion | **Implemented baseline:** {source}`QuantifierComponent::check <src/theory/quantifiers/quantifier_component.cpp#L80-L123>` scans every assigned quantifier. There is no support mask. |
| Static pure-theory filtering | A separate implemented mechanism, explicitly **disabled on the quantified route** in {source}`solve_quantified_uf <src/smt/quantifiers/quantified_solver.cpp#L97-L117>` because generated occurrences can invalidate initial polarity information. |
| Candidate support extraction and relevance-aware quantifier completion | **Deferred implementation; open investigation.** No candidate filter, online relevance propagation, ghost-decision avoidance, or dual extraction solver is claimed here. |

A proposed support object belongs to `smt/`, which owns assertion structure and
Boolean abstraction. It should hold stable `TermId`s and per-candidate metadata,
reading truth through the existing assignment view. The quantifier component
would consume selected signed obligations through an explicit interface.
It must not inspect SAT internals or maintain another truth stack. Recompute
or invalidate support when the candidate, active roots, or generated obligations
change. Term ownership remains with `TermManager`.

The first proof and regression obligations are support of **all** roots,
shared occurrences, both quantifier polarities, nested witness constraints,
generated instances, backtracking, and push/pop. Measure filtering time and
memory against avoided witnesses, tuple visits, lemmas and `unknown` outcomes.
General ground-theory filtering and online propagation require their own
extension/model arguments. No new solver experiment accompanies this survey.

The related [CNF note](polarity-aware-cnf.md) treats directional definitions and
NNF separately. Neither transformation alone implements relevance filtering.
The older {source}`arithmetic relevance lead <docs/open-leads.md#L1154-L1177>` retains
its family-specific motivation; its measurement sequence is not a prerequisite
for investigating quantified don't-cares.


The [quantifier relevance lead](../leads/quantifier-relevance.md) records the
candidate design and acceptance criteria for the replacement documentation.
