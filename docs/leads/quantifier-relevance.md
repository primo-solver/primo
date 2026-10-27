# Skipping irrelevant quantified obligations

**Status: Open lead. Owner: unassigned.**

For an assertion `X or Q`, where `Q` is a quantified formula and `X = true`,
the assertion needs neither `Q` nor `not Q`. Investigate avoiding witnesses
and universal-instance enumeration for such **don't-care** quantifiers.
Their semantic truth is still determined by the eventual interpretation;
we need not force it to match an unnecessary SAT assignment.

This can affect the result as well as runtime. Under the current completion
rule, an assigned universal obligation can cause `unknown` even if that atom
is unnecessary to satisfy the input. Relevance alone would not establish a
universal obligation that the input actually needs.

## What Primo currently does

{source}`QuantifierComponent::check
<src/theory/quantifiers/quantifier_component.cpp#L80-L123>` requires a complete
quantifier assignment and classifies every assigned atom. It creates witnesses
for the existential cases and enumerates instances for the universal cases.
The final check still rejects uncovered witnesses and universal obligations;
it has no notion of Boolean relevance. Thus an irrelevant atom can cost work
in either polarity. Earlier simplification may remove an occurrence entirely,
but there is no filter for this situation in a surviving SAT candidate.

The quantified route explicitly {source}`disables static pure-theory filtering
<src/smt/quantifiers/quantified_solver.cpp#L97-L117>`. New instances can introduce
opposite-polarity occurrences. Re-enabling that filter is not the proposed
solution: relevance depends on the current support of the whole formula.

## Required support

Let `F` be the Boolean abstraction of all active prepared assertions, treating
quantified formulas as atoms. From a satisfying SAT candidate `M`, select a
subset `S` of signed original atoms such that:

```text
S ⊆ M                 S propositionally entails F
```

Here entailment means **every** Boolean completion of `S` satisfies `F`.
Separately establish that the selected ground and quantified obligations have
a joint interpretation. Then that interpretation satisfies `F`, regardless
of how it evaluates the omitted quantified atoms. A merely consistent subset,
or a subset with some satisfying Boolean extension, does not suffice.

| Active assertions and candidate | Sufficient Boolean support | Consequence |
| --- | --- | --- |
| `X or Q`, with `X = true` | `{X}` | No obligation for `Q` from this assertion. |
| `X or Q`, with `X = false, Q = true` | `{Q}` | The truth of `Q` needs justification. |
| `X or Q` together with `not Q`, with `X = true, Q = false` | `{X, not Q}` | The second root requires the false-polarity obligation. |
| `X or Q` together with `Y or not Q`, with `X = Y = true` | `{X, Y}` | Both occurrences can be omitted, although `Q` occurs in both polarities. |

The table establishes only propositional support. If `X` or `Y` is a theory
atom, its selected value must still be checked by its theory. If a retained
existential obligation introduces a witness, the witness body needs support
too, including any nested quantified requirements.

## First candidate design

Start with a structural support pass at the existing
{source}`complete-candidate boundary <src/smt/component_dispatcher.cpp#L132-L150>`.
Follow the asserted roots through the Boolean DAG, retaining enough children
to justify each required value. Union the requirements of all roots and shared
uses. A cheap sufficient support is enough; finding the smallest support is
not the initial objective. Keep the SAT solver and its assignment authoritative.

Retain ground-theory checking initially and filter only quantified obligations.
This conservative scope may still do unnecessary ground work, but isolates the
quantifier contract from a general change to theory assertion and model
construction. The SMT layer should own selected signed term IDs, the support
choices, and an epoch; the quantifier component should receive a view of those
obligations. Cache witnesses and instances as before, while allowing an omitted
atom to become relevant again on a later candidate.

Use the input structure and its preparation provenance. A traversal over all
Tseitin definition clauses can retain obligations solely to fix auxiliary
values. {source}`Nested definitions <src/smt/clausifier.cpp#L476-L500>` are
currently bidirectional. The design must account for auxiliary projection
without mistaking a definitional constraint for an independent user demand.
Boolean expressions used as term values, such as function arguments, need a
separate dependency audit; propositional branch selection cannot discard a
value that a retained theory term requires.

## Proof and lifetime obligations

- **Completion:** use the same justified relevance view for generation and
  final acceptance. Skipping enumeration alone leaves the current `unknown`
  rule unchanged. Accepting `sat` requires joint satisfaction of every retained
  obligation, not exhaustion of the selected tuple pool.
- **Generated work:** close support over required witness bodies and nested
  obligations. Distinguish input assertions, conservative witness extensions,
  and entailed instance lemmas. Specify how their guards affect relevance;
  neither rooting every generated definition unconditionally nor ignoring
  every generated formula is an adequate policy.
- **Changes of candidate:** recompute or invalidate support on assignment
  changes, new atoms/lemmas, and scope changes. A term irrelevant in one round
  can be required later. Retained caches must preserve signed guards and
  witness dependencies. No stale support may authorize final acceptance.
- **Progress and models:** retain ground-search progress, sound conflicts and
  existing limits for relevant quantifiers. Future quantified-model printing
  and the current pool export must not impose discarded candidate values on
  the original quantified formulas.

These are proposed obligations; no relevance-aware completion proof or API has
yet been accepted.

## Literature basis and related leads

The [literature survey](../papers/relevance-filtering.md) reads de Moura and
Bjørner (2007), Sebastiani (2007, 2020), and Niemetz, Preiner and Biere
(FMCAD 2014). It separates online relevance propagation, candidate reduction,
ghost literals, and the meaning of partial satisfaction. Their mechanisms
motivate this design; applying them to Primo's witness and completion protocol
is additional work.

[Optional NNF](quantifier-nnf.md) may simplify Boolean structure but does not
decide relevance. [Directly asserted quantifiers](asserted-quantifiers.md)
are necessarily relevant while their assertions are active; specializing them
is complementary to skipping conditional obligations. The older arithmetic
relevance investigation has separate performance questions. It does not gate
this quantifier investigation.

## First evaluation and acceptance

Add regressions for every row above, both quantifier kinds and both assigned
values, shared opposite-polarity uses, nested witnesses, ITEs, Boolean arguments,
and candidates where the selected support changes. Include a required
universal whose instances happen to hold: it must remain inconclusive under
the current incomplete method. Independently check extracted supports on
small Boolean abstractions and include deliberately insufficient subsets as
negative controls. Cover incremental assertions/pop and new generated roots.

Measure selected and omitted quantifiers, witnesses, tuple visits, lemmas,
SAT rounds, support time and memory, and resulting `sat`/`unsat`/`unknown`
counts. Compare with the unchanged route, holding NNF, root specialization,
budgets and tuple ordering fixed. Run fresh
[correctness gates](../development/correctness.md) and paired evaluations before any promotion. Online watches, a dual extraction solver,
and general ground-theory filtering remain later experiments. No solver change
or new performance result accompanies this lead.

Contributors may use any suitable machine. In the maintainer workspace, these
batches run on SLURM; see the {ref}`execution policy <run-the-applicable-local-checks>`.
