# Docket: TermId-order sensitivity

A running list of places where **the numeric values of `TermId`s change solver
behaviour**, not merely which terms exist. Each entry names the site, what it
decides, whether it is confirmed by measurement, and its status.

## Why this is a docket and not a bug list

Order sensitivity of this kind is ordinary in SMT. Solvers routinely break ties
on internal identifiers, and doing so is deterministic and correct. Nothing
here is a soundness problem, and most entries will never be "fixed".

The reason to track it is narrower and practical:

- **It makes benchmark data quietly incomparable.** A frontend change that
  touches no solver code can move per-instance timings, because it shifted the
  ids that a tie-break reads. Diagnosing that from a benchmark table costs
  hours; the 2026-08-02 entry below cost about that.
- **It is invisible in tests.** Both outcomes are correct, so nothing fails.
- **It hides the real question.** A tie-break on an identifier optimises
  nothing. Where one turns out to matter, that is usually an argument for an
  *informed* rule -- which is how [H11](qf-lra-heuristics.md#h11) came about.

So: record the site, note whether it was measured, and only act where an
informed rule would plausibly beat an arbitrary one.

(termid-test)=
## The test that detects it

Permuting `declare-fun` lines must not change the search. Identical assertions,
declarations in a different order, compare solver counters:

```
(set-logic QF_LRA)(declare-fun a () Real)...(declare-fun e () Real)  <body>
(set-logic QF_LRA)(declare-fun e () Real)...(declare-fun a () Real)  <body>
```

`declaration_order_does_not_change_the_search` in
`tests/unit/qf_lra_solver_tests.cpp` is the checked-in instance.

**What does *not* work:** comparing a `produce-models` run against a plain one.
A defect that is unconditional moves both arms together, so that comparison
passes while the defect is present -- verified, it did.

## Entries

(termid-1)=
### 1. Gaussian elimination pivots on the smallest TermId

`src/smt/preprocessing.cpp`, `collect_gaussian_elimination_candidates`:

```cpp
std::sort(eligible_variables.begin(), eligible_variables.end());  // by TermId
for (const core::TermId variable : eligible_variables) {          // pivot order
  for (std::size_t i = 0; i < rows.size(); ++i) {                 // first row with it
```

**Decides:** which variables are eliminated, hence which substitutions
`substitute_solved_constants` accepts, hence the clauses the SAT solver sees.

**Confirmed, measured.** On `miplib/vpm2-0`, shifting TermIds moved
`:preprocessing.solve-eqs.rewrites` 390 -> 252 and `:lra.pivots` 26890 ->
12537. On a five-variable chain, reversing declarations took accepted
substitutions from 4 to 0.

**Status:** open. The pivot rule is uninformed -- it optimises nothing -- which
is the subject of [H11](qf-lra-heuristics.md#h11): choose by expected fill-in
(Markowitz), as [H3](qf-lra-heuristics.md#h3) already does for the tableau.

(termid-2)=
### 2. `polynomial_to_term` builds terms in TermId order

`src/smt/preprocessing.cpp`, `polynomial_to_term` sorts a polynomial's
variables by `TermId` before rebuilding the sum. The comment says this is *for*
determinism, and within a run it is.

**Decides:** the structural shape of the rebuilt term, hence what gets
hash-consed. Two runs whose TermIds differ produce semantically equal but
structurally different terms.

**Not measured.** No instance is known where this changes an outcome; it is
listed because the mechanism is present and would be a candidate explanation
for an otherwise unexplained shift.

**Status:** open, low priority. A canonical order that does not read ids (for
example by coefficient then by a stable name) would remove it, at the cost of a
comparison that is no longer O(1).

(termid-3)=
### 3. `LraVarId` assignment order feeds Bland's rule

`LraTableau::add_variable()` hands out sequential ids, so an `LraVarId` records
*when* a variable was first seen while `PreparedLraTheory` walks the theory
atoms. Bland's rule then selects "the smallest index among the eligible
candidates" (`lra_tableau.cpp`).

**Decides:** entering/leaving variable choice on the Bland path, and the
termination tie-break that the Dantzig and SOI rules fall back to.

**Not measured**, and note the indirection: atom order comes from the
clausifier, so this is one step removed from TermIds rather than reading them.
It belongs here because the same class of reasoning applies.

**Status:** open, and deliberately not a candidate for change. Bland's rule
*must* use a fixed arbitrary order -- that is what makes it terminating. This
entry exists to be recognised, not repaired.

## Resolved

(termid-resolved-1)=
### Eager interning of declared Real constants (2026-08-02, fixed in `e3059e6`)

Model production interned a term for every declared Real constant at
`declare-fun` time, assigning TermIds in declaration order rather than
first-use order. Unconditional -- it applied whether or not a model was
requested -- so it perturbed every QF_LRA benchmark run through
[entry 1](#termid-1).

Fixed by recording `SymbolId` and materialising terms in
`declared_real_constant_terms()`, called only when a model was requested and
only after the search. Regression test: [](#termid-test).

The general shape is worth remembering: a feature that only *reads* solver
state can still change it, if the reading allocates identifiers.

## Adding an entry

State the site, what it decides, and whether a measurement confirms it. An
unmeasured entry is fine -- entries 2 and 3 are -- but say so, rather than
implying evidence that does not exist.
