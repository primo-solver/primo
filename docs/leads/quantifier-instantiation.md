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

The equality, model-refinement and fail-mask investigations below are connected:
choose candidate tuples, decide whether an instance makes progress, then use
a justified mask to skip a whole family. They need a shared contract for
context validity and revisitation, with separately measurable costs.

(quantifier-equality-enumeration)=
## Enumeration modulo current equalities

**Open question:** can Primo enumerate one representative per current equality
class while preserving refutational completeness? Also investigate grouping
terms that have the same value in a candidate model, keeping the two
equivalence relations distinct.

Reynolds, Barbosa and Fontaine's TACAS 2018 paper §4.1 describes CVC4's
dynamic term selection using equalities **entailed by the current ground
assignment** $E$: a new term is admitted when $E$ does not entail its equality
to an already selected term. Section 3, Corollary 2 requires fair instance
generation for refutational completeness {cite:p}`reynolds2018enumerative`.
Transferring that guarantee to a changing set of representatives needs an
argument about the complete enumeration procedure. This lead does not
establish a completeness defect in CVC4 or cvc5; current cvc5 behavior has
not been audited here.

For a quantifier body $B$ and a fixed $E$, substitutivity gives
$E\models B(\bar t)\leftrightarrow B(\bar r)$ whenever
$E\models t_i=r_i$ for every position. For example, while $E$ contains
$a=b$, the instance $B(a)$ covers $B(b)$ in that context. After backtracking
to a context where $a\ne b$, that reason for skipping $B(b)$ disappears.
Equality only in a particular model $M$ gives equivalence in $M$; it need
not give this entailment from $E$. Using such model equalities requires a
separate argument about model checking and refinement.

**Current Primo status: unimplemented.**
{source}`add_pool_terms <src/theory/quantifiers/quantifier_component.cpp#L178-L195>`
retains syntactic terms, including EUF-equivalent ones.
{source}`enumerate <src/theory/quantifiers/quantifier_component.cpp#L288-L375>`
uses those pools and a persistent tried-tuple set. Adding contextual skipping
to that set without revisitation would lose work after the context changes.

Before implementation:

- Specify whether the equality view reports $E$-entailed equality or equality
  in $M$, and its assignment/model epoch. Keep queries behind a theory
  interface; do not expose EUF's private representatives to the enumerator.
- Keep the original candidate terms available. Define how merges, splits,
  representative replacement, new terms and rebuilt theory state affect
  candidate indices, traversal progress and retries. Already emitted guarded
  lemmas remain valid; contextually skipped tuples have a different lifetime.
- Prove coverage and fairness across those changes, including terms introduced
  by earlier instances and both universally behaving quantifier polarities.
  Fair enumeration of one fixed quotient alone is insufficient. Consider
  interleaving a fair traversal of the original pools as a fallback.
- Test a class that later splits, changing representatives, new terms in
  existing classes, model-only coincidences and repeated context changes.
  Measure original terms, classes, skipped/retried tuples, useful instances,
  equality-query cost and memory before claiming a benefit.

The initial target is fewer redundant instances. Exhausting representatives
must retain the current `unknown` outcome for unresolved universal obligations;
accepting `sat` requires a separate quantified-model argument. Completeness
under unbounded fair search and Primo's resource-limited behavior must also
remain distinct.

(quantifier-model-refinement)=
## Model-based filtering of enumerated instances

Keep enumeration as the candidate generator, then filter its instances against
the current model. For an active enumerative obligation, let $L$ be the required
ground instance, with its assigned polarity, and let $M\models E$ be the
candidate ground model. Emit instances false in $M$ and defer those true in
$M$; an inconclusive evaluation must not discard a candidate. This targets
lemmas that directly rule out the current model.

The connection to entailment filtering is:

- **Entailment filtering:** if $E\models L$, the instance excludes no model
  of $E$ and can be skipped in that context. Failure to prove entailment says
  nothing about whether the current $M$ satisfies $L$.
- **Model-based filtering:** if $M\not\models L$, adding the guarded instance
  rules out this candidate while the obligation is active. A true instance in
  $M$ does not achieve that immediate goal, but can still exclude other models
  of $E$ and help a later refutation.

