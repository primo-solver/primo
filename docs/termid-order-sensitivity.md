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
  *informed* rule -- which is how [H11](#h11) came about.

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
is the subject of [H11](#h11): choose by expected fill-in
(Markowitz), as [H3](#h3) already does for the tableau.

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

(termid-4)=
### 4. ite auxiliaries moved from the frontend to the lowering leaf

`ite` became a `TermKind` (2026-09-04), so its auxiliary constants are no
longer allocated while parsing but by the `lower-extended-terms` preprocessing
leaf. The constants are the same ones, defined by the same two guarded
equalities, but they are created later, so their `TermId`s -- and the SAT
variable numbering derived from them -- differ.

Visible in the statistics as `:input.assertions`: an assertion containing an
ite used to be counted as one assertion plus the frontend's side assertions,
and is now counted as one, with the expansion happening downstream.

**Decides:** SAT variable order, and everything that reads it.

**Measured, and it cost one solve.** A/B over 1300 QF_LRA and QF_LIA
benchmarks weighted to ite-bearing files, 824 of which completed inside 10s on
both binaries: 823 identical answers, and
`QF_LIA/rings/ring_2exp16_3vars_1ite_unsat.smt2` went from `unsat` to
`unknown` on the branch-and-bound node limit. On that instance the encoding is
demonstrably unchanged -- 4 ite auxiliaries both ways, `term-size.initial` 182
and `final` 157 both ways, every preprocessing and profile-selection counter
identical -- and only the search differs: 40 conflicts, 1180 decisions and 497
pivots before, against 3, 166 and 84 after. Raising `--lia-branch-limit` twenty
fold does not recover it, but `--preprocessing-mode=basic` or `strong` solves
it, which is what identifies this as search-path luck rather than a worse
encoding. The same binary at `--preprocessing-mode=basic` answers `unsat`; the
pre-change binary answers `unsat` under all five settings tried.

Against that single loss, the same run shows ite auxiliaries falling from
132601 to 128049, with 169 files needing fewer and none needing more.

**Correction, 2026-09-05: the auxiliary count was the wrong metric, and most of
what followed from this entry was not order sensitivity at all.** The full
QF_LIA division at 60s lost 79 files and gained 35, and the loss was recorded
as timeout-boundary churn from the id shift described above. It is not. Two
measurements say so:

- **Paired runtimes** on files both binaries solve: geometric mean `head/base`
  is **1.1023 on `nec-smt`** and **0.9878 on every other family**. Order churn
  does not concentrate on one family.
- **Preprocessed formula size** (`--preprocess-only`, so entirely
  deterministic -- no scheduler, no timeout, no search), whole division, job
  131983: `nec-smt` **+10.11%**, every other family 0.00% except `RWS` at
  -19.46%. 2789 files bigger, 53 smaller, 10172 byte-identical.

+10.11% size against a +10.23% runtime mean is the same number twice. The cause
is the equality distribution in `simplify-extended-terms`, which bought a 2.7%
reduction in auxiliaries for a 10% larger formula. The auxiliary count went
down while the thing it was standing in for went up.

This entry stays open, because the id shift it describes is real and does
produce two-way churn -- the paired ratios have p10 0.60 and p90 1.85, and
there were 35 genuine gains. It is simply not what the net number measured.
Recorded here because "it is probably order churn" is exactly the kind of
diagnosis this docket exists to stop people accepting without a control.

**Full-division measurement, 2026-09-05.** The balance is now established, and
it is a timeout-boundary effect rather than a capability change.

At a 60s budget the change costs solves: QF_LIA **9041 -> 8997 (-44)** over all
13,306 files, QF_LRA **1539 -> 1534 (-5)** over 1,753, QF_UFLRA
**1271 -> 1270 (-1)** over 1,284. No wrong answers on either side in any
division, and no file solved by both binaries got a different answer. The
QF_LIA losses concentrate in `nec-smt`, 68 of 79, which is the ITE-heavy family
(2,780 files, every one containing both `let` and `ite`).

Those losses are not new incompleteness. 73 of the 79 are timeouts, and the
median time the *baseline* needed to solve them was 46.8s of the 60s budget.
Re-running all 115 files whose solved-status changed, both binaries, at 300s:
**72 of the 79 lost files are solved by head**, only 7 are genuinely out of
reach, and 11 of the 35 files head gained remain unsolvable by base even at
300s. Over that boundary set at 300s the score is base 103, head **107**, so
the sign flips. Total runtime across the full QF_LIA division differs by 0.5%,
which bounds any systematic slowdown.

Read together: the encoding is not worse, and at a longer budget the change is
marginally better. At a 60s budget it costs about 50 solves across the three
divisions, because a large population of `nec-smt` instances sits within a few
seconds of the limit and id order decides which side they land on.

**Caveat on that reading.** The 300s comparison covers only the 115 files that
changed at 60s. A file solved by both binaries at 60s could still have become
slower without crossing the threshold, and this measurement would not see it.
The 0.5% total-runtime difference is the bound on how much of that there can
be.

**Status:** accepted, not repaired. Preserving the old id order would mean
allocating the auxiliaries in the frontend again, which is the design this
change deliberately removed. The instance is recorded so that a future
benchmark table showing a lost `rings` solve is recognised rather than
re-diagnosed.

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
