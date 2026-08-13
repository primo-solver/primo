# A Fast Linear-Arithmetic Solver for DPLL(T)

## Citation

Bruno Dutertre, Leonardo de Moura:  
A Fast Linear-Arithmetic Solver for DPLL(T). CAV 2006.

Two companion sources, both consulted for this map:

- **The extended technical report**, Bruno Dutertre and Leonardo de Moura,
  *Integrating Simplex with DPLL(T)*, CSL Technical Report SRI-CSL-06-01,
  23 May 2006 (`docs/papers/qflra-sri-csl-06-01.pdf`, untracked -- reference
  papers are read locally and never committed). It carries the material the
  CAV version compresses: Section 3.1 Preprocessing, the `update` /
  `pivotAndUpdate` / `Check` pseudocode, the termination proof, and Gomory
  cuts. **Section references below are to the technical report** where the
  two differ.
- The authors' [CAV 2006 slides](https://yices.csl.sri.com/old/slides-cav06.pdf).

Several names below are the authors' own rather than ours, and are noted as
such.

## Role in Primo

This paper is the base algorithm for Primo's QF_LRA support: the
simplex tableau, its Assert-Bound/Check/Pivot loop, the
infinitesimal-delta encoding of strict inequalities, Farkas-style conflict
explanation, and the backtracking and propagation that make it a DPLL(T)
theory solver.

**The authors' own framing, worth using rather than paraphrasing.** They
distinguish *Standard Form* (`Ax = b`, `x >= 0`) from the **General Form**
(`Ax = 0` with `l_j <= x_j <= u_j`) their algorithm uses, and describe the
algorithm itself as "a new algorithm based on the **Dual Simplex**". Primo
implements the General Form: `LraTableau` holds equations and per-variable
bounds, and only bounds are asserted during search -- never new rows. Where
this project says "bounded-variable simplex", the paper says General Form,
and where these docs say *basic* / *non-basic*, the slides say **dependent**
/ **non-dependent** variable. Same objects throughout.

**What this file is.** A map from the paper's sections to the code that
implements them, plus the deviations and the extensions that go beyond it.
Nothing here is a progression, a measurement, or a milestone list.

**What this file is not.** It is deliberately *not* a second copy of the
roadmap. Read it alongside:

- [](../arithmetic.md) -- current implementation status, options, and the
  measured effect of each change.
- [](../qf-lra-heuristics.md) -- the 2026-07-27 heuristics round (items H1-H7):
  design rationale, evidence, and falsifiers.
- [](lra-related-work.md) -- everything Primo implements that is *not* from
  this paper (pivoting-rule survey, SOI-Simplex, Monniaux's sparsity
  criterion).
- `docs/run-log.json` -- the artifacts behind every benchmark claim.

This file previously restated the milestone history as well, and drifted:
by 2026-07-30 it still named `PivotingRule::Bland` as the default, described
slack dedup as scale-sensitive, and referred to identifiers
(`atoms_by_slack`, `true_threshold`/`false_threshold`) that no longer
existed. Keeping the progression in exactly one place is what stops that
recurring.

## Section-by-section map