**MBQI is due to Ge and de Moura**, *Complete Instantiation for Quantified
Formulas in Satisfiability Modulo Theories*, CAV 2009, §6
{cite:p}`ge2009complete`. Its model-checking procedure finds counterexample
values and maps them back to instantiation terms. In the proposed filter,
the enumerator supplies the tuples and the model test decides which instances
to emit. This does not constitute an implementation of MBQI.

The TACAS 2018 paper §4, Figure 2 and Example 2 compares enumerative
instantiation and MBQI {cite:p}`reynolds2018enumerative`.
Example 2 shows a refutation assembled from instances at one term, including
instances satisfied by the initial candidate model. Compare the proposed
filter with unfiltered enumeration: instances satisfied
now may still help a later refutation. The filter's completeness remains open
until its treatment of deferred work and changing models is justified.

This ties directly to representative enumeration: equal tuples under $E$
have interchangeable bodies in every model of $E$; equal tuples only under
$M$ have interchangeable bodies in that model. Both selection and skipping
must be reconsidered when their supporting context changes. Keep emitted
lemmas separate from instances deferred because this particular model
satisfies them.

Primo currently supplies neither the entailment query nor a candidate-model
evaluator to the quantifier component. A proposal must define evaluation of
new ground applications, model completion, nested obligations and the
lifetime of the model view. An unavailable value must remain unknown, not
be treated as satisfaction. Require a coverage/refinement argument or fair
enumeration fallback before emitting only model-breaking instances;
finding none in a bounded scan cannot establish `sat`.

(quantifier-fail-masks)=
## Entailment filtering and fail masks

Carry forward the existing
{source}`entailment and fail-mask design <docs/quantifier-entailment-design.md>`.
Reynolds, Barbosa and Fontaine's
[*Revisiting Enumerative Instantiation*](https://hanielbarbosa.com/papers/tacas2018.pdf),
§4.1, supplies the candidate entailment filter; Janota et al.'s
[*Fair and Adventurous Enumeration of Quantifier Instantiations*](https://people.ciirc.cvut.cz/~janotmik/fmcad21.pdf),
§IV, supplies fail masks for skipping classes of redundant tuples.

A fail mask generalizes a rejection by fixing only some substitution positions:
every completion of the other positions must satisfy the same rejection
criterion. For example, if $E\models P(a)$, all instances
$P(a)\lor R(t)$ are entailed, so the candidate for $t$ need not be examined.
If only $M\models P(a)$ is known, the same family cannot break that particular
model; this supports only a model-scoped mask under the proposed selection
policy. One satisfied tuple alone cannot justify wildcard positions.

Connect masks to both investigations above. Record whether a mask follows
from persistent rewriting/duplicate evidence, entailment by $E$, or truth
in $M$. Validate it against that evidence's lifetime. Model or assignment
changes may invalidate the latter two; representative replacement also changes
what an index-based pattern denotes. Retain stable term identities or rebuild
patterns, and restore skipped work when its justification disappears. These
are obligations for the Primo adaptation; the FMCAD paper supplies masks
from entailment and rewriting, not the proposed model-truth extension.

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

## Memory-growth candidate

On `sledgehammer/FFT/uf.706467.smt2`, the preserved `4419dc9` binary fails a
term-vector allocation during instantiation under an 8 GiB address-space cap;
the [diagnostic traces](../evidence/long-limits.md) establish the allocation
failure before the secondary cleanup abort. Use this as a candidate for the
filters above and the [memory-retention investigation](incremental-memory.md#current-memory-exhaustion-candidates).
Measure generated terms and retained cache/lemma bytes alongside useful
instances and live roots. This distinguishes the potential benefit of producing
fewer terms from reclaiming terms no longer needed; the trace alone proves
neither redundant instantiation nor reclaimable garbage.

## Acceptance

Choose one change at a time and retain the unchanged enumerator as a control.
Test redundancy decisions against small independently checked contexts,
including both polarities, nested quantifiers, Boolean witnesses, pool growth,
rollback, representative changes, and invalidated masks. Check every completion
of small masked patterns against the corresponding filter, and check that
retries do not starve new tuples. Run fresh [correctness gates](../development/correctness.md),
with reference disagreements explicitly triaged, before comparing corpus
results. Keep independent refutation confirmations and inconclusive outcomes
separate. Record total runtime, solved count, memory, and whether reduced
enumeration work actually offsets new bookkeeping.
