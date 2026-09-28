# Quantifier instantiation efficiency

**Status: Open lead. Owner: unassigned.**

The [quantifier chapter](../implementation/quantifiers.md) describes the
implemented enumerator, caches, queue scheduling, and conservative answer
boundary. The following investigations remain separate from that baseline.
Optional [NNF through quantifiers](quantifier-nnf.md) and
[special treatment of directly asserted quantifiers](asserted-quantifiers.md)
have separate leads, so their costs and benefits can be evaluated independently.
[Relevance filtering](quantifier-relevance.md) asks whether a quantified
obligation is needed at all; the filters below concern instances of an
obligation that remains relevant.

## Entailment filtering and fail masks

Carry forward the existing
{source}`entailment and fail-mask design <docs/quantifier-entailment-design.md>`.
Reynolds, Barbosa and Fontaine's
[*Revisiting Enumerative Instantiation*](https://hanielbarbosa.com/papers/tacas2018.pdf),
§4.1, supplies the candidate entailment filter; Janota et al.'s
[*Fair and Adventurous Enumeration of Quantifier Instantiations*](https://people.ciirc.cvut.cz/~janotmik/fmcad21.pdf),
§IV, supplies fail masks for skipping classes of redundant tuples.

Primo currently removes duplicate tuples and normalized instances, but does
not consult EUF to test entailment. A new query contract must define which
assignment and congruence state it observes, how explanations or cached
results survive rollback, and how generated atoms invalidate the query state.
Keep the quantifier component independent of private EUF representation.
Skipping an instance requires a sound redundancy argument; a cache miss or
failed match supplies none. SAT completion needs its own justification and
must not follow automatically from adding an incomplete filter.

## Time spent before useful work is delivered

The current stall cutoff only yields once a formula is queued. Investigate
bounded, resumable work before the first useful formula, and cancellation
checks inside long scans. Preserve the tried-set and pool-growth invariants;
interruption must neither lose tuples nor turn incomplete enumeration into
`sat`. Measure scans, useful lemmas, SAT re-entry costs, and retained memory
before choosing a policy or changing the defaults.

## Acceptance

Choose one change at a time and retain the unchanged enumerator as a control.
Test redundancy decisions against small independently checked contexts,
including both polarities, nested quantifiers, Boolean witnesses, pool growth,
and rollback. Run fresh [correctness gates](../development/correctness.md),
with reference disagreements explicitly triaged, before comparing corpus
results. Keep independent refutation confirmations and inconclusive outcomes
separate. Record total runtime, solved count, memory, and whether reduced
enumeration work actually offsets new bookkeeping.
