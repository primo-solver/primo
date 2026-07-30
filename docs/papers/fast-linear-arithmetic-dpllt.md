# A Fast Linear-Arithmetic Solver for DPLL(T)

## Citation

Bruno Dutertre, Leonardo de Moura:  
A Fast Linear-Arithmetic Solver for DPLL(T).

## Role in Primo

This paper is the base algorithm for Primo's QF_LRA support: the
bounded-variable simplex tableau, its Assert-Bound/Check/Pivot loop, the
infinitesimal-delta encoding of strict inequalities, Farkas-style conflict
explanation, and the backtracking and propagation that make it a DPLL(T)
theory solver.

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
| Section 3 -- Assert-Bound, Check, Pivot | `LraTableau::assert_upper/assert_lower`, `check()`, `pivot()` | `theory/arithmetic/lra_tableau.{hpp,cpp}` |
| Section 3.1 -- infinitesimal delta for strict inequalities | `DeltaRational` (`q + k*delta`, lexicographically ordered); no floating point anywhere | `lra_tableau.hpp` |
| Section 3.2 -- conflict explanation from row coefficients and the stuck variables' binding bounds | `LraTableau::build_conflict` | `lra_tableau.cpp` |
| Section 4 -- backtracking | `push()` / `pop(checkpoint)`, trail-based (one `TrailEntry` per bound, assignment, pivot, SOI f-row and row-freeze change, replayed backwards) | `lra_tableau.{hpp,cpp}` |
| Section 4 -- precomputing the tableau once per problem | `PreparedLraTheory` (mirrors `PreparedFlatTheory` for EUF) | `smt/check_sat.cpp` |
| Section 4.2 -- "deriving satisfied atoms" | `OnlineLraPropagator::scan_for_propagations`, over the atoms sharing a bound variable | `smt/check_sat.cpp` |

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
- **Pivots are undone on `pop()`.** The paper notes that which basic/non-basic
  partition is current is an internal representation choice, not something
  tied to search depth. Primo still has to undo a pivot's row rewrite,
  because leaving it in place while reverting the bounds and assignments it
  was performed under would break the invariant that every basic variable's
  row equation holds for the current assignment. See `LraTableau`'s class
  comment.
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
  incrementally -- once per `pop()` call, not once per pivot undone.

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
  moves. Where the resulting row provably constrains nothing it is also frozen
  out of the working set, under a guard (`mark_boundable`) that is what keeps
  that half sound. Nothing in the paper covers fixed variables; the
  quantities being exploited are its own (`:lra.fixed-variables`,
  `:lra.eliminated-rows`, `:lra.elimination-pivots`).

## Not implemented

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
