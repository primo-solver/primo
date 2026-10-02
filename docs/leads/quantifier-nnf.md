# Optional NNF through quantifiers

**Status: Open lead. Owner: unassigned.**

Investigate optional negation normal form (NNF) for quantified input and
generated instances. NNF pushes negation through Boolean connectives and
quantifiers until it occurs only on atoms. For example:

```text
not (forall x. (p(x) or exists y. r(x,y)))
    ≡ exists x. (not p(x) and forall y. not r(x,y))
```

This could give simplification and instance caching fewer syntactic forms to
handle and make effective quantifier kinds explicit. The core term API,
clausifier, and quantifier component must continue to accept general formulas;
NNF would be a selectable preprocessing policy, not a global invariant.

## Existing support and the missing extension

{source}`rewrite_boolean_negations_to_nnf
<src/smt/preprocessing.cpp#L3304-L3453>` already normalizes `not`, `and`, `or`,
and implication. It memoizes positive and negative forms of each source term
and avoids distributive expansion. Other node kinds, including quantifiers,
are opaque. The {source}`integer preprocessing strategy
<src/smt/preprocessing_strategy.cpp#L434-L445>` selects it within the enabled
integer preprocessing loop. This is an existing textbook rewrite, not a new
proposal; its contract is in the {source}`preprocessing note
<docs/preprocessing.md#L118-L145>`.

Quantified input {source}`takes a separate route before that pipeline
<src/smt/solver_session.cpp#L624-L655>`. Its current
{source}`instance simplifier <src/theory/quantifiers/instance_simplifier.cpp#L12-L15>`
also treats quantifiers as opaque. The proposed work is to extend normalization
through binders and select where it runs on the quantified route.

## What could become simpler

Equivalent inputs such as `not (forall x. p(x))` and
`exists x. not p(x)` could share a normalized quantifier term and its later
cache entries. Rewriting nested negative connectives could expose Boolean
simplifications after instantiation. These are hypotheses to measure:
NNF does not identify every pair of equivalent formulas, and creating both
polarities of a shared subformula can increase the retained term set.

NNF alone does **not** remove the current four quantifier cases. Even in NNF,
`Q or r` permits `Q = false, r = true`. The component still owes the semantics
of a false quantified atom under its current assignment-based contract.
The [polarity handling](../implementation/quantifiers.md#universal-instances-and-existential-witnesses)
therefore remains necessary. Omitting irrelevant assigned obligations would
require a separate [relevance/partial-assignment argument](quantifier-relevance.md). The
[asserted-quantifier lead](asserted-quantifiers.md) instead exploits an obligation
whose truth value is already fixed by an active assertion.

## Candidate design and obligations

- Extend the existing `(TermId, polarity)` traversal with quantifier duality.
  Preserve binder order, sorts, de Bruijn indices, and sharing; define how
  attached patterns are retained when a binder is dualized. Keep theory atoms
  and Boolean arguments used as values behind their existing preparation
  boundaries. Use the term manager for every new node.
- Specify whether normalization runs on input, generated instances, or both,
  and its order relative to Boolean argument naming and extended-term lowering.
  Preserve the meaning of guards that mention previously registered atoms.
  Cache and export records must still identify the corresponding original
  quantifier, its polarity, and its witnesses.
- Require equivalence of each normalized formula under every interpretation.
  This transformation introduces neither Skolem symbols nor a prenex prefix.
  Retain the general path when disabled and the current conservative `sat`/
  `unknown` boundary in either mode.
- Count visited term/polarity pairs, new nodes and edges, retained memory,
  normalization time, duplicate instances, witness obligations, and resulting
  SAT clauses. DAG memoization avoids tree duplication; it does not establish
  an end-to-end speedup or bound constructor and flattening costs by itself.

## Basis and first evaluation

The binder extension is a proposed local design using ordinary De Morgan and
quantifier-duality equivalences. The existing NNF contract and the
[binder chapter](../implementation/quantifiers.md#substitution-without-variable-capture)
provide its representation obligations.

Masina, Spallitta and Sebastiani, [*On CNF Conversion for SAT and SMT
Enumeration*](https://arxiv.org/html/2303.14971v7), JAIR 83 (2025), §4,
studies NNF combined with Plaisted–Greenbaum encoding for short partial
assignments. Its §5.5 and Appendix C report no advantage for plain SAT/SMT
solving on their tested sets. This is context for the separate
[polarity-aware CNF lead](../papers/polarity-aware-cnf.md), not evidence for
quantified instantiation in Primo. Enumerating satisfying assignments and
enumerating substitution tuples are different workloads.

Compare the unchanged route with input-only and input-plus-instance NNF,
keeping tuple order, budgets, SAT settings, and root handling fixed. Begin with
the {source}`existing NNF regressions <tests/unit/preprocessing_tests.cpp#L352-L395>`
and extend coverage to both binder polarities, alternating quantifiers,
shared subterms, Boolean arguments, patterns, and generated guarded lemmas.
Use small independently evaluated formulas to check equivalence, then fresh
[correctness gates](../development/correctness.md) and paired remote SLURM
evaluations. Record solved counts, resource limits, runtime/PAR2, and peak
memory alongside normalization and cache counters. No implementation or new
performance evidence is recorded yet.
