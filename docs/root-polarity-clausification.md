# Root-polarity clausification experiment

This page records a rejected Boolean clausification prototype and the invariant
it clarified. Primo does not currently provide a root-polarity clausification
option.

## Assertion semantics

At `check-sat`, all active SMT-LIB assertions denote one implicit conjunction.
Primo keeps them in `std::vector<TermId> assertions` for command scopes,
frontend-generated side constraints, preprocessing provenance, and to avoid
constructing one artificial high-arity `And` term. The vector is an operational
representation, not an additional logical boundary.

Consequently, a future top-level normalization must give equivalent treatment
to separate assertions, one assertion containing their conjunction, and root
De Morgan or implication equivalents. It may use the vector as an implicit
conjunct list; it must not make a semantic optimization depend on where the
user wrote an `assert` command.

## Prototype

The temporary prototype extended `smt::BooleanClausifier` with an explicit
`(TermId, required_value)` worklist and a two-bit-per-term visited table shared
across the active assertion vector. For a term required true, it decomposed
`And`, emitted one clause for `Or` or `Implies`, and reversed the requirement
through `Not`. For a term required false, it applied the dual rules: decompose
`Or`, emit one negative-child clause for `And`, split a false implication into
a true antecedent and false consequent, and reverse `Not`.

This was a deliberately narrow required-context extraction, not a complete
NNF/CNF conversion and not an implementation of Plaisted-Greenbaum
clausification. It never distributed `Or` over `And`; nested subformulas still
used Primo's cached Tseitin-style encoding. The background is the linear-size
structure-preserving idea of G. S. Tseitin, *On the Complexity of Derivation in
Propositional Calculus* (1968). Griggio's MathSAT thesis, section 2.2, provides
the relevant SMT engineering caution: MathSAT retained plain Tseitin conversion
after more elaborate CNF algorithms showed no clear general winner.

The worklist was `O(V + E)` over reachable Boolean terms and direct child edges,
with each term processed at most once per required value. Tests covered
assertion-boundary equivalence, both connective polarities, constants, shared
subterms reached under both requirements, and unchanged LRA tableau shape.

## Evaluation and rejection

The first prototype collected all theory-atom polarities before emitting any
clauses. That changed SAT variable order and therefore search. The corrected
prototype interleaved polarity collection and clause emission in the existing
assertion order while retaining shared signed visited state. This restored exact
baseline search statistics where no useful decomposition occurred.

On merge commit `91f94b9`, which includes main's default post-preprocessing
bounded flattening, the corrected prototype gave:

| instance | decompositions | SAT variables | clauses | decisions | time |
|---|---:|---:|---:|---:|---:|
| `main3_true` baseline | - | 5,615 | 22,381 | 56,304 | 3.82s |
| `main3_true` prototype | 2 | 5,615 | 22,381 | 56,304 | 3.45s |
| `mt20xx` baseline | - | 9,510 | 51,027 | 75,021 | 15.18s |
| `mt20xx` prototype | 33 | 9,481 | 50,943 | 75,021 | 14.24s |

The `mt20xx` change removes only 29 variables and 84 clauses; decisions, solve
calls, and theory conflicts are identical. The timing difference is a single-run
diagnostic and is not sufficient evidence of a speedup. The other established
mixed representatives exposed only one or two decompositions. The prototype was
therefore reverted before fuzzing or benchmark submission.

The experiment remains useful as a design constraint. Root simplification, if
revisited, should normalize the implicit conjunction early enough for ordinary
preprocessing to consume the exposed conjuncts, and it needs evidence that this
changes more than a handful of clauses. The current performance lead is instead
the mixed shared-model path: on `mt20xx`, the merged baseline performs
18,190,224 LRA model-filter full-pair visits and projects 5,972,400 terms during
compatibility checks.
