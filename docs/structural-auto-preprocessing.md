# Structural automatic preprocessing selection

This note specifies the opt-in `--qf-uf-auto-selector=structural` dispatcher.
It replaces assertion-root counts with properties of the implicit conjunction
without changing the preprocessing transformations themselves. The default is
still `legacy` until differential fuzzing and the QF_UF/QF_LRA benchmark gates
justify promotion.

This selector is an original empirical policy, not an implementation of a
paper algorithm. Its soundness obligation is limited: it selects among existing
sound preprocessing subsets. Its performance claims therefore require measured
validation rather than a theorem citation.

## Ownership and data flow

`smt::CheckSatOptions` owns `QfUfAutoSelectorMode` and
`AutoPreprocessTuning`. `check_sat_smtlib` invokes two analysis-only helpers
before the preprocessing loop:

- `analyze_implicit_conjunction` inspects `core::TermId` roots and positive
  top-level `And` descendants;
- `FiniteDomainAtMostOne::analyze` reuses the finite-domain recognizers but
  counts candidate domains and clauses without creating terms.

The returned values are solve-local. They neither outlive the frontend's
`TermManager` nor enter SAT or a theory solver. If the selected profile needs
finite-domain clauses or symmetry constraints, the existing generators create
them after selection through `TermManager`.

## Invariants and operations

The assertion vector denotes one conjunction. A positive `And` underneath an
assertion root contributes its children as conjuncts recursively; an `And`
under `Not`, `Or`, implication, equality, or a UF application remains inside
one conjunct. Consequently `(assert A) (assert B)` and
`(assert (and A B))` have the same selector features.

The conjunction analyzer builds a postorder over unique reachable `And` nodes,
then propagates saturating root-occurrence weights from parents to children.
It reports occurrence and unique counts, clause-like conjuncts, and implications
whose literal antecedent and literal/disjunctive consequent can become one
clause. Shared DAG nodes are inspected once even when their occurrence weight
is greater than one.

The finite-domain analyzer recognizes both choice disjunctions and observed
equalities to pairwise-disequal values. It predicts pairwise at-most-one clauses
and same-domain disequal-endpoint clauses arithmetically. The estimate can be an
upper bound when an equivalent generated clause is already an assertion; it is
used only as a diagnostic in this first selector version.

The structural profiles are, in priority order:

1. `large-skip` when implicit conjunct occurrences exceed the configured cap;
2. `finite-domain-basic` when the recognized domain and symmetry guards pass;
3. `compact-clause-basic` for bounded clause-like conjunctions with recognized
   finite-domain choices;
4. `strong` otherwise.

All thresholds are explicit options. Zero means unlimited only for upper
bounds:

- `--auto-preprocess-large-conjunct-limit`;
- `--auto-basic-compact-term-limit`;
- `--auto-basic-compact-min-conjuncts`;
- `--auto-basic-single-term-limit`;
- `--auto-basic-single-min-domains`;
- `--auto-basic-single-max-symmetry-terms`.

The defaults reproduce the numeric limits of the legacy selector. They do not
constitute evidence that the structural policy should become default.

## Complexity and limitations

For `V` unique reachable `And` nodes and `E` direct `And` edges, implicit
conjunction analysis takes `O(V + E + R)` time for `R` assertion roots and
`O(V + C)` memory for `C` terminal conjuncts. Counts saturate at
`size_t::max()`. Finite-domain analysis has the same recognizer cost as the
existing finite-domain scan plus quadratic counting in each observed value
set; it allocates only temporary maps and sets.

This first implementation does not estimate equality-definition,
congruence-definition, or repeated-loop growth. Clause-like classification is
syntactic and deliberately does not distribute arbitrary Boolean formulas.
The structural mode also does not make implication rewriting implicit; that is
a separate opt-in transformation.

## Proof obligations and tests

- Packaging-equivalent conjunctions must produce identical structural profile
  counters, including nested and shared `And` DAGs.
- Analysis must not grow `TermManager`.
- Choice and observed-equality finite domains must both be recognized.
- Every threshold boundary must be configurable and reported.
- Legacy mode and implication rewriting must remain off by default.
- QF_UF and QF_UFLRA merge fuzzing must exercise structural mode before any
  default promotion; ordinary QF_LRA and QF_UF samples must retain default
  answers and non-time statistics.

The selector reports `:preprocessing.auto.structural.*`,
`:preprocessing.auto.analysis.*`, `:preprocessing.auto.profile.*`, and the
configured `:preprocessing.auto.limit.*` values. During evaluation,
`:preprocessing.auto.selector.legacy-disagreement` identifies inputs whose
selected profile differs from the legacy assertion-count policy.