| Paper | Implemented as | Where |
|---|---|---|
| Section 2 -- normalized linear terms and `t ~ c` atoms | `LinearPolynomial`, atom normalization | `theory/arithmetic/linear_polynomial.{hpp,cpp}`, `lra_atom.{hpp,cpp}` |
| TR Section 3.2.1 -- `Check`, `update`, `pivotAndUpdate`, "based on the dual Simplex algorithm and relies on Bland's pivot-selection rule to ensure termination" | `LraTableau::check()`, `update()`, `pivot()` (Primo splits `pivotAndUpdate` into `pivot` + `update`, which is why `check()` always calls them as a pair) | `theory/arithmetic/lra_tableau.{hpp,cpp}` |
| TR Section 3.2.3 -- assertion procedures | `LraTableau::assert_upper` / `assert_lower` | `lra_tableau.cpp` |
| Section 3.1 -- infinitesimal delta for strict inequalities | `DeltaRational` (`q + k*delta`, lexicographically ordered); no floating point anywhere | `lra_tableau.hpp` |
| Section 3.2 -- conflict explanation from row coefficients and the stuck variables' binding bounds | `LraTableau::build_conflict` | `lra_tableau.cpp` |
| TR Section 3.2.4 -- backtracking | `push()` / `pop(checkpoint)`, trailing **bounds only** as the report specifies; `PivotingRule::Soi` additionally trails assignments, pivots and its f-row | `lra_tableau.{hpp,cpp}` |
| Section 4 -- precomputing the tableau once per problem | `PreparedLraTheory` (mirrors `PreparedFlatTheory` for EUF) | `smt/check_sat.cpp` |
| Section 4.2 -- "deriving satisfied atoms" | `OnlineLraPropagator::scan_for_propagations`, over the atoms sharing a bound variable | `smt/check_sat.cpp` |
| Section 4.2 -- deriving *new* bounds from equations plus bounds (the slides' `x = y - z`, `y <= 2`, `z >= 3` gives `x <= -1`, explained by `y <= 2, z >= 3`) | `compute_implied_bound` / `compute_implied_child_bound`, **opt-in and off by default** -- `--lra-row-propagation`, `--lra-bidirectional-row-propagation` | `smt/check_sat.cpp` |

**A note on that last row, since it is a paper feature we do not enable.**
The slides list "Efficient Theory Propagation" as a headline contribution,
and the bound-derivation rule above is what it means. Primo implements it and
leaves it **off**, because a cluster sweep over the whole 500KB-5MB gap band
on 2026-07-31 found no cap setting that beats the baseline's solved count --
even with the mechanism firing 380x harder than at its default caps. See
[](../qf-lra-heuristics.md) section 6.5's D1 outcome. This is a measured
divergence from the paper on one benchmark family, not a claim that the
technique is generally worthless.

**DPLL(T) integration.** `OnlineLraPropagator` is a trail-synced
`CaDiCaL::ExternalPropagator`: one `LraTableau` per solve, kept in sync with
the SAT trail by `notify_new_decision_level` / `notify_backtrack` calling
`push()` / `pop()`. That is real cross-round trail reuse rather than a fresh
tableau copy each round. `find_incremental_lra_theory_conflict` (copy the
base tableau, re-assert every round) survives only as the fallback for builds
without the external-propagator API, under `#if !PRIMO_HAVE_IPASIR`.

The Check/Pivot repair is deferred to `cb_has_external_clause` rather than run
after every assigned literal -- verified against CaDiCaL's own source that
this hook fires once per quiescence point, and that `cb_propagate` is drained
before it, so Section 4.2 propagation is unaffected. See
`notify_assignment_lit`'s comment.

## Deviations from the paper

- **Bland's rule is not the default.** The paper selects both the violated
  and the entering variable by smallest index, which is what its termination
  argument rests on. Primo defaults to `PivotingRule::Sparse` (heuristics item
  H3) and also offers `Dantzig` and `Soi`. Termination is preserved by
  falling back to Bland selection past a per-call pivot threshold: a finite
  non-Bland prefix followed by a Bland suffix is still covered by the paper's
  own argument. The threshold (`4 * vars_.size() + 64`) is a heuristic, not a
  derived bound. `--lra-pivoting-rule bland` restores the paper's choice.
- **Backtracking now follows the paper** ([heuristics-note item H9](#h9), 2026-07-31, commit `ec6244e`).
  TR Section 3.2.4: "We just need to save the value of `u_i` (`l_i`) on a
  stack before it is updated... Backtracking does not require to save the
  successive betas on a stack... Since no pivoting or other expensive
  operation is used, backtracking is very cheap." Primo used to trail and
  undo bounds, assignments *and* every pivot's row rewrite, justified by an
  argument that was circular -- reverting the assignments is what forced
  reverting the pivots. It now trails bounds only. The soundness argument
  lives on `LraTableau`'s class comment.

  *Why the report is so emphatic about this.* TR Sections 2.2-2.3 are an
  argument against the incremental-Simplex solvers that preceded it --
  including SRI's own ICS and Simplics -- whose backtracking removes rows and
  "otherwise, a pivoting operation is applied first". Primo did not add and
  remove rows, but undoing pivots put it in the same family for the same
  reason. The scheme Primo removed is **Nelson's**: Rueß & Shankar
  (CSL-SRI-04-01, reference [15]) record that in Nelson's method "retraction
  of assertions is carried out by recording and undoing pivoting steps". See
  [](lra-related-work.md).

  **`PivotingRule::Soi` keeps the old scheme.** Its objective row is a sum
  over the *currently violated* rows, not a linear combination of `A`, so
  neither half of the argument covers it.

  *Result, jobs 118043-118046:* on the 595-file scrambled competition set at
  60s, **378 -> 399 solved** and PAR2 28691 -> 26661; on the 237-file
  stratified sample, 192 -> 197. Zero disagreements, zero wrong answers.
  The mechanism is that not undoing pivots lets the search keep the repair
  work it already paid for: pivot counts fall 2.3-5.3x on the profiled
  instances, and `rebuild_occurs_in_index` leaves the profile entirely
  because nothing structural changes on a pop.

  *It is not a free win, unlike item H8.* Churn is two-directional
  (+32/-11 here, +7/-2 on the stratified sample), and among the 145
  commonly-solved files over a second, **62 are more than 10% slower and 64
  more than 10% faster** -- median 1.01x, p25 0.63, p75 1.30, tails from
  0.10x to 24.9x. It nets +21 because the wins land on instances near the
  timeout. That variance is inherent rather than a defect: keeping the basis
  across a backtrack keeps one that suited the *previous* subproblem, which
  is sometimes a warm start and sometimes worse than starting fresh.

- **Real equality is lowered, not handled in the tableau.** `t1 = t2` becomes
  `(t1 <= t2) AND (t2 <= t1)` -- sound because Real is a total order -- so
  `NOT(t1 = t2)` reduces to ordinary Boolean structure that
  `BooleanClausifier` already clausifies, and CDCL branching over the two
  `Le` atoms performs the case split for free. No case-split mechanism was
  added to `LraTableau`. The paper does not cover this: its theory-atom
  framing assumes DPLL(T) hands the solver each atom's truth value, which
  sidesteps how a compound relation decomposes at the Boolean level. This is
  a frontend/encoding decision, not a departure from the tableau algorithm.
  The lowering runs as the final preprocessing step (`lower_real_equalities`),
  not at parse time, so solve-eqs and the other `Equality`-aware passes still
  see genuine Real equalities. See [](../preprocessing.md).
- **`pop()` rebuilds the `occurs_in_` reverse index** rather than diffing it
  incrementally -- once per `pop()` call, not once per pivot undone. An
  incremental version was built and measured on 2026-07-31 and is *slower*
  (1.4-1.8x); see [](../qf-lra-heuristics.md) section 6.2b. Note this index
  exists only to serve the explicit tableau, so it would disappear entirely
  under the paper's backtracking scheme above.
- **Model extraction is implemented for the supported model fragments.** TR
  Section 3.3's second half converts a satisfying β' over the
  delta-rationals into a concrete rational assignment by computing
  `delta_0 = min{(d_i - c_i)/(k_i - h_i) | c_i < d_i, k_i > h_i}` and
  evaluating each variable at it. The QF_LRA path chooses a positive rational
  delta that preserves its asserted bounds. Mixed QF_UFLRA model
  reconciliation also sorts the distinct shared values and applies the same
  crossing bound to adjacent values, which is sufficient to preserve their
  complete order in `O(S log S)` time. `get-model` is supported for the
  fragments documented in [](../arithmetic.md); `get-value` remains
  unsupported.

## Extensions beyond the paper

Each of these is measured in [](../qf-lra-heuristics.md); its own source, where
it has one outside this paper, is in [](lra-related-work.md).

- **Scale-canonical slack keys** (item H1b). Slack variables are deduplicated
  by a scale-invariant key, so `x <= 5` and `2x <= 10` share one tableau
  variable and one row. Section 4.2 propagation only relates atoms sharing a
  variable, so this is what gives it anything to relate.
- **Direct bounds for single-variable atoms** (item H1a). An atom over one
  variable bounds that variable directly instead of allocating a slack and a
  row for it. `bound_assertion()` is the single place the four
  relation/polarity combinations are interpreted.
- **Two-tier rationals** (item H2). `util::Rational` keeps a machine-word
  numerator/denominator with `__int128` intermediates and promotes to GMP only
  on overflow, maintaining the invariant that the big form is used exactly
  when the value does not fit the small one.
- **Fill-in-aware pivot selection** (item H3, `PivotingRule::Sparse`, the
  default). Prices what a pivot *costs* -- the Markowitz product of
  `|occurs_in(entering)|` and `|row(leaving)|` -- rather than what it
  achieves. Source: Monniaux (CAV 2009) p.8.
- **Sum-of-infeasibilities pivoting** (`PivotingRule::Soi`, opt-in). A global
  objective across every out-of-bound basic variable, with QuickXplain-style
  minimization of the resulting multi-row conflicts. Source: King, Barrett,
  Dutertre (FMCAD 2013).
- **Row-based and bidirectional bound propagation** (opt-in). Derives a row's
  implied bound from its terms' bounds, and the reverse direction: tightening
  one term from the row's own bound plus the remaining terms. This is the
  generalization of Section 4.2 across *different* variables.
- **Theory-guided decisions** (opt-in, `cb_decide`). The theory supplies the
  decision literal instead of leaving every decision to VSIDS.
- **Fixed-variable elimination** (item H7). A basic variable whose bounds
  coincide is pivoted out of the basis immediately, because a fixed *non-basic*
  variable is already inert -- `can_enter()` rejects it on every branch -- while
  a fixed *basic* one demands a repair pivot every time any term in its row
  moves. **The term is the authors' own** -- TR Section 3.1: "If `l_j = u_j`
  then `x_j` is called a *fixed variable*" -- but they only define it; doing
  anything with one during search is not in either version of the paper. The
  quantities exploited are ours (`:lra.fixed-variables`,
  `:lra.elimination-pivots`).
  Prior art is LP presolve rather than this paper -- Andersen & Andersen,
  *Presolving in linear programming* (1995); see [](lra-related-work.md).
  **Ships opt-in** (`--lra-fixed-elimination`): it nets to nothing on the
  595-file scrambled set. An earlier version also *froze* qualifying rows out
  of the working set; that half was strictly negative and was deleted
  outright, along with its `mark_boundable` guard and its
  `:lra.eliminated-rows` counter.

## Not implemented

- **Presimplification (TR Section 3.1).** Since every atom is known before
  search, the constraints `Ax = 0` can be simplified by "removing any
  variable `x_i` that does not occur in any elementary atom", by Gaussian
  elimination; the authors report it "can reduce the matrix size
  significantly" and call the resulting speedup substantial. Primo does not
  do this. Note what it is *not*: `--gaussian-elimination` here solves
  asserted linear **equality systems** feeding solve-eqs, a different pass.
  - **Measured 2026-07-31, and it is not a lead.** Throwaway instrumentation
    (added, measured, reverted) counted prepared-tableau variables bounded by
    no atom -- exactly the paper's criterion:

    | instance | variables | free (in no atom) | share | rows holding >=1 free var |
    |---|---|---|---|---|
    | `scrambled339450` | 57,658 | 2,503 | 4.3% | 43,140 |
    | `scrambled312090` | 26,041 | 119 | 0.46% | 17,812 |
    | `scrambled228965` | 5,694 | 28 | 0.5% | 2,999 |

    So the pass would remove **0.5-4.3%** of variables. Worse, those few are
    spread across nearly every row -- 91% of rows on `scrambled339450` hold
    at least one -- and the elimination is *Gaussian*, so substituting them
    out rewrites those rows and generates fill-in. That is the mechanism
    section 6.2 identifies as the bottleneck in the first place. A pass that
    shrinks the matrix by 4% while densifying 91% of its rows is not a
    trade this band can afford.
  - Sanity check on the measurement: every free variable found was
    *non-basic* (basic count zero on all three). That is the expected shape
    -- a slack exists only because some polynomial had an atom, so slacks are
    always bounded, and the free variables are exactly the structural ones
    that never received a direct bound.
  - The earlier inference from H1a and the ~5% dimension match against z3
    pointed the same way; this replaces it with a direct count.
  - The paper's name for a variable with no bounds at all is a **free
    variable** (`l_j = -inf`, `u_j = +inf`); that predicate exists here as
    `LraTableau::is_unbounded`, used for H7's entering-candidate preference.
- **Devex and steepest-edge pricing.** Surveyed in [](lra-related-work.md);
  `Dantzig` covers the cheap end of that survey and `Sparse` optimizes a
  different quantity entirely.
- **Offset equalities (`x - y = c`) as a column merge.** Designed as the
  follow-on to item H7 in [](../qf-lra-heuristics.md); not attempted yet.
- **Explicit Farkas coefficients.** `build_conflict` produces the *literals*
  of a Farkas-style certificate, not the numeric multipliers, so nothing here
  emits a checkable arithmetic proof.
- **Difference logic (QF_IDL/QF_RDL).** `difference_logic.hpp` is an empty
  placeholder; item H6 sketches a fast path for it.
