# Arithmetic

`OnlineLraPropagator` and `ComponentLraExecution` now read a shared SAT assignment
view. They retain tableau checkpoints, bound/explanation caches, and pending
consequences; they own no Boolean assignment array or literal trail. Backtracking
receives the exact removed literals and preserves reasons for retained
propagations. See [the ownership protocol](dpllt.md#shared-assignment-protocol-implementation-underway)
for lifetime, model-scope deferral, and qualification status.

This is the Real-arithmetic reference and historical development record.
Integer completion is implemented in [](qf-lia-branch-and-bound.md), and mixed
integer combination in [](n-way-combination-migration.md). For the current
five-logic qualification and limitations, use [](current-status.md); the dated
samples below are not measurements of the current branch.

QF_LRA (quantifier-free linear real arithmetic) is implemented through
Milestone 4 of the plan described below: a full SMT-LIB QF_LRA frontend
plus a bounded-variable Simplex theory solver integrated into DPLL(T) via a
trail-synced IPASIR-UP external propagator, with both conflict detection and
same-variable bound propagation, following Dutertre & de Moura's "A Fast
Linear-Arithmetic Solver for DPLL(T)". **Supported**, not just
experimental, as of the 2026-07-24 campaign-tier fuzz gate (3000 fuzzsmt
cases plus opfuzz/yinyang mutation seeded from real benchmarks, all clean
-- see `docs/run-log.json`'s Milestone 4b entries and Milestone 4b below).

**Historical sample (2026-07-29).** On the 237-file stratified sample
(`benchmarks/qf-lra-stratified-sample.txt`, 10s timeout) Primo solves
**168/237 with PAR2 1538.9s**, against z3's 167/1531.6s and cvc5's
150/1887.1s -- up from 108 at the start of the 2026-07-27 heuristics round.
The four items that got it there are H2 (two-tier rationals), H1a (direct
bounds for single-variable atoms), H1b (scale-canonical polynomial keys) and
H3 (fill-in-aware pivot selection, now the default rule); each is described
in its own section below, and [](qf-lra-heuristics.md) has the design
rationale and the per-step progression.
Difference logic (QF_IDL/QF_RDL) remains unimplemented; `difference_logic.hpp`
is an empty placeholder reserved for that, separate future work.

See [](papers/fast-linear-arithmetic-dpllt.md) for the base algorithm and
[](papers/lra-related-work.md) for a literature survey of extensions and
alternatives considered before/while implementing it.

## Theory-literal language

The ordinary arithmetic theory boundary is narrower than the surface QF_LRA
grammar. After preprocessing and Boolean clausification, the LRA integration
accepts SAT-assigned atoms of these two forms:

```text
t1 <= t2
t1 <  t2
```

Here `t1` and `t2` are Real-sorted linear terms built from rational constants,
Real constants, addition, and multiplication or division by a rational
constant. The frontend canonicalizes `t1 >= t2` to `t2 <= t1` and `t1 > t2`
to `t2 < t1`; chainable comparisons become conjunctions of adjacent binary
atoms. With `--arithmetic-atom-canonicalization`, an early arithmetic-atom pass
scales every linear direction to primitive integer coefficients and a
deterministic sign. Thus proportional source predicates are hash-consed before
clausification; the option can be combined with `--preprocessing-mode=off` but
defaults off pending stronger performance evidence. Nonlinear multiplication
and division by a nonliteral term are outside the accepted fragment.

The integer operators `div`, `mod`, and `abs` are accepted as a **deliberate
extension**, not as conformance. They belong to the SMT-LIB `Ints` theory, but
both QF_LIA and QF_UFLIA exclude them by name -- QF_LIA requires Int terms to
have "no occurrences of the function symbols `**`, `/`, `div`, `mod`, and
`abs`", and its `:extensions` clause readmits only `*` with concrete
coefficients. So no logic Primo supports permits them, and rejecting them was
correct on the standard's terms; Primo accepts them because the solvers it is
compared against do, and because rejecting them leaves the operators
unreachable everywhere. Dolmen enforces the logic and will refuse to
model-check such a file.

`frontend.cpp` builds `IntDiv`, `IntMod` and `Abs` terms, which the
`lower-extended-terms` leaf encodes; a non-constant divisor is reported as
nonlinear and a literal zero divisor is rejected outright. The QF_UFLIA real-seed mutation gate is where these turned up: of 165
unsupported mutants in job 131463, 44 were `div`, 40 `abs` and 29 `mod`. Those
mutants are themselves outside their declared logic -- typefuzz mutates
operators under `--keep-logic` and does not respect the `:extensions` clause --
so rejecting them was defensible.

Supporting the operators did **not** unblock that gate, which was the other
argument for the work. Job 131564, the same gate after the change, still
reports 157 unsupported mutants of 200 and an effective yield of 22.8% against
the roughly 25% before. The reason is that typefuzz substitutes operators
without regard to linearity: of the 157, 68 are `mod` and 43 are `div` with a
**non-constant divisor**, and 46 are nonlinear `*`. All are genuinely outside
the linear fragment and correctly rejected, so the failure simply moved from
"unknown function symbol" to "nonlinear divisor". Only about 8 of the original
165 became answerable. The remaining justification for `div`/`mod`/`abs` is
usability, and that is all. The remaining unsupported mutants
are correct rejections: nonlinear multiplication, and files Z3 rejects too
because a mutation left a `let` variable unbound.
[](papers/integer-division-and-modulo.md) surveys what the standard requires
and what the literature does and does not say. In
QF_UFLRA, mixed purification first replaces each
Real-valued UF application occurring in arithmetic with a fresh Real proxy, so
the prepared LRA atom contains no UF application node.

`BooleanClausifier` owns the mapping from each `Le` or `Lt` term to a SAT
literal. The LRA side receives the term together with the SAT assignment's
truth value and blocking literal; it does not receive a syntactic `not`.
`normalize_atom()` converts the comparison to a polynomial bound `p <= c` or
`p < c`, and `PreparedLraTheory` installs either a direct bound on one Real
variable or a bound on a slack variable for a compound polynomial. The
assignment polarity then selects exactly one tableau bound:

| Core atom | SAT value | Bound asserted to LRA |
| --- | --- | --- |
| `p <= c` | true | `p <= c` |
| `p <= c` | false | `p > c` |
| `p < c` | true | `p < c` |
| `p < c` | false | `p >= c` |

Reversing a comparison during polynomial scale canonicalization reverses the
upper/lower direction in the same way. Strict bounds use the exact
`DeltaRational` representation from Dutertre and de Moura, Section 3.1: for
example, false `p <= c` is the lower bound `c + delta`, while true `p < c` is
the upper bound `c - delta`.

(lra-equality-and-disequality)=
### Equality and disequality

Real equality is deliberately **not** part of the SAT-visible arithmetic-atom
language. It remains a genuine `TermKind::Equality` through the equality-aware
preprocessing passes. At the final equality-producing preprocessing boundary,
the mandatory `lower_real_equalities()` pass rewrites

```text
t1 = t2  ==>  (t1 <= t2) and (t2 <= t1).
```

This happens before mixed purification, Boolean clausification, and theory
dispatch, not during parsing. The ordering lets solve-eqs and related passes
consume Real equalities and also ensures that equalities generated by those
passes are lowered before LRA sees them.

SMT-LIB disequality is `not (= t1 t2)`; `distinct` is expanded to pairwise
negated equalities. After equality lowering, a Real disequality therefore has
the Boolean form

```text
not ((t1 <= t2) and (t2 <= t1)).
```

SAT performs the required case split. Whichever non-strict inequality is
assigned false reaches LRA as a strict separating bound, so the tableau needs
no native `!=` relation.

There is one separate, internal combination input. In QF_UFLRA, the
`TheoryCombinationManager` may register an equality between shared Real terms
after the ordinary lowering pass. When SAT assigns such an interface equality
true, the mixed final check layers the native `Equality` atom onto
`PreparedLraTheory`; `LraRelation::Eq` then asserts both the upper and lower
bound at the same value. A false interface equality is not sent to LRA as a
native false-`Eq`: that input would be the non-convex disjunction `t1 < t2 or
t2 < t1`, which is not a single tableau bound. Thus the complete prepared-LRA
input contract is SAT-assigned `Le`/`Lt` atoms plus combination-internal,
known-true shared equalities. Native false equality is rejected defensively.

The optional `--guarded-real-equality-lowering` changes only the Boolean shape
of one guarded case to avoid an extra Tseitin variable; it does not change this
contract. See [](real-equality-lowering) for the precise pass ordering and
guarded rewrite.

(tableau-sparse-representation)=
## Tableau sparse-row and occurrence representation

`LraTableau` owns two views of the current sparse tableau. For every current
basic variable `b`, `rows_[b]` is a contiguous `Row` containing the nonzero
right-hand-side coefficients in

\[
x_b = \sum_j a_{bj}x_j.
\]

The entries are strictly ordered by `var`, with at most one entry per variable:

```cpp
struct RowEntry {
  LraVarId var;
  util::Rational coeff;
  std::uint32_t occ_slot;
};
```

The reverse view, `occurs_in_[v]`, is an unordered vector of the basic variable
IDs whose rows currently contain `v`. Therefore, for every entry `e` in
`rows_[b]`, the cross-link invariant is

```cpp
occurs_in_[e.var][e.occ_slot] == b
```

`occ_slot` is thus an index into the **column list** `occurs_in_[e.var]`. It is
not the entry's position inside `rows_[b]`, a variable ID, or an occurrence
count. Conversely, because each row contains a variable at most once, every
element `b` of `occurs_in_[v]` identifies exactly one entry for `v` in
`rows_[b]`.

This is Primo's array-backed, one-direction subset of the doubly cross-linked
sparse tableau in King, Section 2.2.7 ([](king-thesis)). King links each nonzero
into both its row and column and leaves both lists unsorted. Primo keeps rows
sorted and contiguous for linear two-way row merging, so only the row entry
points into the column; the column entry stores a basic-variable ID rather than
a direct row position.

Maintenance follows the ownership of the column entry:

- inserting a new row occurrence appends `b` to `occurs_in_[v]` and stores the
  old column size as the new entry's `occ_slot`;
- rewriting or moving a surviving entry within its row preserves `occ_slot`,
  because its element in `occurs_in_[v]` did not move;
- removing an occurrence swap-pops its column element, then updates the one row
  entry whose basic-variable ID moved into the vacated column slot;
- rebuilding the occurrence index, including after restoration from the trail,
  rewrites every `occ_slot` while repopulating the columns.

Rows remain sorted, but column lists deliberately do not: swap-pop removal
destroys column order. `occurs_in_index_matches_full_rebuild()` independently
reconstructs the columns and also checks the cross-link equation above. The
tests call it after pivots, exact coefficient cancellation, and backtracking,
because a stale slot normally produces a later wrong row update rather than an
immediate failure. `occ_slot` is 32-bit to limit row-entry growth; the current
representation consequently assumes that one variable occurs in fewer than
$2^{32}$ basic rows.

```{figure} _static/lra-tableau-occurrence-index.svg
:name: lra-tableau-occurrence-index
:alt: Sparse tableau rows with occ_slot back-links into occurs_in column lists, followed by a swap-pop removal and repair of the moved entry.

The current sparse-row and reverse-occurrence representation. The paired gold
markers instantiate the cross-link invariant for `x0`; the lower inset shows why
swap-pop removal must repair the surviving entry that moved within the column.
The diagram abbreviates the implementation field `occurs_in_` as `occ`.
The editable TikZ source is `docs/figures/lra-tableau-occurrence-index.tex`.
```

## Implemented (Milestones 1-4, supported)

- **Grammar/frontend** (`src/smt/frontend.cpp`): `(set-logic QF_LRA)`,
  `Real`-sorted declarations, `+ - * <= < >= > =` per the SMT-LIB QF_LRA
  grammar (verified against the spec, not assumed) -- n-ary `+`/chainable
  comparisons, arity-disambiguated `-`, `*`/`/` requiring a literal
  operand (with the literal recognized after `build_term`, so let-bound
  aliases and constant-folded sub-expressions work, not just raw numeral
  tokens). Before mandatory lowering, a surface `Real`-sorted `=` is classified
  as a QF_LRA equality rather than an EUF atom --
  `contains_equality`/`contains_arithmetic_atom` classify by operand sort.
- **Term representation** (`src/core/term.hpp`, `term_manager.{hpp,cpp}`):
  `SortKind::Real`; `TermKind::{Rational, Plus, Times, Le, Lt}`. `Times` is
  a *unary* scale node (coefficient * one child), not general
  multiplication -- see the TermKind comment for why (a coefficient has to
  attach to some node; repeated addition can't represent non-integer
  coefficients).
- **Exact rationals** (`src/util/rational.{hpp,cpp}`): a two-tier
  representation -- numerator/denominator held inline as `std::int64_t` when
  they fit, falling back to a heap `mpq_class` (GMP, vendored via
  `contrib/install_gmp.py`, `make contrib-gmp`) when they don't. See
  "Two-tier rational representation" below.
- **Normalization** (`src/theory/arithmetic/linear_polynomial.{hpp,cpp}`,
  `lra_atom.{hpp,cpp}`): `LinearPolynomial` (sum of coeff*var + constant,
  Section 2's normal form), `LraAtom` (canonical "t ~ c" form, Section 2).
- **Decision procedure** (`src/theory/arithmetic/lra_tableau.{hpp,cpp}`):
  `LraTableau` implements Assert-Bound, Check/Pivot (Bland's rule, for
  termination), and Farkas-certificate conflict explanation (Section 3,
  3.2). Strict inequalities via `DeltaRational` (q + k*delta, exact, no
  floating point). Incremental: `push()`/`pop(checkpoint)` are genuinely
  trail-based. The default non-SOI path trails bounds only: `pop` relaxes
  bounds while retaining the feasible assignment and current basis, following
  the technical report's Section 3.2.4 and the implemented H9 design below.
  SOI is the exception: its basis-dependent objective retains full
  bound/assignment/pivot rollback. Do not mix assignment rollback with a
  retained basis. The original full-trail prototype is historical, not the
  current default.
- **DPLL(T) wiring** (`solver_engines.cpp`): `PreparedLraTheory` precomputes the
  tableau's structural part exactly once per prepared `check-sat` problem,
  before any theory call and independently of any SAT assignment. It sees the
  complete theory-atom list and allocates one stable slack variable and row per
  *scale-canonical compound polynomial*, not per atom. Every corresponding
  `PreparedAtom` then stores the same `bound_var`, so an atom encountered in
  any later callback refers to the same tableau variable. Slacks are therefore
  global to the prepared problem, not allocated afresh for each theory check.
  The online path copies this base tableau once and trails bounds and pivots;
  the fallback may copy it for a check, but preserves the prepared variable
  numbering and atom-to-variable map. A **single-variable** atom gets no slack
  and no row at all -- it is a bound on that variable itself (see "Direct
  bounds for single-variable atoms" below). The `atoms_by_bound_var` index,
  also built once, records every atom that can bound each stable tableau
  variable.
  `OnlineLraPropagator` (Milestone 3a) is a trail-synced
  `sat::ExternalPropagator`, owning one `LraTableau` (copied once from
  the prepared base) kept in sync via `notify_new_decision_level`/
  `notify_backtrack` calling `LraTableau::push()`/`pop()` -- this is what
  actually exercises push/pop for cross-round reuse. `solve_qf_lra_engine_impl` always
  prefers this when the backend reports external-propagator support
  (`#if PRIMO_HAVE_IPASIR`); `find_incremental_lra_theory_conflict` (copy
  the base tableau and reassert every round, no push/pop reuse) survives
  only as the fallback when it isn't.
- **Typed arithmetic models** (`solver_engines.cpp`, `typed_model_builder.cpp`,
  and `smtlib/printer.cpp`): when a model is requested, exact values are
  read from the final
  `LraTableau` assignment and strict `DeltaRational` values are concretized
  by choosing a positive rational delta small enough to preserve the asserted
  strict bounds. The model builder also replays preprocessing reconstruction
  equalities from solve-eqs/Gaussian elimination, so a constant eliminated
  before theory solving is assigned from the final values of the terms it was
  rewritten to. The mixed QF_UFLRA path supplies compatible numeric assignments
  and EUF facts to the same typed adapter, not concatenated model strings.
  It supports UF symbols with Real arguments and Real-valued applications after
  purification. Its coordinator additionally
  preserves the ordering of all distinct shared `DeltaRational` values while
  choosing the concrete delta, using the adjacent-value form of Dutertre and de
  Moura Section 3.3 in `O(S log S)` time. This prevents a concrete model from
  collapsing Real arguments that the accepted EUF model distinguishes.
  One frozen `core::Model` owns constants and evaluated-argument function tables;
  the session checks original assertions and prints it separately. See
  [](model-interpretation.md). Arrays, nonlinear arithmetic and the complete
  paper `DPLL(T^M)` transition system remain unsupported; model-guided
  combination itself is implemented for the documented mixed fragments.
- **Model-based SAT phase hints** (heuristics item H5, `solver_engines.cpp`,
  **opt-in** via `CheckSatOptions::lra_model_phase` / `--lra-model-phase`):
  `OnlineLraPropagator` keeps the SAT backend's variable choice but refreshes
  arithmetic atom polarities from the current tableau assignment β at
  quiescence points. For each arithmetic atom, it evaluates the atom's TRUE
  bound using the same `DeltaRational` `bound_assertion()` representation
  used by Assert-Bound and same-variable propagation, then calls
  `IpasirUpBackend::phase()` only when the preferred literal changed since
  the previous refresh. This is a search hint only; it does not add clauses,
  propagations, or theory decisions.
  *Attribution:* designed independently here (2026-08-04). The published
  description is SPASS-SATT's **decision recommendation** -- Bromberger, Fleury,
  Schwarz and Weidenbach, *SPASS-SATT: A CDCL(LA) Solver*, CADE 2019, §3 and
  footnote 3; DOI
  [10.1007/978-3-030-29436-6_7](https://doi.org/10.1007/978-3-030-29436-6_7).
  Their rule is a disjunction, and its second disjunct is unreachable whenever
  unate propagation is on, so **this is their technique, not half of it**, and
  their +129 on QF_LIA is a measurement of this mechanism. The flag is off by
  default and has only been evaluated on QF_LRA; see
  [lead 20](lead-decision-recommendation). Full attribution chain,
  including what *not* to cite, in [](h5-attribution).
- **QF_LRA fragment boundary** (`Frontend::declare_fun`): QF_LRA accepts free
  constants, but rejects non-nullary `declare-fun` declarations. Uninterpreted
  functions or predicates over arithmetic terms are QF_UFLRA/NO work, not pure
  QF_LRA.
- **Same-variable bound propagation** (Milestone 3b, `solver_engines.cpp`; called
  same-*slack* propagation before item H1a, when a slack was the only thing a
  bound could live on): after each successful bound assertion,
  `OnlineLraPropagator` scans the other atoms bounding that same tableau
  variable (via `atoms_by_bound_var`) and eagerly propagates any now entailed
  true/false, using `LraTableau::upper_bound()`/`lower_bound()` (new read
  accessors) compared against the `DeltaRational` `bound_assertion()` says
  that atom's own TRUE/FALSE case would assert -- Dutertre & de Moura Section
  4.2's "deriving satisfied atoms". Each propagation's reason is exactly the one bound's
  own asserting literal (a 2-literal Horn clause), computed eagerly at scan
  time rather than lazily re-derived, since the check is exact and cheap
  (no candidate/budget/backoff machinery like EUF's propagation needs).
  Gated behind `CheckSatOptions::lra_propagation` (default on),
  `--lra-propagation`/`--no-lra-propagation` CLI flags.
  **Also known as *unate propagation***, which is the name Dutertre and de Moura
  use for it in §3.2.5 of the CSL-06-01 technical report ("if bound `xi >= ci`
  has been asserted then any unassigned atom of the form `xi >= c'` with
  `c' < ci` is immediately implied. Similarly, the negation of any atom
  `xi <= u` with `u < ci` is implied"), and the name it goes by in SPASS-SATT
  and cvc5. The alias is recorded here because searching the tree for "unate"
  otherwise returns nothing and invites the false conclusion that Primo lacks
  the technique -- which it did once, on 2026-08-29; see
  [](h5-attribution).
  Calibration, for anyone tempted to tune it: SPASS-SATT measures unate
  propagation at **exactly zero** on both divisions (1607 vs. 1608 QF_LRA, 6806
  vs. 6806 QF_LIA) and keeps it on by default anyway, on a stated assumption
  about theory combinations rather than a measurement
  ([](papers/bromberger-linear-arithmetic-thesis.md) §8.1). Against that,
  cvc5's own counters on a Primo dramatic-gap file show
  `unatePropagateCalls = 15,595` against `pivots = 129` (below). Primo has never
  A/B'd `--no-lra-propagation`; it is cheap to.
- **Row-based/interval bound propagation** (competitiveness-roadmap item 2,
  2026-07-25, `check_sat.cpp`): generalizes same-slack propagation to a
  *derived* rather than only directly-asserted bound. For a row `basic_var
  = sum(coeff_j * var_j)`, `compute_implied_bound` sums each term's own
  maximum/minimum contribution (`var_j`'s upper bound if `coeff_j` is
  positive, lower if negative, for the implied-upper case; symmetric for
  implied-lower) into an implied interval for `basic_var`, then checks it
  against `atoms_by_bound_var[basic_var]` exactly like same-variable
  propagation does. Triggered from the same `notify_assignment_lit` hook,
  right after the same-variable scan, on whatever variable was just
  tightened. *(Originally, bounds could only ever live on slacks, so a
  row's term variables needed an extra `single_variable_slack` indirection
  to find their current bound at all, and the trigger only fired for slacks
  wrapping exactly one coefficient-1 variable. Item H1 removed both: a row
  term's bound is now read directly, which is simpler and strictly more
  reaching.)* Reasons are genuinely multi-literal
  (one antecedent per row term used, unlike same-slack propagation's
  single-literal reason) -- `OnlineLraPropagator`'s `pending_`/`reasons_`/
  `cb_add_reason_clause_lit` were widened from a single `sat::Lit` to
  `vector<sat::Lit>` to carry this (same-slack entries become singleton
  vectors, unchanged behavior). Two defensive, non-adaptive caps bound the
  O(row size) per-affected-row cost this technique has that same-slack
  propagation's O(atoms sharing a slack) doesn't: `--lra-row-propagation-max-row-size`
  (default 64) skips deriving a bound for any row longer than this, and
  `--lra-row-propagation-max-fanout` (default 64) caps how many rows a
  single bound change may trigger a scan over -- a variable occurring in
  more rows than this propagates nothing that round. Both live in
  `CheckSatOptions::lra_row_propagation_limits` and actually bind as of
  2026-08-10; before that the two CLI options parsed but were silently
  ignored in favor of now-removed hardcoded constants of the same values
  (`docs/tuning-parameters.md`'s "dead knobs" finding). Reported
  by `:lra.row-propagations`, alongside the existing aggregate
  `:lra.theory-propagations`. **Opt-in**
  (`CheckSatOptions::lra_row_propagation = false`,
  `--lra-row-propagation`), only takes effect when `lra_propagation` is
  also enabled. Cleared merge-gate-tier fuzz smoke (500 fuzzsmt QF_LRA
  cases, 2026-07-25; 500 more with bidirectional also on, 2026-07-26) and
  the 2026-07-31 raised-cap sweep's 2,000-case fuzz gate (job 117767, 0
  wrong answers), plus a ~4,000-case mixed QF_UFLRA campaign with it
  enabled (2026-08) -- see `docs/run-log.json`. What is still missing is a
  dedicated pure-QF_LRA, default-cap, campaign-tier (>=3000-case) run of
  the kind that gates promotion to default-on; see
  [](qf-lra-equality-sharing.md). Only derives a row's *own* implied bound and checks its
  *own* slack's atoms -- rearranging a row to tighten one of its
  *children's* bounds from the basic variable's own bound (Section 4's
  fuller bidirectional treatment) is the separate, since-implemented
  competitiveness-roadmap item 7 below, `compute_implied_child_bound` /
  `--lra-bidirectional-row-propagation`.
- **Selectable pivoting rule** (Milestone 4a, `lra_tableau.{hpp,cpp}`):
  `LraTableau::PivotingRule::{Bland, Dantzig}`. Dantzig picks the
  most-violated basic variable and largest-`|coefficient|` entering
  variable in `check()`'s Check/Pivot loop -- cheaper per pivot and faster
  per-instance than Bland's smallest-index selection (per
  `docs/papers/lra-related-work.md`'s survey), but with no anti-cycling
  guarantee on its own, so `check()` falls back to Bland's-rule selection
  once a heuristic pivot-count threshold is exceeded *within that call*.
  A finite Dantzig prefix followed by a Bland's-rule suffix still
  terminates (the paper's termination argument applies to the suffix on
  its own). Opt-in via `CheckSatOptions::lra_pivoting_rule`
  (`--lra-pivoting-rule {bland,dantzig,soi,sparse}`) -- **default was Bland
  at the time**, the already campaign-fuzzed behavior (superseded 2026-07-29:
  the default is now `sparse`, see "Fill-in-aware pivot selection" above); `kStatLraBlandFallbacks` reports how
  often the fallback engages.
- **SOI-Simplex pivoting** (competitiveness-roadmap item 4, 2026-07-25,
  King/Barrett/Dutertre FMCAD 2013 "Simplex with Sum of Infeasibilities
  for SMT", `lra_tableau.{hpp,cpp}`): `PivotingRule::Soi` replaces
  Bland/Dantzig's single-row objective with a global one -- minimize the
  sum of infeasibilities across *every* out-of-bound basic variable at
  once, addressing Dutertre-de Moura's own documented weak point (no
  global progress criterion). Full paper algorithm, not a scoped-down
  version:
  - An incrementally-maintained "f-row" (`f_var_`, a real extra
    `LraVarId`/row so `pivot()`'s existing row-rewrite logic keeps it
    correct across basis changes for free) tracks `sum(d_i * row_i)` over
    every currently-violated basic variable, updated via
    `set_soi_direction()` whenever a basic variable's direction (`d_i`,
    eq 3) flips -- hooked into `assert_upper`/`assert_lower` (bound
    tightened while already basic), `update()` (assignment changed via
    propagation), and `pivot()` (leaving variable resets to the nonbasic
    baseline). This trail/incremental integration is genuinely new
    design beyond the paper (which only describes one non-incremental
    `Check` call, not DPLL(T) backtracking) -- a new `FRowTrailEntry`
    trail variant undoes it on `pop()`.
  - `soi_select()` (Section V-A) picks the entering variable/leaving
    row/step size via the paper's own breakpoint-sorted `ΔVio(j,δ)`
    walk, not a cheaper approximation.
  - `build_soi_conflict()`/`minimize_soi_conflict()` (Section V-B) handle
    the case where several rows are simultaneously stuck: a multi-row
    conflict generalizing the existing single-row `build_conflict`, then
    minimization via `soi_subset_still_conflicts()`'s restricted-objective
    oracle. The algorithm is selectable
    (`--lra-soi-minimize {quickxplain,deletion,none}`, default the original
    QuickXplain of Junker 2001), and deletion takes an ordering
    (`--lra-soi-minimize-order {id,row-support,violation}`).

    The reason for a choice here is *not* speed. Instrumented over 2253
    calls: 82% of error sets have one element, the rest average 2.83, and
    the largest seen was 9 -- far too small for the asymptotics separating
    these algorithms to matter. What matters is that minimization reduces
    the set to 1.23 on average, so there are usually several equally
    minimal answers and the algorithm silently picks one. QuickXplain's
    divide-and-conquer decides which elements are tested together and so
    cannot be steered; deletion drops what it visits first, which makes
    the pick an ordering decision. `row-support` sheds the longest rows
    first because `build_soi_conflict` emits one literal per nonzero entry
    of the restricted row *plus* one per error variable, and the first term
    usually dominates -- so which row survives, not how many, is what sets
    clause length.

    **Measured 2026-08-10** (18 instances where all five arms complete, 0
    answer disagreements; `:lra.soi-conflict-lits`, median ratio against
    quickxplain):

    | arm | median | geomean | better/worse |
    |---|---:|---:|---:|
    | `deletion/id` | 1.000 | 0.957 | 6 / 7 |
    | `deletion/row-support` | 1.000 | 0.917 | 8 / 5 |
    | `deletion/violation` | 1.001 | 0.917 | 5 / 9 |
    | `none` | 1.218 | 1.186 | 3 / 15 |

    Two conclusions, and the second is the awkward one. **Minimization
    clearly pays** -- `none` costs ~19% more literals, 3 better against 15
    worse. But **no deletion arm dominates QuickXplain**: all three sit at a
    median of ~1.000, and the better geomeans for `row-support` and
    `violation` come from a handful of instances with large swings while
    their win/loss records point in opposite directions. So the
    `row-support` rationale above is *not* confirmed as a general effect.
    It is not worse either, which is what makes these portfolio arms rather
    than default candidates -- they are complementary, cheap, and cannot
    reach a default run. **The default stays `quickxplain/id`.**

    Clause length is a proxy: a shorter clause is usually stronger, but a
    longer one over lower-level literals can backjump further, and the
    tableau cannot see decision levels. PAR2 would be authoritative and was
    not measured.
  - Defensive pivot-count fallback to plain Bland selection, belt-and-
    suspenders beyond the paper's own tie-breaking-reduces-to-Bland
    termination argument (Section V-C), reusing the existing
    `bland_fallback_count_` machinery.
  - **Directly verified against the paper**: a white-box test
    (`lra_tableau_tests.cpp`) reproduces the paper's own Fig 6 worked
    example exactly (entering=x2, δ=1, leaving=x1, `Vio(V)` reaching 0 in
    one pivot) via `check()`'s final state.
  - A real heap-use-after-free was found (not just theorized) during
    manual smoke testing before any fuzzing: `update()`'s dependent-
    basic-row loop iterated `occurs_in_[nonbasic_var]` by reference while
    `set_soi_direction()` (called inside that same loop) could mutate
    that exact vector, when `nonbasic_var` was itself a term in
    `f_var_`'s row -- fixed by copying the affected-rows list before
    iterating.
  - Reported by `:lra.soi-multi-row-conflicts`, `:lra.soi-minimize-calls`,
    `:lra.soi-minimize-shrunk` and `:lra.soi-conflict-lits`,
    alongside the existing `:lra.bland-fallbacks`. **Opt-in**
    (`--lra-pivoting-rule soi`), same rollout precedent as Dantzig.
  - **Merge-gate fuzz result (job 113439, 2026-07-25): correctness-clean
    (500/500, 0 crashes/wrong-answers) but a notably higher timeout rate
    (176/500, ~35%) than the analogous Gaussian-elimination/row-
    propagation runs (~5-6%)**, and ~4.5x the wall-clock time for the
    same 500 cases. Consistent with the paper's own framing -- SOI's
    breakpoint-sorted `ΔVio` walk over every `Flex(f)` candidate each
    pivot is genuinely more expensive per-pivot than Bland/Dantzig's
    O(row size) selection, trading that cost for robustness on hard
    instances specifically, not average-case speed on typical ones. Not
    campaign-tier confirmed and not currently planned for default-on
    promotion given this; see `docs/run-log.json`.

- **Two-tier rational representation** ([heuristics-note item H2](#h2), 2026-07-28,

  `src/util/rational.{hpp,cpp}`, **unconditional**): `util::Rational` stores
  small values inline as `std::int64_t` numerator/denominator and only falls
  back to a heap `mpq_class` when a value genuinely needs more than 64 bits.
  Following Monniaux (CAV 2009, Section 4 -- see
  [](papers/lra-related-work.md)), whose stated reason is exactly the one
  profiling found here: "GMP rationals inherently involve some inefficiencies,
  including dynamic memory allocation, even for small numerator and
  denominator".
  - **Canonical-form invariant**: a `Rational` is in big form *if and only if*
    its value does not fit the small form. The representation is a function of
    the value, never of how the value was reached -- which is what lets
    `operator==` reject a form mismatch outright and lets `RationalHash` hash
    the small pair directly. Without it, a value reachable both directly and
    through an overflowing intermediate would have two representations and
    hash two ways, silently breaking `PolynomialKey` hash-consing (atoms that
    should share a slack quietly wouldn't) rather than producing a visibly
    wrong answer. Exactly two private factories (`from_int128_reduced`,
    `from_mpq`) establish it; nothing else assigns the representation fields.
    A debug/ASAN-only `check_invariant()` asserts it at both.
  - **Arithmetic**: Knuth (TAOCP Vol. 2, Sec. 4.5.1) two-gcd rational
    addition and cross-reduced multiplication, entirely in machine words with
    `__int128` intermediates. Both operands being canonical with
    magnitudes `<= INT64_MAX` bounds every product by `2^126` and every sum by
    `2^127`, so no intermediate can overflow `__int128` and the only range
    check needed is on the reduced result. Comparisons cross-multiply in
    `__int128` and never allocate or change representation.
  - **Deviation from the paper**: the small form is 64-bit, not Monniaux's
    32-bit. `kSmallMax` is the single knob, and the two widths were compared
    empirically rather than argued about; `__int128` is a GCC/Clang
    extension, introduced under `__extension__` because the build is
    `-Wpedantic -Werror`.
  - **Width ablation (2026-07-28, local, two alternating rounds; the same
    commit built twice, changing only `kSmallMax`).** 64-bit is kept, and the
    trade-off is exactly the predicted one:

    | instance | 32-bit | 64-bit | 64/32 | promotions (32 / 64) |
    |---|---|---|---|---|
    | `TM/p5-zenonumeric_s5` | 0.13s | 0.13s | 1.00x | 0 / 0 |
    | `sc-21.induction3` | 1.56s | 1.55s | 1.01x | 0 / 0 |
    | `uart-18.induction` | 0.57s | 0.53s | 1.08x | 0 / 0 |
    | `clock_synchro/clocksynchro_2clocks` | 2.44s | **1.10s** | **2.22x** | 668k / 359k |
    | `miplib/pp08a-12000` | 1.48s | 1.59s | 0.93x | 0 / 0 |
    | `DTP-Scheduling/constraints-tempo-width-30` | 7.59s | 8.77s | 0.87x | 0 / 0 |

    On every instance whose coefficients never leave 32 bits, the two are
    within noise or 32-bit is up to ~13% faster -- `__int128` operations are
    multi-instruction where 64-bit ones are not, and that cost is paid on
    every add and multiply. On the one instance that does overflow, 64-bit is
    2.2x faster because it cuts big-form operations 12-fold (4.44M -> 365k).
    A few percent on the common case buys a factor of two on the hard one, so
    64-bit stays. The ablation also earned its keep by finding a real latent
    bug: `Rational(std::int64_t)` range-checked only one side, which is
    vacuous at `kSmallMax == INT64_MAX` and wrong at any narrower width.
  - **Observability**: `:rational.promotions` and `:rational.big-operations`
    report how often the fast path was left. Measured: **zero** on
    `TM/p5-zenonumeric_s5`, `sc-21.induction3`, `uart-18.induction` and
    `miplib/pp08a-12000` (the fast path is taken 100% of the time), but 1.22M
    promotions / 1.27M big operations on
    `clock_synchro/clocksynchro_2clocks.main_invar.induct`, whose coefficients
    genuinely blow up through pivoting -- which is itself the explanation for
    that instance's ~1.5 ms/pivot cost.
  - **Effect** (paired local A/B: three alternating before/after rounds on one
    idle host, reporting each round so cluster/host drift is visible rather
    than averaged away; `:sat.decisions`/`:sat.conflicts`/`:lra.pivots`/
    `:lra.online-check-calls`/`:lra.theory-propagations` **bit-identical**
    throughout, confirming a pure representation change):

    | instance | before (3 runs, s) | after (3 runs, s) | best-of-3 | median |
    |---|---|---|---|---|
    | `TM/p5-zenonumeric_s5` | 3.66 / 4.65 / 5.77 | 0.75 / 0.79 / 0.94 | 4.9x | 5.9x |
    | `sc-21.induction3` | 27.73 / 32.17 / 41.76 | 6.79 / 7.52 / 8.14 | 4.1x | 4.3x |
    | `uart-18.induction` | 7.31 / 7.36 / 7.77 | 1.40 / 1.78 / 2.70 | 5.2x | 4.1x |
    | `clocksynchro_2clocks` | 11.77 / 14.13 / 14.76 | 5.56 / 6.27 / 6.73 | 2.1x | 2.3x |
    | `latendresse/ecoliFBAyicesTest-500` | timeout (90s) | timeout (90s) | -- | -- |

    `clocksynchro_2clocks` gains least, consistent with its 1.22M promotions:
    it is the one instance here whose coefficients really do outgrow 64 bits,
    so it keeps paying GMP's cost. `ecoliFBAyicesTest-500` does not move at all
    -- it is a single `check()` call over one large LP, which is H1/H3
    territory, not H2's.
  - **Promotion bar (Slurm job 114900, the 237-file stratified sample at a 10s
    timeout, diffed against the unchanged z3/cvc5 results from job 114200):
    solved 108 -> 124/237 (45.6% -> 52.3%), PAR2 2675.6s -> 2380.9s (-11.0%),
    zero wrong answers, and 16 files newly solved against ZERO regressions.**
    Per family: TM 6->10, miplib 1->4, uart 4->7, 2017-Heizmann 0->2,
    tta_startup 9->11, clock_synchro 6->7, sc 5->6. The gap to z3 (167) narrows
    from 59 files to 43 and to cvc5 (150) from 42 to 26. Unlike roadmap item 8,
    whose dramatic narrow-sample win did not survive this bar, this one does.
  - **Verified at merge-gate and campaign-gate fuzz tiers on both pivoting
    paths** (jobs 114896/114897 merge, 114898/114899 campaign): 7,000 fuzzsmt
    cases, two opfuzz runs and four yinyang typefuzz shards over real QF_LRA
    seeds -- 0 crashes, 0 wrong answers throughout. Incidentally, the gates'
    own timeout rates fell sharply (Bland campaign 118-143 -> 28, SOI campaign
    965-976 -> 324), the same speedup showing up in the fuzz corpus.

- **Direct bounds for single-variable atoms** ([heuristics-note item H1a](#h1),

  2026-07-28, `check_sat.cpp`, **unconditional**): an atom whose normalized
  polynomial is a single term `c*x` is a bound on `x` itself. The
  coefficient is divided out (`rhs' = rhs / c`) and no slack variable and no
  row are created at all. Dividing by a negative `c` reverses the relation,
  recorded as `PreparedAtom::asserts_lower_when_true` and interpreted in
  exactly one place, `bound_assertion()`, whose comment tabulates all four
  relation/orientation rows and which is what the new
  `single_variable_atom_*` tests pin.
  - Monniaux (CAV 2009, Section 2.1) states this as ordinary practice --
    "for each left hand side that is *not a variable*, a new variable is
    introduced" -- and Dutertre & de Moura's Section 2 does the same. Primo
    previously introduced a slack for *every* distinct polynomial.
  - **Two things follow beyond the row saving.** A variable's lower and
    upper bound now share a tableau variable: `(<= 0 x)` normalizes to
    coefficient -1 and `(<= x 10)` to +1, so under exact-content polynomial
    keys they used to land on two *different* slacks and the tableau could
    not see that `x` was bounded on both sides without pivoting. And
    row-based propagation reads a row term's bound directly instead of via
    `single_variable_slack`, which both simplifies it and removes the quirk
    item 7 had to work around (that index only ever registered a bare
    variable's positive-coefficient form, so `>=` atoms never got an entry).
  - **Row counts, measured via the new `:lra.rows` stat, against the
    pre-H1 shape measurements in [](qf-lra-heuristics.md) Section 1.3 --
    every one matches that section's predicted single-variable share
    exactly:** `latendresse/ecoliFBAyicesTest-500` 4,555 -> 1,394 (-69%),
    `TM/p6-zenonumeric_s5` 1,378 -> 67 (-95%), `miplib/pp08a-12000` 400 ->
    138 (-66%), `sc-17.induction3` 1,185 -> 723 (-39%), `uart-17.induction`
    1,446 -> 920 (-36%), `tta_startup/simple_startup_4nodes` 584 -> 500
    (-14%), `clock_synchro/clocksynchro_2clocks` 264 -> 259 (-2%),
    `DTP-Scheduling/constraints-tempo-width-30` 3,932 -> 3,932 (unchanged --
    it is 100% difference constraints, so it has no single-variable atoms
    to remove).
  - **Effect** (paired local A/B against the H2-only build, two alternating
    rounds): `TM/p5-zenonumeric_s5` 0.63s -> 0.12s (5.3x; decisions 13,230
    -> 1,435, pivots 18,097 -> 230), `sc-21.induction3` 6.33s -> 1.98s
    (3.2x; decisions -47%, pivots -58%), `uart-18.induction` 1.18s -> 0.94s
    (1.3x; pivots -63%), `miplib/pp08a-12000` 1.97s -> 1.72s (1.2x),
    `clock_synchro` and `DTP-Scheduling` flat within noise (their counters
    are unchanged or nearly so, as their row counts predict).
    `latendresse/ecoliFBAyicesTest-500` still times out despite losing 69%
    of its rows -- its cost is the remaining 1,394-row LP with a 691-term
    dense row, i.e. H3's fill-in problem rather than H1's row-count one.
    Unlike H2 this is *not* a pure representation change: it renumbers
    tableau variables and strengthens propagation, so search counters
    legitimately move.
  - **Promotion bar (Slurm job 115172): solved 124 -> 136/237, PAR2 2380.9s
    -> 2149.8s (-9.7%), 12 files newly solved, ZERO regressions, zero wrong
    answers.** Per family: TM 10 -> 15 (now matching z3's 15/15), 2019-ezsmt
    3 -> 7, sc 6 -> 8, tta_startup 11 -> 12. **Stacked with H2: 108 -> 136
    (+28 files, 45.6% -> 57.4%), PAR2 -19.6%**, and the gap to z3 (167)
    narrows from 59 files to 31, to cvc5 (150) from 42 to 14.
  - **Verified at merge-gate and campaign-gate fuzz tiers on both pivoting
    paths** (jobs 115168/115169 merge, 115170/115171 campaign): 7,000
    fuzzsmt cases, two opfuzz runs and four yinyang typefuzz shards over real
    QF_LRA seeds -- 0 crashes, 0 wrong answers throughout, with the Bland
    merge gate coming back 500/500 PASS with no timeouts at all.

- **Scale-canonical polynomial keys** ([heuristics-note item H1b](#h1), the

  long-deferred competitiveness-roadmap item 3, 2026-07-28, `check_sat.cpp`,
  **unconditional**): `PolynomialKey` divides every coefficient by the
  coefficient of the lowest-indexed variable, so proportional polynomials
  collapse to one key.

  **How sharing is detected.** During `PreparedLraTheory` construction, each
  arithmetic atom is processed as follows:

  1. `normalize_atom` moves all variable terms to the left and the constant to
     the right, producing $p \mathrel{\sim} c$, where $p$ has no constant
     term.
  2. The terms of $p$ are sorted by `TermId`. Let $a$ be the nonzero
     coefficient of the first term. Divide every coefficient and $c$ by
     $a$. If $a<0$, record that the bound orientation is reversed.
  3. The resulting sorted vector of exact `(TermId, Rational)` coefficients is
     the `PolynomialKey`. The right-hand side, relation, strictness and
     orientation are deliberately not part of this key: they describe bounds
     on the polynomial, not the polynomial whose value the slack represents.
  4. If the key has one term, its normalized polynomial is just one problem
     variable and no slack is needed. Otherwise, look up the key in
     `slack_by_polynomial`. Reuse the existing slack when found; on the first
     occurrence allocate one slack, add its defining row, and insert the key.

  Since 2026-08-17 this is also preceded by a global GCD rewrite of the core
  arithmetic atom. The mandatory pre-clausification pass clears coefficient
  denominators, divides by integer content, fixes the sign, and rebuilds the
  predicate through `TermManager`. `PreparedLraTheory` still divides by the
  first nonzero coefficient as a defensive theory-boundary invariant. For
  example, the core pass maps both `2x + 3y <= 5` and
  `4x + 6y <= 10` to `2x + 3y <= 5`; theory preparation may represent that
  same direction internally as `x + 3/2 y <= 5/2`. The conventions identify
  the same rational scale class, while the early primitive-integer form also
  lets Boolean preprocessing and clausification share the atom.

  Because the map is populated while walking the complete theory-atom list,
  this lookup finds proportional forms regardless of which SAT assignment
  first activates them. For example, it allocates one global slack
  `s = x + y` for all of these atoms:

  | source atom | normalized bound on `s` |
  |---|---|
  | `x + y <= 5` | `s <= 5` |
  | `2x + 2y <= 10` | `s <= 5` (an equivalent atom) |
  | `2x + 2y <= 12` | `s <= 6` (a distinct, weaker upper bound) |
  | `-x - y <= -5` | `s >= 5` (the opposite orientation) |

  The first two source bounds become the same hash-consed core atom before
  clausification. At the tableau level `LraTableau::assert_upper` therefore
  sees only one SAT atom for that semantic predicate.
  The `s <= 6` atom cannot be discarded from the prepared problem, however:
  under arbitrary Boolean structure it may be assigned without `s <= 5`, or
  assigned false and therefore assert `s > 6`. What is shared globally is the
  structural slack; each *distinct* predicate still has its own relation,
  scaled right-hand side, polarity, and explanation reason. In particular,
  `s <= 5` and `s <= 6` cannot be merged.

  The scale factor is divided out of the atom's right-hand side and a negative
  factor reverses the relation, reusing H1a's
  `asserts_lower_when_true` flag and its single interpretation point. Monniaux
  (CAV 2009, Section 2.1) is the basis for the early coprime-integer form.
  Prepared theory state retains the leading-coefficient-one form as a defensive
  internal representation. See
  [](papers/lra-atom-canonicalization.md) for the ownership and invariants.
  - Row counts fall further on top of H1a: ecoliFBA 1,394 -> 695, TM/p6
    67 -> 38, sc-17 723 -> 433, uart-17 920 -> 511, clock_synchro 259 -> 179,
    tta_startup 500 -> 284, and DTP-Scheduling 3,932 -> 2,882 -- the first
    time that family moves at all, since `x-y` and `y-x` are proportional.
  - **Correctness-verified, performance verdict deferred.** Merge and
    campaign gates on both pivoting paths (jobs 116545-116548): 7,000 fuzzsmt
    cases, 0 crashes, 0 wrong answers, plus clean opfuzz and yinyang runs.
    Local wall-clock is genuinely mixed (clock_synchro 3.1x faster, uart-18
    1.5x, but DTP-Scheduling 0.74x *slower* despite 8.6x fewer decisions --
    merging proportional rows concentrates fill-in into fewer, denser rows),
    so its 237-file verdict comes from item H3's four-way run, whose `bland`
    column measures exactly this item's effect on the default.

- **Fill-in-aware pivot selection** ([heuristics-note item H3](#h3), 2026-07-28,
  `lra_tableau.{hpp,cpp}`, **the solver default since 2026-07-29**):
  `PivotingRule::Sparse`, selected by `--lra-pivoting-rule sparse`. The exact
  definition follows; this is the authoritative description of what Primo
  calls *Sparse*.

  In plain terms, Sparse aims to limit fill-in (the creation of new nonzero
  tableau coefficients during pivoting). It first chooses a violated basic
  variable whose defining row has the fewest nonzero coefficients, breaking
  ties in favor of the largest bound violation. Among the nonbasic variables
  that can repair this violation, it then chooses one occurring in the fewest
  tableau rows, preferring a coefficient of $+1$ or $-1$ on ties. This favors
  pivots that substitute a short row into as few other rows as possible,
  following the sparsity-oriented heuristics described by Monniaux and King;
  after a finite number of heuristic pivots, Primo switches to Bland's rule to
  guarantee termination. The citations and precise deviations from those
  sources are given below and in [](papers/lra-related-work.md).

  Let the current tableau basis be $B$, its nonbasic variables be $N$, and
  write each basic row as

  \[
  x_b = \sum_{j\in N} a_{bj}x_j.
  \]

  `Row` stores exactly the nonzero right-hand-side entries, so define

  \[
  r(b) = |\{j\in N : a_{bj}\ne 0\}| = \texttt{rows_[b].size()},
  \qquad
  c(j) = |\{b\in B : a_{bj}\ne 0\}| =
  \texttt{occurs_in_[j].size()}.
  \]

  Thus row length is a nonzero count, not the number of tableau variables and
  not a byte size. Column length is the number of current basic rows containing
  the variable; coefficient magnitudes are not stored in `occurs_in_`.
  `LraTableau` owns both dense outer vectors, and `pivot()` maintains the
  cross-linked occurrence index as the basis changes.

  Before the Bland fallback engages, one Sparse repair step is:

  1. Form the sorted set $V$ of violated basic variables. For $b\in V$, let
     $d(b)$ be the exact `DeltaRational` distance from its assignment to the
     violated bound.
  2. Under the shipped default leaving policy, choose the lexicographic minimum
     of $(r(b),-d(b),b)$: shortest row first, then largest violation, then
     smallest variable id. `--no-lra-sparse-leaving` instead chooses the
     lexicographic minimum of $(-d(b),b)$, the Dantzig-style policy. The
     separate experimental `--lra-least-violated-leaving` policy takes
     precedence when enabled and minimizes $(d(b),r(b),b)$, following
     Griggio's MathSAT 4 description.
  3. An entry $j$ in the chosen row is eligible exactly when $x_j$ can move
     in the direction required to bring $x_b$ to its violated bound without
     crossing $x_j$'s own bound. Concretely, when $x_b$ is too low,
     $a_{bj}>0$ requires $x_j<u_j$ and $a_{bj}<0$ requires $x_j>l_j$;
     when $x_b$ is too high, those directions reverse. A missing relevant
     bound makes the corresponding movement eligible.
  4. Scan the chosen row cyclically from `sparse_pricing_cursor_`. Among the
     first $k$ *eligible* entries, choose the lexicographic minimum of
     $(c(j),\mathit{nonunit}(a_{bj}),j)$, where `nonunit` is 0 for coefficient
     $+1$ or $-1$, and 1 otherwise. The option
     `--lra-sparse-pricing-candidates` supplies $k$, default 16; zero means
     all eligible entries. The cursor advances after a successful selection.
     If fewer than $k$ entries are eligible, the entire row is inspected.
     In particular, the scan never reports that no repair exists without
     examining the entire row.
  5. Pivot $j$ into the basis, pivot $b$ out, and assign $x_b$ its
     violated bound.

  `pivot()` substitutes the leaving row into the rows listed by the entering
  column, so $c(j)r(b)$ is Primo's upper-level proxy for row-combination work.
  Classical Markowitz notation instead estimates possible fill with
  $(c(j)-1)(r(b)-1)$. For a fixed row with $r(b)>1$, both rank entering
  variables by $c(j)$; when $r(b)=1$, the classical fill estimate is zero for
  every candidate and provides no ranking. Primo does **not** compute that
  classical product or minimize either product globally over every feasible
  $(b,j)$ pair: it chooses the leaving row first, then prices only a bounded
  prefix of its eligible entering variables. “Markowitz-style” therefore
  describes the cost intuition, not an implementation of global Markowitz
  pivot search.

  The minimum-column-length rule and finite Bland fallback are described in
  King's thesis, Section 2.2.12; Monniaux (CAV 2009), p. 8, motivates sorting
  by row and occurrence counts. Primo takes only those sparsity criteria from
  those sources. The unit-coefficient tie-break, rotating bounded scan, exact
  lexicographic combination, and its defaults are Primo-specific engineering
  choices. See [](papers/lra-related-work.md).

  After more than
  `--lra-bland-fallback-factor * variables + --lra-bland-fallback-offset`
  pivots in one `check()` call (defaults 4 and 64), selection remains Bland for
  the rest of that call. Consequently, bounded Sparse pricing changes only the
  heuristic prefix; the finite Bland suffix retains the termination argument.
  - New `:lra.pivot-row-updates` / `:lra.pivot-term-ops` count what the pivots
    cost, not just how many there were; trading fewer-but-costlier pivots for
    more-but-cheaper ones is this rule's whole risk, so both are reported.
  - **Promoted to default 2026-07-29**, together with the leaving-side half.
    The four-way 237-file run (job 117222), all four configurations on one
    binary against the unchanged z3/cvc5 of job 114200:

    | config | solved / 237 | PAR2 | wrong |
    |---|---|---|---|
    | pre-H1b/H3 | 136 | 2149.8s | 0 |
    | bland (former default) | 144 | 1962.4s | 0 |
    | dantzig | 148 | 1931.3s | 0 |
    | sparse | 154 | 1798.9s | 0 |
    | **sparse + leaving (new default)** | **168** | **1538.9s** | 0 |
    | z3 | 167 | 1531.6s | -- |
    | cvc5 | 150 | 1887.1s | -- |

    **+24 files over the former default with zero regressions** (+32 over the
    pre-H1b/H3 run, also zero). Per family: latendresse 0 -> 6 (matching z3;
    cvc5 solves 0), uart 10 -> 13, clock_synchro 7 -> 12 (z3 9), 2019-ezsmt
    7 -> 12 (z3 10), sc 11 -> 13, LassoRanker 1 -> 3 (z3 2).
  - **The latendresse gain is entirely the leaving-side half**, the knob that
    was gated separately *because* it is not progress-neutral. That objection
    is right for the Boolean-heavy families and simply does not apply to a
    pure-LP instance: one `check()` call with no Boolean structure has no
    search trajectory to steer wrong. Entering-side alone reaches 154.
  - **Dantzig, finally measured on this bar** after three roadmap cycles of
    only 17-file quick-evals: 148, four ahead of the Bland default it never
    replaced. Including it is what makes the sparse win attributable rather
    than confounded with the leaving-rule change the two share.
  - Cost counters explain the win rather than just recording it. Term-ops,
    bland -> new default: sc-21 20.4M -> 2.8M, clock_synchro 11.2M -> 1.2M,
    DTP-Scheduling 54.6M -> 11.0M. On `latendresse/ecoliFBAyicesTest-500` the
    default finishes in 0.58s with 8.7M term-ops, where Dantzig needs 130.9M
    and ~19.5s and Bland does not finish. `clock_synchro`'s
    `:rational.promotions` falls 358,801 -> 1,994, the `|coefficient| = 1`
    tie-break keeping values inside H2's machine-word fast path.
  - Ablation: `--lra-pivoting-rule bland` and `--no-lra-sparse-leaving`, both
    verified to change behaviour.
  - **Gated at both tiers on both pivoting paths.** Merge: 117221 (bland,
    499/500 PASS -- confirming the shared-loop refactor left the then-default
    alone), 117219 (sparse, 499/1), 117220 (sparse+leaving, 496/4). Campaign:
    117227 (new default, fuzzsmt 3000: 2990 PASS/10 TIMEOUT) and 117228 (SOI,
    3000: 2908/92), each with opfuzz 14/14 seeds valid and two yinyang shards,
    0 bug triggers throughout. No crashes, no wrong answers anywhere.

- **Delta-maintained `occurs_in_`** ([heuristics-note item H10](#h10),
  2026-08-01, `lra_tableau.{hpp,cpp}`, **unconditional**): `pivot()` no longer
  removes and re-adds every affected row's occurrence entries. `occurs_in_`
  records only which rows mention a variable, never a coefficient, so a term
  carried through a pivot keeps the entry it had; only cancellations remove
  and only genuinely new terms add. Callgrind had put the round trip at ~91%
  of instructions -- 40.1M `std::remove` calls scanning 37.6 billion elements
  on `scrambled228965`.
  - **Scrambled competition set 398 -> 439 solved** (PAR2 26723 -> 22207),
    stratified 197 -> 202, churn +41/-0 and +5/-0 -- zero regressions on
    either bar. Median paired 1.51x. That is 439 against z3's 429 on the
    same files. 1.15-4.85x locally on four mid-band instances, answers
    identical. `pivot`'s gprof
    self time falls 10.04s -> 1.26s at an identical call count, and the
    profile becomes arithmetic-dominated rather than bookkeeping-dominated.
  - Guarded by `occurs_in_index_matches_full_rebuild()`, checked after every
    pivot across all four pivoting rules -- a wrong index does not crash, it
    makes `update()` skip a dependent row and yields a wrong answer later.
- **Bounds-only backtracking** ([heuristics-note item H9](#h9), 2026-07-31, `lra_tableau.{hpp,cpp}`,
  **unconditional except under SOI**): `pop()` restores bounds and nothing
  else. It used to restore bounds, assignments *and* every pivot's row
  rewrite. This is the paper's own scheme -- technical report SRI-CSL-06-01
  Section 3.2.4, "since no pivoting or other expensive operation is used,
  backtracking is very cheap" -- and Primo had diverged from it on a
  justification that turned out circular: reverting the assignments is what
  forced reverting the pivots. Sound because the assignment satisfies
  `Ax = 0` independently of the basis, and `pop()` only ever *relaxes*
  bounds. `PivotingRule::Soi` keeps the old scheme; its objective row is a
  sum over the currently-violated rows, not a linear combination of `A`. The
  argument is on `LraTableau`'s class comment; the line-by-line comparison
  that found it is in [](papers/fast-linear-arithmetic-dpllt.md).
  - **The largest single gain measured on this project.** Scrambled
    competition set (595 files, 60s): **378 -> 399 solved**, PAR2 28691 ->
    26661. Stratified sample (237, 60s): 192 -> 197, PAR2 6069 -> 5575. Zero
    cross-configuration disagreements, zero wrong answers.
  - The mechanism is that not undoing pivots lets the search keep the repair
    work it already paid for: pivot counts fall 2.3-5.3x on the profiled
    instances, and `rebuild_occurs_in_index` leaves the profile entirely
    because nothing structural changes on a pop.
  - **Unlike H8 it is not regression-free, and that is the part to read.**
    Churn is +32/-11 on the 595 set and +7/-2 on the stratified sample, and
    among the 145 commonly-solved files over a second, 62 are more than 10%
    *slower* against 64 more than 10% faster -- median 1.01x, p25 0.63, p75
    1.30, tails 0.10x to 24.9x. It nets +21 because the wins land near the
    timeout: 10 of the 11 lost files already took 24-60s of a 60s budget.
    The variance is inherent -- keeping the basis across a backtrack keeps
    one that suited the *previous* subproblem.
  - Gated campaign-tier on the default path (3000 fuzzsmt QF_LRA cases plus
    opfuzz and two yinyang shards, 0 wrong) and merge-tier on SOI (2000, 0
    wrong), plus local ASAN across five configurations.
- **Merged row combination in `pivot`** ([heuristics-note item H8](#h8), 2026-07-31,
  `lra_tableau.{hpp,cpp}`, **unconditional**): `pivot`'s affected-row loop
  combines the affected row with the scaled entering row by a linear two-way
  merge instead of concatenating both into a scratch buffer and sorting it.
  This rests on a new **`Row` invariant -- strictly ascending by var id, never
  a zero coefficient** -- which `add_row` now establishes by sorting and
  `pivot` preserves by splicing `basic_var` into `new_entering_row` at its
  ordered position. The affected row is also taken by reference and *moved*
  into the trail rather than copied twice.
  - Found by a flat profile rather than by design argument: a gprof run put
    11.6% of runtime on `scrambled339450` in that one `std::sort`, with 100M
    inner comparisons. See [](qf-lra-heuristics.md) section 6.2a.
  - **Promoted on every bar, with zero regressions on any of them.** Scrambled
    competition set (595 files, 60s): **369 -> 378 solved**, PAR2 29561 ->
    28654, median paired 1.19x, **+9/-0** -- five of the nine in the
    500KB-5MB gap band. Mid-band gap (59 files, 300s): 37 -> 38, PAR2 19475
    -> 18309, median 1.16x, +1/-0. Stratified sample (237 files, 60s): 189 ->
    192, PAR2 6473 -> 6067, median 1.23x, +3/-0. All three same-node paired
    against the parent commit; 0 cross-configuration disagreements and 0
    wrong answers throughout.
  - Calibration, so this is not over-read: the payoff curve in
    [](qf-lra-heuristics.md) section 6.1a says the gap band needs 2x for 15
    files. H8 is 1.19x and buys 5 there -- on the curve, not above it.
  - Gated: campaign-tier QF_LRA fuzz on the default path (3000 fuzzsmt cases,
    0 wrong) plus merge-tier on the SOI path (1000, 0 wrong).
  - **Sanitizer coverage is local, not from the cluster.** The remote ASAN
    toolchain is still broken, so H8 was covered with a local `build-asan`
    instead: `make asan-test` (12/12), 45 real instances and 48 runs across
    four configurations with zero sanitizer reports and zero answer
    differences against Release, and 250 fuzzsmt cases against z3 (247 PASS,
    3 timeouts at 20s under Debug+ASAN, 0 wrong). That is smoke-to-merge
    tier, not campaign tier -- but it matters here specifically because H8
    rewrites the row loop and moves rows into the trail, which is where this
    class previously had a real heap-use-after-free.
- **Cross-linked `occurs_in_` index** ([open-leads lead 3b](#lead-occurs-in), 2026-08-06, `lra_tableau.{hpp,cpp}`,
  **unconditional**): each row entry carries a back-index into the column list
  it appears in, so `pivot()` removes a dead occurrence without searching for
  something it is already holding.
  - **Representation.** See [](#tableau-sparse-representation) for the
    authoritative row/column invariant and its maintenance rules. The important
    optimization here is that removal uses the row entry's back-index to
    swap-pop the column in O(1), then repairs the one moved entry with a
    `lower_bound` over its sorted row in O(log row).
  - **What it replaced:** a linear scan of `occurs_in_[var]`. Measured at
    12.0M removals scanning 8.17B elements on `constraint-199552`, where
    occurrence lists average 679.
  - **This is half a cross-linked sparse matrix, deliberately.** Rows point
    into columns; columns do not point back into rows. King's thesis §2.2.7
    specifies both directions via doubly linked lists, which would make the
    repair step O(1) -- but his rows are *unsorted*, so adopting his form
    wholesale means giving up the two-way merge and item H8. See
    [](papers/summaries.md) for the source and
    [open-leads](#lead-occurs-in) for the fork.
  - **Correctness gate:** QF_LRA model fuzz gate on Slurm (job 119566, 9-task
    array) -- 0 wrong answers, 0 invalid models.
    `occurs_in_index_matches_full_rebuild()` checks that every back-index
    round-trips
    (`occurs_in_[var][entry.occ_slot] == basic_var`), which is what makes the
    scheme falsifiable in a debug build.
  - **Evidence, mixed and worth reading before relying on it.** Stratified
    sample (237 files, 10s, job 119555): **neutral** -- gained 0, lost 0, and
    the PAR2 gain of 8.00 is below the 12.22 run-to-run noise floor measured
    from an identical baseline binary. Tropical-matrix (10 files, 1200s, job
    119577): **1/10 -> 2/10 solved**, PAR2 21611 -> 19998, gained 1 / lost 0,
    with `constraint-199552` at 10.65s -> 6.80s.
  - **It cannot change the search**, which is unusual and is what makes the
    above interpretable: on 25 solved stratified files the counters are
    bit-identical to the parent commit (`:lra.pivots`,
    `:lra.pivot-term-ops`, `:sat.decisions`, `:sat.conflicts`). Every per-file
    time difference is therefore pure constant factor.
  - **Known regression, with a known cause.** Long-*row* instances lose, the
    mirror of the long-*column* instances that win: the O(log row) repair step
    replaces a short column scan, and `RowEntry` grew 32 -> 40 bytes. Worst
    measured is `ecoliFBAyicesTest-500` at 1.58x (rows there reach 691 terms);
    on files the baseline takes over a second, no regression exceeds 1.10x.
    Two open follow-ups: narrow `LraVarId` to 32 bits (restores 32 bytes, and
    separates the two causes), and store the row slot in `occurs_in_` too,
    which makes the repair O(1) and removes the mechanism.
- **Fixed-variable elimination** ([heuristics-note item H7](#h7), 2026-07-30,

  `lra_tableau.{hpp,cpp}`, **opt-in**, `--lra-fixed-elimination`): a basic
  variable whose lower and upper bounds coincide is pivoted out of the basis
  as soon as it becomes fixed. `can_enter()` already rejects a fixed variable
  on every branch, so a fixed *non-basic* variable is completely inert; only a
  fixed *basic* one churns, demanding a repair pivot whenever any term in its
  row moves. The entering candidate is preferred unbounded (an unbounded basic
  variable's row can never be violated), then by Markowitz occurrence count,
  then by a unit coefficient -- and never one that is itself already fixed,
  which the other criteria actively steer into.
  - **Not promoted, and the reason is worth reading.** Per-instance the
    mechanism is decisive: `latendresse/ecoliFBAyicesTest-500` 0.8s -> 0.1s
    with `:lra.pivot-term-ops` 8.66M -> 0.13M, `scrambled335326` 51.0s ->
    14.9s. On the 595-file scrambled bar it is +1 file (373 -> 374), PAR2
    flat, median 0.94x over commonly-solved files.
  - **It is bimodal.** The entire 56-file deficit against z3 is the 500KB-5MB
    band (45 against 101), and H7 nets exactly zero there: +8/-8, with a
    *median 0.64x* over the 32 files both configurations solve -- the typical
    gap instance gets 1.6x slower. The one real gain is >5MB, 7 -> 13 of 50
    (+8/-2), closing that band's deficit against z3's 14 to one file.
  - No size threshold was added to keep the large-instance win and dodge the
    mid-range cost: that is tuning a heuristic that lost its own bar.
  - An earlier version also *froze* qualifying rows out of `occurs_in_`. That
    half was strictly negative and carried all of the item's wrong-answer
    risk, so it was deleted rather than kept behind a flag. What remains has
    no wrong-answer mode -- it is a pivot, which is what `check()` already
    does on every repair.
  - New `:lra.fixed-variables` / `:lra.elimination-pivots`. Note that
    `:lra.pivots` is NOT comparable across this ablation: it is sampled as a
    delta around `check()`, while elimination pivots happen inside
    `assert_upper`/`assert_lower`. Use `:lra.pivot-row-updates` /
    `:lra.pivot-term-ops`.
  - Ablation: default off; `--lra-fixed-elimination` verified to change
    behaviour, and the default build reproduces pre-H7 counters exactly.
- **Disequality**: SAT-visible `Real`-sorted `=` is lowered to `(t1 <= t2) AND (t2 <= t1)`
  (since Real is a total order), not handled as a special relation in the
  tableau at all. `NOT(t1=t2)` is then just `NOT(AND(Le,Le))`, already
  correctly handled by `BooleanClausifier`'s existing Tseitin/CNF machinery
  for arbitrary Boolean structure -- ordinary CDCL branching over the two
  underlying `Le` atoms explores `t1<t2` vs `t1>t2` for free. No
  case-splitting or lemma-learning code exists anywhere in
  `LraTableau`/`OnlineLraPropagator`. `LraAtom`'s `Eq` relation is unreachable
  through the ordinary frontend-to-LRA path, but the mixed final check uses it
  for combination-internal interface equalities already assigned true; false
  `Eq` remains unsupported. The lowering itself (`lower_real_equalities` in
  `preprocessing.cpp`) runs once, as the last step of the preprocessing
  pipeline, immediately before the `has_equality`/`has_arithmetic` theory
  dispatch -- **not** at parse time as originally implemented (changed
  2026-07-25) -- so solve-eqs and the other Equality-aware preprocessing
  passes see genuine Real `Equality` nodes first. See
  `docs/preprocessing.md`'s "Real-Equality Lowering" section.

### Implemented subset vs. deviations vs. limitations

- **QF_LIA, stages 1-3.** `(set-logic QF_LIA)` is supported: an `Int`
  sort, linear atoms (`+ - *`(literal coefficient) `< <= > >= = distinct`),
  a plain branch-and-bound decision procedure layered entirely on top of
  the existing `LraTableau`, a read-only GCD-consistency pre-check, and a
  Gomory-cut pre-check (King's thesis's mixed-integer cut, eq. 4.9) that
  adds rows to a private per-round tableau copy via `LraTableau`'s
  existing public API -- no change to `LraTableau` or
  `OnlineLraPropagator` themselves in any stage. No `div`/`mod`, and no
  mixing `Int` with `Real` in one problem (that would be QF_LIRA). The
  three stages together still have no completeness argument of their own
  -- some integer-infeasible systems can branch forever regardless (a
  concrete example is documented and tested) -- so the search is capped
  by `--lia-branch-limit`/`--lia-gomory-cut-limit` and reports `unknown`
  if a cap is hit rather than looping. See `docs/qf-lia-branch-and-bound.md`
  for the full design.
- **Real-only Real/UF combination.** No `Int` counterpart to the QF_UFLRA
  coordinator below: QF_LIA cannot currently be combined with EUF or mixed
  with `Real`. A partial QF_UFLRA
  coordinator exists for separable formulas and for arithmetic atoms containing
  Real-valued UF applications. The SMT layer purifies each such application to
  an internal Real proxy, first replacing any non-constant Real arguments by
  their own LRA-defined proxies so the EUF-side definition remains a flat UF
  application. The purified arithmetic atom is sent to LRA, and EUF-implied
  proxy equalities are checked against LRA under the current SAT assignment. EUF
  atoms with non-constant Real arguments are purified in the other direction:
  the EUF atom receives an internal Real proxy, while LRA receives the two
  defining bounds `proxy <= term` and `term <= proxy`. The reverse interface
  direction is checked for shared Real
  constants, including those internal Real-argument proxies: if LRA makes both
  `x < y` and `y < x` inconsistent under the current assignment, the coordinator
  asserts `x = y` into an augmented EUF check. Mixed `get-model` reconstructs
  concrete Real values for both Real arguments and Real-valued UF application
  results: reconstruction augments the LRA model with EUF-implied equalities and
  strict separations for EUF-distinct Real constants before printing concrete
  Real values in UF table guards and results. This is not full Nelson-Oppen
  combination: fine-grained cross-theory explanations, full model-equality
  propagation, native false-`Eq` LRA atoms, and unbounded arrangement
  enumeration are still not implemented. The optional `--model-equality-branching`
  scaffold creates default-off equality assumptions for LRA-model-equal shared
  Real pairs, but those guesses are not a complete arrangement-enumeration
  procedure.
- ~~Disequality (negated `=`) is rejected~~ -- **fixed 2026-07-24**, see
  above. Was the dominant cause of `UnsupportedInput` in every fuzz
  campaign (394-418/500 cases); after the fix, zero `UnsupportedInput` in
  the same 500-case campaign.
- **`*` is more permissive than strict QF_LRA syntax**: the non-literal
  operand may be a compound linear term (`(* 2 (+ x y))`), not only a bare
  variable -- harmless (still linear) and simpler to implement than
  enforcing the stricter grammar.
- ~~No pivoting-rule choice yet~~ -- **added 2026-07-24** (Milestone 4a):
  `LraTableau::PivotingRule::Dantzig`, opt-in, default stayed Bland at the
  time (superseded 2026-07-29 by `sparse`; Dantzig itself was finally measured
  on the 237-file bar in job 117222, scoring 148 against Bland's 144). Devex
  and Steepest-Edge (`docs/papers/lra-related-work.md`'s survey) remain
  unimplemented -- Dantzig alone already closes most of the practical gap
  per the cited comparative benchmark (fastest per-instance wall time),
  and adding either of the other two is a bigger, separate undertaking.
- ~~**Polynomial dedup is exact-match only, not scale-invariant**: `x<=5`
  and `2x<=10` still get separate slacks~~ -- **fixed 2026-07-28**. Item H1a
  made both single-variable forms direct bounds on `x`, with no slack at all;
  item H1b made compound `PolynomialKey`s scale-canonical, so `x+y<=5` and
  `2x+2y<=10` share the one slack for `x+y`. Since 2026-08-17, the early
  primitive-integer atom pass also maps equivalent source predicates to one
  hash-consed SAT atom; `PreparedLraTheory` retains scale-canonical keys as a
  defensive boundary. See [](papers/lra-atom-canonicalization.md).
- **Propagation only relates atoms sharing the exact same slack**, not
  Dutertre & de Moura's stronger row-based/interval bound propagation
  across *different* variables via a basic row's children. Also
  deliberately out of scope for Milestone 3b (a materially bigger
  technique), not a bug.
- **Doesn't reuse `--pure-theory-filter`**: that optimization's soundness
  was established for EUF specifically; reusing it for LRA without
  independent verification was judged too risky for a first milestone.
- ~~A pre-existing, theory-agnostic robustness gap: deeply-nested
  S-expressions can stack-overflow `smtlib::SExpr`'s recursive copy
  constructor~~ -- **fixed 2026-07-24**, theory-agnostic (not an
  arithmetic-specific change): `SExpr` parsing/copying/destruction and
  `Frontend::build_term`'s term-building (both shared by QF_UF and
  QF_LRA) are now iterative, not recursive descent. See
  `docs/papers/fast-linear-arithmetic-dpllt.md`'s note and the
  `parser.cpp`/`ast.cpp`/`check_sat.cpp` (`TermBuilder`) class comments.

## Milestone roadmap

See `/home/user/.claude/plans/piped-wondering-fairy.md` for the full plan.
Milestones 1-4 (above) are all done, plus disequality support and the
recursion-removal pass -- QF_LRA is **supported**, not experimental.


### QF_LRA options and statistics reference

Every QF_LRA option, its default, and where its evidence lives. Verified
against `CheckSatOptions` and `--help` on 2026-08-06 by diffing the table
against `--help` output rather than by eye -- five options were missing.

| option | default | what it does |
|---|---|---|
| `--lra-pivoting-rule {bland,dantzig,soi,sparse}` | `sparse` | Check/Pivot variable selection. `sparse` is item H3. |
| `--no-lra-sparse-leaving` | leaving rule **on** | Ablates the leaving-side half of `sparse`. |
| `--lra-least-violated-leaving` | **off** | MathSAT's leaving rule (Griggio thesis 2.5.3 p.53): least-violated first, shorter row as tie-break. Only under `sparse`. 167 vs 174 on the bar, but beats most-violated by 5 -- a complementarity candidate, not a win. |
| `--mixed-dispatch {narrow,wide}` | `narrow` | Which formulas reach the mixed EUF/LRA solver. `wide` counts a Bool-sorted *constant* as an EUF predicate (pre-2026-08-07 behaviour), routing pure QF_LRA plus propositional variables to the mixed path. Usually much worse, but recovers `tropical-matrix/constraint-199552` under `--guarded-real-equality-lowering` (2.2s against a timeout). Portfolio dimension and fuzzing reach; see [](nelson-oppen-qf-lra-regression.md). |
| `--monotone-elimination` | **off** | One-sided (monotone) variable elimination in preprocessing, with model reconstruction. 173 vs 174, gained 0 -- did not clear its bar. |
| `--no-lra-propagation` | propagation **on** | Ablates same-slack propagation (Dutertre & de Moura 4.2). |
| `--lra-row-propagation` | **off** | Row/interval implied bounds (roadmap item 2). |
| `--lra-bidirectional-row-propagation` | **off** | Reverse direction: tighten a row term from the row's own bound (item 7). Requires `--lra-row-propagation`. |
| `--lra-theory-decisions` | **off** | Theory-supplied decision literals via `cb_decide` (item 8). |
| `--lra-model-phase` | **off** | H5 beta-based phase hints for arithmetic atoms; keeps VSIDS variable choice. |
| `--lra-fixed-elimination` | **off** | Item H7 (LP presolve's fixed-variable removal, Andersen & Andersen 1995). Opt-in; see its falsifier outcome. |
| `--guarded-real-equality-lowering` | **off** | Opt-in guarded equality distribution inside Real-equality lowering; useful local tropical result but mixed 10-file evidence. |
| `--lra-row-propagation-max-row-size` | 64 | Longest row row-propagation will derive a bound for. |
| `--lra-row-propagation-max-fanout` | 64 | Most rows one bound change may scan. |
| `--lra-bidirectional-row-propagation-max-row-size` | 32 | Reverse direction's row-length cap. |
| `--lra-bidirectional-row-propagation-max-fanout` | 32 | Reverse direction's fanout cap. |
| `--lra-sparse-pricing-candidates` | 16 | How many eligible entering candidates `sparse` scores before taking the best seen; 0 scores all. See below. |
| `--lra-bland-fallback-factor` | 4 | Per-variable coefficient of `check()`'s Bland's-rule fallback point, `factor * variables + offset` pivots. |
| `--lra-bland-fallback-offset` | 64 | Constant term of the same threshold. |

**The three Check/Pivot knobs** were hardcoded constants until 2026-08-06 and
became options for the same reason the propagation caps did: so a tuning
campaign can vary them per run rather than per build. None can change an
answer, only how long it takes to reach one, and
`tuning_knobs_never_change_a_verdict` pins that. Full rationale and the
measured response surface are in [](tuning-parameters.md); the short version
is that `--lra-sparse-pricing-candidates` has a >100x range in pivot term ops
on a single file but a **spiky, non-monotone** profile, so it wants per-family
or portfolio selection rather than a new global default.

Related fix: `:lra.bland-fallbacks` incremented only under `PivotingRule::
Dantzig`, so it read 0 under `sparse` -- the default -- whether or not the
fallback fired. With that corrected the threshold turns out to bind on real
input (counts of 1-12 across the Heizmann files), which means some pivoting
attributed to `sparse` is actually running under Bland.

**The four propagation caps** bound the per-bound-change cost of the two
row-propagation directions. Their defaults are documented in the source as
"not precisely derived", and on the mid-band gap instances they gate the
bidirectional path off almost entirely: `occurs_in` reaches 521 and rows
reach 91 terms there, so it fires 11-68 times against a 7k-190k opportunity
count. They were compile-time constants until 2026-07-31 and became options
so that direction D1's sweep could vary them per run rather than per build.

**That sweep came back negative** (job 117726, 59 mid-band files at 300s):
raising all four to 128, 256 or 1024 turns propagation on by two to three
orders of magnitude and still does not beat the baseline's solved count, so
the defaults stand. The options remain because they cost nothing to keep and
are what made the question answerable. Full outcome in
[](qf-lra-heuristics.md)'s D1.

The reverse direction's two caps default smaller than the forward pair
because it runs an O(row size) child-bound computation for *each* other term
of each affected row, not once per row.

**Statistics.** All are cumulative per solve.

| statistic | meaning |
|---|---|
| `:lra.theory-checks` | full theory checks run (non-incremental path) |
| `:lra.theory-conflicts` | theory conflicts reported to the SAT solver |
| `:lra.theory-propagations` | literals propagated by the theory |
| `:lra.theory-decisions` | decisions supplied via `cb_decide` |
| `:lra.model-phase.refreshes` / `:lra.model-phase.hints` | H5 phase-refresh scans and changed hints sent to CaDiCaL |
| `:lra.online-check-calls` | `LraTableau::check()` calls from the online propagator |
| `:lra.pivots` | **repair** pivots inside `check()` only -- see the caveat below |
| `:lra.pivot-row-updates` | rows rewritten by pivots (Markowitz column factor) |
| `:lra.pivot-term-ops` | terms merged while rewriting them (row factor) |
| `:lra.bland-fallbacks` | `check()` calls that hit the anti-cycling fallback |
| `:lra.rows` / `:lra.tableau-vars` / `:lra.row-nonzeros` | prepared tableau shape |
| `:lra.row-propagations` / `:lra.bidirectional-row-propagations` | propagations by direction |
| `:lra.soi-multi-row-conflicts` / `:lra.soi-minimize-calls` / `:lra.soi-minimize-shrunk` / `:lra.soi-conflict-lits` | SOI-only |
| `:lra.fixed-variables` / `:lra.elimination-pivots` | item H7 |
| `:rational.promotions` / `:rational.big-operations` | item H2's small-to-GMP transitions |

**Caveat on `:lra.pivots`:** it is sampled as a delta around `check()`, so
item H7's elimination pivots -- which happen inside
`assert_upper`/`assert_lower` -- are **not** counted in it. Comparing
`:lra.pivots` across an H7 ablation is therefore misleading; use
`:lra.pivot-row-updates` / `:lra.pivot-term-ops`, which count every pivot's
work wherever it was triggered.

### Model construction for Boolean atoms in QF_LRA (fixed 2026-08-17)

`build_qf_lra_model` (`check_sat.cpp`) previously dropped every
non-arithmetic (plain Boolean) entry of `assigned_atoms` -- the SAT search's
own decided values -- before printing the model. Only Booleans resolved
during *preprocessing* (`preprocessing_model_assignments`) ever reached the
printed model; a Boolean atom the main SAT search alone decided defaulted
silently to `false` in `model_builder.cpp`'s congruence closure, regardless
of what was actually assigned. The search itself was sound throughout --
confirmed with runtime instrumentation showing the correct internal
assignment (e.g. `x=true, q=true`) discarded before printing -- this was
purely a model-printing defect.

Trigger: any `sat` QF_LRA problem containing a Real-sorted declaration
(used or not, anywhere in the problem) plus a Boolean atom the search
decides rather than preprocessing. Minimal repro:
`(declare-fun v () Real) (declare-const x Bool) (declare-fun q () Bool)
(assert (or x q))`. Fixed by folding `assigned_atoms`'s non-arithmetic
entries into the assignments passed to `build_composed_uf_lra_model`,
mirroring how the mixed QF_UFLRA path already folds in `euf_assignments`.
See [](fuzz-coverage-qf-lra.md) for why the existing QF_LRA fuzzing
infrastructure never exercised this shape, and
`tests/unit/qf_lra_solver_tests.cpp`'s
`prints_boolean_atoms_decided_by_search_in_qf_lra_model` and the
strengthened `monotone_elimination_rejects_atom_shared_across_polarities`
for the regression tests.

### Where QF_LRA work stands, and what is open (2026-08-07)

**The complete non-incremental QF_LRA division, 1,753 files, 300s**
(primo re-run 2026-08-26 at `16d0d96`, SLURM job 130137; references not
re-run -- z3 and cvc5 from 118382, Yices from 118904, OpenSMT from 119045,
and their PAR2 figures below are byte-identical to the 2026-08-07 reading,
which is the check that the two primo runs are comparable):

| solver | solved | PAR2 | wrong |
|---|---|---|---|
| **OpenSMT 2.9.2** | **1670** (95.3%) | **68068** | **0** |
| **primo `16d0d96`** | **1656** (94.5%) | **80616** | **0** |
| Yices 2.7.0 | 1645 (93.8%) | 80585 | 0 |
| z3 5.0.0 | 1584 (90.4%) | 128079 | 0 |
| cvc5 1.3.4 | 1511 (86.2%) | 181248 | 0 |

**The 2026-08-26 re-runs change nothing here.** primo scores 1656 against
job 120784's 1657 -- one file, inside the timeout band -- and holds second
place, 14 behind OpenSMT and 11 ahead of Yices. That is the expected
outcome rather than a disappointment: everything merged since 120784 is
QF_LIA machinery that QF_LRA never enters, plus a word-GCD change measured
flat on this fragment. The run exists to confirm that, and it does. PAR2
moved 79646 -> 81218 (2%), which is run-to-run variation at a 300s timeout,
not a regression anyone should chase.

primo moved from third to second on 2026-08-07, overtaking Yices on both
solved count and PAR2. The previous figure was 1631; the change is the three
Nelson-Oppen regression fixes plus the merged QF_LRA arithmetic work, measured
in [](nelson-oppen-qf-lra-regression.md). Against the pre-regression commit it
gained 26 files and lost none.

```{figure} _static/qf-lra-cactus.svg
:alt: Instances solved against per-instance time budget for OpenSMT, primo, Yices, z3 and cvc5, over the 654 instances not solved by every solver within 10s.
:width: 100%

Instances solved against the per-instance time budget, over the **654
instances that at least one solver did not finish within 10s**. The 1,099
instances every solver finished quickly are omitted: each contributed one
point to all five curves, so removing them subtracts the same constant from
every series and leaves every gap between curves intact, while the vertical
range stops being dominated by the shared easy majority. **The counts in the
legend are therefore of 656, not of 1,753** -- the table above has the full
numbers. Regenerate with `--drop-solved-by-all-under 10`; omit that flag for
the unfiltered version. Legend entries carry each solver's sat/unsat split
below the total. primo is job 130137 at `16d0d96` (2026-08-26); the
references are jobs 119045, 118904 and 118382, not re-run.
```

**The plot shows two crossovers the table cannot.** Yices is the fastest
solver at every budget up to 10s and OpenSMT is the strongest by 300s; they
trade places between 10s and 30s. primo is slowest of the three at short
budgets and overtakes Yices only between 60s and 300s:

| solved within | 0.1s | 1s | 10s | 30s | 60s | 300s |
|---|---|---|---|---|---|---|
| OpenSMT | 868 | 1103 | **1370** | **1527** | **1595** | **1670** |
| Yices | **1003** | **1173** | 1428 | 1525 | 1575 | 1645 |
| primo `16d0d96` | 800 | 1066 | 1303 | 1463 | 1537 | 1656 |
| z3 | 861 | 1028 | 1227 | 1346 | 1430 | 1584 |
| cvc5 | 711 | 945 | 1118 | 1209 | 1296 | 1511 |

(primo's row is the 2026-08-26 re-run; the reference rows are unchanged
data. Job 120784's primo read 768 / 1063 / 1305 / 1470 / 1543 / 1657 --
the same shape, within a handful of files at every budget.)

At a 10s budget Yices leads this division by 127 files over primo, OpenSMT is
second and primo is **third**. **Which solver is "strongest" is a function of the
timeout**, which is worth remembering whenever the 300s standing is quoted --
primo's second place exists at 300s and nowhere below 60s.

Worth noting against the previous run: primo improved at every budget from
**10s** up (1286 -> 1305, 1440 -> 1470, 1515 -> 1543, 1631 -> 1657) and
regressed at both short budgets -- 0.1s from 832 to 768, and 1s slightly, 1066
-> 1063. The fixes removed per-backtrack
and per-dispatch overhead that dominates long searches; on instances that
finish in under a tenth of a second there is nothing to recover and the
cross-link change's larger `RowEntry` is the visible term.

**OpenSMT still leads at 300s; primo is second, 14 behind it and 11 ahead of
Yices** (13 and 12 at the 2026-08-07 reading). Two separate lessons sit in that sentence, and they are worth keeping
apart.

The first is about the *reference set*. It was built incrementally -- z3 and
cvc5 first, Yices on 2026-08-03, OpenSMT the same day -- and primo's apparent
standing fell at each addition: ahead by 47, then second, then third, **with
its own score unchanged throughout**. That is the honest reading of a
three-reference comparison extended to five, and it is why "primo leads
QF_LRA" was never a safe claim to make from three. It is also why the recovery
to second on 2026-08-07 is a real result and the earlier slide was not a real
regression: this time the score moved, 1631 -> 1657.

**One family carries most of OpenSMT's remaining margin.** LassoRanker, the
division's largest at 419, is where the gap lives; `tropical-matrix` is the
sharpest single result on the board, OpenSMT 5 of 10 where every other solver
manages 1 or 0. Primo takes `2019-ezsmt`, `latendresse` and `tta_startup`, and
eleven of seventeen families tie exactly, ten of them saturated.

```{warning}
The per-family *counts* previously given here (LassoRanker 385 to 347,
2017-Heizmann 48 to 41, `2019-ezsmt` 103 to 95) were measured when primo
scored **1631** and are superseded. The run-log entry
`2026-08-07-qf-lra-full-division-recovery-measured` records the deltas that
supersede them -- LassoRanker **+25** and 2017-Heizmann **+1** against that
same 1631 baseline, with nothing lost -- so primo's current LassoRanker figure
is 372 rather than 347. That is *derived* from two recorded numbers, not
measured directly, which is why it is flagged here instead of being written
into the prose above: a fresh per-family breakdown of job 120784 has not been
computed.
```

**They are still complementary, not ranked.** 29 instances Primo solves and
OpenSMT does not, against 68 the other way; 23 fall only to OpenSMT, 8 only to
Primo, 8 only to Yices, 1 only to z3, none only to cvc5, and 40 defeat all
five. A portfolio of Primo and OpenSMT would solve more than either.

primo scores 1631 at `b6b8d55` against 1631 at `aa44320`, PAR2 95091 against
94992: independent confirmation that the model-production merge and the
frontend changes of 2026-08-03 are behaviour-neutral here.

**Zero wrong answers and zero cross-solver disagreements.** Every file in
this set carries `(set-info :status)`, so unlike the scrambled set the
harness's own wrong-answer column is live: 1,629 of the 1,753 carry a
checkable status, all five solvers agree with it on every file they answer,
and no file is unsolved for a non-standard reason. This is the strongest
correctness evidence QF_LRA has, and adding a fifth independent
implementation strengthened it rather than finding a crack.

77 files only Primo solves against 30 only z3 solves; against cvc5, 130 to
10.

**Per family**, every solver, sorted by size:

| family | n | opensmt | yices | primo | z3 | cvc5 |
|---|---|---|---|---|---|---|
| meti-tarski | 488 | 488 | 488 | 488 | 488 | 488 |
| LassoRanker | 419 | **385** | 381 | 347 | 331 | 266 |
| sc | 144 | 144 | 144 | 144 | 144 | 144 |
| sal | 107 | 107 | 107 | 107 | 107 | 107 |
| 2019-ezsmt | 105 | 95 | 99 | **103** | 100 | 99 |
| DTP-Scheduling | 91 | 91 | 91 | 91 | 91 | 91 |
| uart | 73 | 73 | 73 | 73 | 73 | 73 |
| tta_startup | 72 | **72** | 70 | **72** | 68 | 70 |
| 2017-Heizmann-UltimateInvariantSynthesis | 58 | **48** | 25 | 41 | 22 | 26 |
| miplib | 42 | 25 | **27** | 23 | 25 | 21 |
| spider_benchmarks | 42 | 42 | 42 | 42 | 42 | 42 |
| clock_synchro | 36 | 36 | 36 | 36 | 36 | 36 |
| TM | 25 | 25 | 25 | 25 | 25 | 25 |
| keymaera | 21 | 21 | 21 | 21 | 21 | 21 |
| latendresse | 18 | 11 | 13 | **15** | 8 | 0 |
| tropical-matrix | 10 | **5** | 1 | 1 | 1 | 0 |
| check | 2 | 2 | 2 | 2 | 2 | 2 |

Ten families are saturated -- every solver takes all of them -- so the
division is decided by seven. **`miplib` is the only family where z3 beats
Primo**, 25 to 23, consistent with King, Barrett & Tinelli finding that
CVC4's LP/MIP integration paid off on exactly that family and essentially
nowhere else in QF_LRA.

*`latendresse` is worth noting*: 15 of 18, ahead of every reference
including OpenSMT's 11 and cvc5's 0. It was 0/15 before [H3](#h3), and it is
the family the SOI paper was written for.

*`tropical-matrix` is the open question OpenSMT raises.* 10 instances, on
which the other four solvers score 1, 1, 1 and 0 -- a pattern that read like
an intrinsically hard family. OpenSMT's 5 says it is not.
Whatever it does there is worth understanding, and it is small enough to
profile.

**The 595-file scrambled subset is not representative.** It gave a +8 lead;
the division gives +47. The scrambled sample under-weights precisely the
families Primo is strongest on -- Heizmann, LassoRanker and latendresse
alone account for +42 of the +47. Comparisons quoted from the scrambled set
should be read as a hard sample rather than as a division result.

*What this is not.* Not a competition result. SMT-COMP uses a 1200s
timeout, competition-entrant binaries and its own scoring; these are
`contrib` builds at 300s, and no official SMT-COMP logs are available here.
Nothing about placement follows.

**How it got here.** On the scrambled subset, 369 solved at 60s on
2026-07-31 to 439, via [H8](#h8) (merge instead of
sort, +9/-0), [H9](#h9) (bounds-only backtracking,
+32/-11) and [H10](#h10) (delta-maintained
`occurs_in_`, +41/-0). Five other directions were measured and rejected in
the same period.

#### The 595-file scrambled subset at 300s, for comparison

**Comparison at 300s, all 595 scrambled competition files**
(job 118310, plus OpenSMT from job 119085; z3 and cvc5 were re-run for 118310
because the timeout differs from every stored result, and primo/z3/cvc5 were
*not* re-run for OpenSMT's addition):

| solver | solved | PAR2 |
|---|---|---|
| **OpenSMT 2.9.2** | **552** (92.8%) | **33907** |
| primo | 515 (86.6%) | 61303 |
| z3 5.0.0 | 507 (85.2%) | 66718 |
| cvc5 1.3.4 | 465 (78.2%) | 96616 |

**Zero cross-solver disagreements** over 595 files and four solvers -- the
strongest soundness signal this set can give, since it carries no
`:status`.

Primo and z3 are **complementary rather than one dominating**: 30 files only
Primo solves, 22 only z3 solves. Against cvc5 it is 59 to 9. Against OpenSMT
the gap is real rather than complementary: 11 files only Primo solves against
48 only OpenSMT solves, and 29 of the 595 fall to OpenSMT alone.

**The band this campaign was about is narrowed against z3, and not against
OpenSMT:**

| file size | n | opensmt | primo | z3 | cvc5 |
|---|---|---|---|---|---|
| 0-50 KB | 179 | 179 | 179 | 179 | 179 |
| 50-500 KB | 192 | **178** | 173 | 157 | 154 |
| 500 KB-5 MB | 174 | **166** | 134 | 141 | 118 |
| >5 MB | 50 | 29 | 29 | **30** | 14 |

The 500KB-5MB band was **-56 against z3** on 2026-07-29 and is **-7** now.
Against OpenSMT the same band is **-32**, on a set where Primo and z3 are
within seven files of each other overall. **z3 was flattering as a yardstick
here.** The campaign's diagnosis was not wrong -- that band is where the work
was needed, and 56 files of it were recovered -- but "most of the deficit
removed" was measured against the wrong reference, and the honest statement is
that a third of the original gap remains against the best solver now measured.
The >5MB band ties at 29-30 across all three, so nothing in that band is
specific to Primo.

*What this is not.* Not a competition result. SMT-COMP uses a 1200s
timeout, competition-entrant binaries and its own scoring; these are
`contrib` builds on one 595-file scrambled set, and no official SMT-COMP
logs are available here. It says Primo is competitive with z3 and behind
OpenSMT on this set at this budget, nothing about placement.

**How it got here.** 369 solved at 60s on 2026-07-31 to 439 at 60s and 515
at 300s, via [H8](#h8) (merge instead of sort, +9/-0),
[H9](#h9) (bounds-only backtracking, +32/-11) and
[H10](#h10) (delta-maintained `occurs_in_`, +41/-0).
Five other directions were measured and rejected in the same period.

**It is a constant factor, and a small one.** Re-run at 600s (job 117631),
Primo solves 45 of those 59, median 167s. Against a 60s budget that is a
median 2.8x. The payoff is near-linear, with no threshold to clear:

| speedup | files recovered at 60s (of 59) |
|---|---|
| 2x | 15 |
| 3x | 24 |
| 5x | 37 |
| 10x | 45 |

**The cost is fill-in.** Rows holding 4.1-5.8 nonzeros when prepared merge
21.7-91.1 terms during search, while entering columns reach up to 521 rows, so
a single pivot can cost ~47,000 rational operations.

**The open list is [](qf-lra-heuristics.md)'s section 6.5**, and after
2026-07-31 it is short: D2 floating-point guided basis selection, and D4
revised simplex with an LU-factored basis. Periodic refactorization was
considered and **rejected** -- for an explicit tableau the density is a
property of the basis reached, so there is nothing for it to discard.

Ruled out already, so as not to be re-proposed: the sparse-pricing candidate
cap (flat across a 60,000x range), fixed-equality slacks (item H7, ships
opt-in, nets zero on this band), preprocessing size (tableau dimensions
match z3's within ~5%), and **D1/D3, bound propagation across offset
equalities** -- swept over the whole band on 2026-07-31 and rejected: no cap
setting beats the baseline's solved count even with propagation firing 380x
harder, and the single instance that motivated it turned out to be
workstation noise at a timeout boundary. D1 and D3's cheap design are the
same code (`compute_implied_child_bound`), so that one sweep closed both;
D3's expensive column-merge design had already been rejected on its own
evidence.


- **Milestone 3b's real-benchmark impact**: a 500-case fuzz merge-gate
  re-run found zero wrong-answer disagreements against z3 (a real bug --
  `OnlineLraPropagator` clearing valid propagation reasons on backtrack --
  was found and fixed via this same gate, see the run-log). A quick 82-file
  diverse-sample re-run against `tmp/QF_LRA` showed no material aggregate
  change on that particular sample (44/82 solved vs. 45/82 before, within
  noise) -- same-slack propagation alone did not move the previously-0%
  families (invariant synthesis, lasso ranking, tropical-matrix), which
  suggests they need either scale-invariant polynomial matching or
  row-based/interval propagation (both explicitly out of scope here, see
  above) rather than same-slack propagation alone. Not a negative result
  for the milestone -- see `docs/run-log.json` for the full picture,
  including unit-test-level evidence (decision-count reduction on
  hand-built cases) that the mechanism itself works correctly.
- **Milestone 4b (done, 2026-07-24)**: the campaign-tier fuzz gate
  (AGENTS.md: >=3000 cases plus a seed-mutation campaign) that promotes
  QF_LRA from "experimental" to "supported"/default. Ran on the
  `llm2smt-slurm` cluster after fixing two remote-infrastructure gaps
  (missing fuzzsmt/z3/yinyang tooling; a workspace-sync rsync clobbering
  provisioned tooling with host-specific local paths -- see
  `docs/run-log.json` and the `feedback_fuzzing_on_slurm` memory).
  Result: 3000/3000 fuzzsmt cases (0 crash, 0 wrong, 149 timeout) plus
  opfuzz and two yinyang typefuzz mutation shards (14/14 real seeds each,
  0 bug triggers) -- fully clean. Covered the default (Bland) pivoting
  rule at campaign scale; Dantzig has only merge-gate-tier (500-case)
  local confirmation, consistent with it being opt-in rather than the
  behavior being promoted.

- **Competitiveness roadmap item 1 (2026-07-25): LRA-aware solve-eqs.**
  Real-equality lowering moved from parse time to a dedicated
  `lower_real_equalities` pass at the end of preprocessing (see
  "Disequality" above), so `substitute_solved_constants` ("solve-eqs")
  now fires on genuine Real `Equality` nodes instead of silently no-op'ing
  for QF_LRA. `rewrite_with_solved_constants` and
  `rewrite_with_ground_unit_equalities` were both extended to recurse into
  `Plus`/`Times`/`Le`/`Lt` so a substitution actually propagates through
  arithmetic subterms. Full suite (Release + ASAN/UBSan) and new unit
  tests pass; a local 500-case merge-gate fuzz smoke test (417 PASS, 83
  TIMEOUT, 0 wrong-answers/crashes) and a fresh campaign-tier Slurm run
  (job 113357, 2026-07-25: 3000/3000 fuzzsmt cases -- 2854 PASS, 146
  TIMEOUT, 0 CRASH, 0 WRONG -- plus opfuzz and two yinyang typefuzz
  mutation shards, 14/14 real seeds each, 0 bug triggers) both came back
  fully clean. This item is **done** and as trusted as Milestone 4b's
  disequality behavior -- see `docs/run-log.json`.
- **Competitiveness roadmap item 1 follow-up (2026-07-25, done): Gaussian
  elimination over asserted linear equality systems.** The plain
  recognizer above only catches `x = t` with a bare variable on one side;
  `--gaussian-elimination` (**default on**, `--no-gaussian-elimination` for
  ablation) additionally row-reduces (Gauss-Jordan, exact rationals) the
  whole system of asserted Real equalities, catching single equations with
  a coefficient (`2x = 5`) and genuine multi-equation systems (`2x+3y=5`
  and `x-y=1` jointly solving for both `x` and `y`) that the plain
  recognizer can't. Merges into the same candidate list/accept-reject
  pipeline the plain recognizer uses -- no new correctness logic there.
  Landed opt-in first (materially new algorithmic machinery, not just a
  recognizer extension), then promoted to default-on the same day once a
  merge-gate-tier Slurm smoke test (job 113406: 500 fuzzsmt cases, 0
  crashes/wrong-answers) and a campaign-tier Slurm run (job 113407: 3000
  fuzzsmt cases plus opfuzz + 2 yinyang mutation shards, 0 crashes/wrong-
  answers/bug triggers) both came back fully clean -- see
  `docs/preprocessing.md`'s "Gaussian Elimination" section for the design
  and `docs/run-log.json` for the full verification trail.
- **Competitiveness roadmap item 2 (2026-07-25): row-based/interval bound
  propagation.** See "Row-based/interval bound propagation" above for the
  design. Full suite (Release + ASAN/UBSan) and new unit tests pass. Opt-in
  (`--lra-row-propagation`), same staged-rollout reasoning as item 1's
  Gaussian elimination. A quick 17-file real-benchmark re-run (the same
  sample as this roadmap's original quick-eval) showed the mechanism does
  fire on several real files (`:lra.row-propagations` nonzero, up to
  thousands of propagations on some, confirmed via `--stats`, including on
  some of the still-0%-solved families) but did **not** move the sat/unsat
  outcome on that narrow 17-file/5s-timeout sample -- 6/17 solved either
  way, same set, no wrong answers. This mirrors Milestone 3b's own
  same-slack-propagation finding exactly (real work happening, timeout
  cases stay timeouts at this sample size/budget) -- not a negative
  result for the mechanism, just evidence that propagation reach alone
  isn't what's gating these particular hard families; see `docs/run-log.json`.
- **Competitiveness roadmap item 4 (2026-07-25): SOI-Simplex pivoting.**
  See "SOI-Simplex pivoting" above for the full design -- the complete
  paper algorithm (King/Barrett/Dutertre FMCAD 2013), not a scoped-down
  version, per explicit direction. Full suite (Release + ASAN/UBSan) and
  new unit tests pass, including a direct reproduction of the paper's own
  Fig 6 worked example. Opt-in (`--lra-pivoting-rule soi`). A real heap-
  use-after-free was found and fixed during manual smoke testing before
  any fuzzing (see "SOI-Simplex pivoting" above) -- a reminder that this
  item's trail/incremental-integration piece (genuinely new beyond the
  paper) was exactly the highest-risk area it was flagged as during
  design. A quick 17-file real-benchmark re-run (same sample as items
  1-2's) showed **identical results to Bland**: 7/17 solved, the exact
  same files, no wrong answers either way -- SOI's global objective
  didn't move this particular narrow sample, matching items 1-2's own
  honest findings that none of solve-eqs/row-based propagation/SOI-
  Simplex individually closes this sample's timeouts. See
  `docs/run-log.json`.
- **Competitiveness roadmap item 0 (2026-07-25, done last, out of
  priority order): empirical baseline vs. z3/cvc5.** A new reusable
  stratified sample, `benchmarks/qf-lra-stratified-sample.txt` (up to 15
  files per family, all 17 corpus families, fixed seed, 237 files --
  much broader than items 1/2/4's own 17-file one-per-family quick-eval
  sample), compared against z3 and cvc5 at a 10s timeout via the new
  `scripts/slurm_qf_lra_solver_compare.sbatch` template. **Solved: primo
  102/237 (43%), z3 167/237 (70%), cvc5 150/237 (63%); PAR2 primo 2799,
  z3 1531, cvc5 1885. Zero 3-way wrong-answer disagreements.** Crucially,
  the gap is **not uniform**: Primo fully matches both reference solvers
  on keymaera/meti-tarski/sal/spider_benchmarks/check, and all three
  struggle roughly equally on 2017-Heizmann-UltimateInvariantSynthesis/
  LassoRanker/tropical-matrix (confirming those are genuinely hard for
  everyone, not Primo-specific) -- but on **TM, sc, uart, 2019-ezsmt,
  tta_startup, and miplib, Primo is specifically and substantially
  behind** while z3/cvc5 mostly solve them within the same budget.
  Spot-checked TM/uart's failures: pure timeouts (not wrong/error/
  unsupported), with a threshold effect (solved cases finish in
  1.9-8.8s, failed ones hit the wall exactly at 10s) suggesting some
  specific size/structure crossing point, not uniform slowness. **This
  is the first genuine evidence of *where* Primo is behind** -- none of
  items 1/2/4 were evaluated against this broader, more representative
  family set (their own quick-evals used the narrow 17-file sample,
  dominated by the "hard for everyone" families) -- making the TM/sc/
  uart/2019-ezsmt/tta_startup/miplib gap a concrete, prioritized target
  for follow-up profiling before further engineering investment. Two
  real sbatch-script bugs (a delimiter collision, an argparse `nargs='+'`
  gotcha) and a broken cvc5 install (missing libc++ shared libraries,
  fixed via `contrib/install_cvc5.py` preferring static release assets)
  were found and fixed along the way. See `docs/run-log.json`.
- **Item 0 follow-up (2026-07-25): pivot() allocation profiling.**
  Profiled (`perf record`/`perf report`, not just `--stats` guesswork)
  the 3 most dramatic timeout-vs-instant cases item 0 surfaced
  (`tmp/QF_LRA/sc/sc-21.induction3.cvc.smt2`,
  `tmp/QF_LRA/uart/uart-18.induction.cvc.smt2`,
  `tmp/QF_LRA/TM/p5-zenonumeric_s5.smt2`). Finding: 50-60% of runtime was
  in malloc/free/GMP-allocation machinery, with `LraTableau::pivot()`'s
  per-affected-row combine step (a fresh
  `std::unordered_map<LraVarId, Rational>` built and torn down for every
  row touched by every pivot) as the dominant contributor;
  `set_soi_direction()` had the identical pattern. Fixed (commit
  `04a6c73`) by replacing both with a reused member vector, sort-merged
  instead of hashed -- a first attempt that just reused an
  `unordered_map` member as-is did **not** help (re-profiling showed the
  same signature unchanged), since `unordered_map` is node-based: every
  element is its own heap allocation no matter how the container itself
  is reused. Full suite (Release + ASAN/UBSan) green, no behavior change;
  merge-gate Slurm fuzz smoke (job 113938: 500 fuzzsmt cases, 471
  PASS/29 TIMEOUT/0 CRASH/0 WRONG) came back clean. Re-timing showed a
  real but mixed effect: TM/p5-zenonumeric_s5 dropped from 20.58s to
  11.05s (~47% faster); sc-21 and uart-18 improved more modestly/noisily.
  **Follow-up (2026-07-26, commit `2222b8f`):** re-profiling after the
  above still showed `std::__detail::_Map_base` hot -- `rows_`/
  `occurs_in_` were themselves `std::unordered_map<LraVarId, ...>` keyed
  by a dense small integer (`LraVarId` is a plain index into `vars_`,
  already a `std::vector<VarState>`), so every pivot's `rows_.erase`+
  `emplace` and `occurs_in_` insert/erase was its own per-call
  heap-allocated map-node churn, independent of the combine step already
  fixed. Converted `rows_`/`occurs_in_`/`soi_directions_` to plain
  vectors sized in lockstep with `vars_` (valid to do since
  `add_variable()`/`add_row()` are only ever called before the first
  `push()`, a documented class contract -- so their size never changes
  once solving starts); a nonbasic slot's stale content is harmless
  because `rebuild_occurs_in_index()` (the only place that ever iterated
  all of `rows_`) now gates by `is_basic` instead of by map-key presence.
  Full suite green again, `:sat.decisions`/`:sat.conflicts` bit-identical
  to the pre-vectorization commit (pure perf change). Merge-gate Slurm
  fuzz smoke on **both** pivoting paths came back clean (job 113939,
  Bland: 480 PASS/20 TIMEOUT/0 CRASH/0 WRONG; job 113940, `--lra-
  pivoting-rule soi`: 338 PASS/162 TIMEOUT/0 CRASH/0 WRONG -- SOI's
  higher timeout rate matches its already-known characteristic, not a
  regression). Re-profiling confirmed the `unordered_map` overhead is
  now gone entirely from the hot path; what remains is GMP's own --
  `mpq_class`'s arithmetic operators canonicalize (GCD-reduce) on every
  add/multiply by design, an inherent cost of exact rational arithmetic
  no container-level fix can remove. Re-timing: TM/p5-zenonumeric_s5 down
  further to 8.62s (~58% faster than the original pre-fix baseline);
  sc-21 43.24s, uart-18 18.04s. None of the 3 files come close to
  z3/cvc5's sub-1s times either way -- these two fixes close two real
  constant-factor costs, not the competitiveness gap itself.
  **Campaign-tier confirmation (2026-07-26, job 113941): clean.** 3000/3000
  fuzzsmt cases (2857 PASS, 143 TIMEOUT, 0 CRASH, 0 WRONG) plus opfuzz and
  two yinyang typefuzz mutation shards (14/14 real QF_LRA seeds valid
  each, 0 bug triggers across all three) on the default Bland path --
  both fixes are now trusted at the same level as any other default-on
  QF_LRA behavior, closing the campaign-tier gap the merge-gate-only
  entries above had left open. See `docs/run-log.json`.
- **Competitiveness roadmap item 5 (2026-07-26, done): violated-variable
  worklist in `LraTableau::check()`.** New `:lra.pivots`/
  `:lra.online-check-calls` stats (added while investigating the two
  allocation fixes above) showed `check()`'s Bland/Dantzig
  violated-variable scan and SOI's end-of-check error-var collection both
  doing a full `O(vars_.size())` linear scan over every variable, once
  per pivot-loop iteration, on every `check()` call -- 25-49% of which
  find nothing violated at all (e.g. TM/p5-zenonumeric_s5: 64,350
  `check()` calls for only 8,617 decisions, since propagated literals
  trigger the same eager repair as decisions do). Replaced both scans
  with iteration over a new incrementally-maintained
  `std::set<LraVarId> violated_vars_` -- `soi_direction(var)!=0` is
  exactly "var is basic and violated" by construction, so one set serves
  Bland, Dantzig, *and* SOI's error-var scan with no separate
  bookkeeping. Kept in sync going forward via `update_violation_status()`
  (called from `assert_upper`/`assert_lower`'s is_basic branch and
  `update()`'s two assignment-change sites); `pop()` rebuilds it from
  scratch rather than patching every undo case individually, mirroring
  `rebuild_occurs_in_index()`'s existing precedent. Commit `80b617b`.
  `:sat.decisions`/`:sat.conflicts`/`:lra.pivots`/
  `:lra.online-check-calls` bit-identical to the pre-item-5 commit on all
  3 profiled files (pure perf change, confirmed no selection-order
  change); wall-clock further improved: uart-18 18.04s->12.45s (~31%
  faster), TM/p5-zenonumeric_s5 8.62s->6.54s (~24% faster), sc-21
  slightly faster. Stacked with the two prior allocation fixes, TM/p5 is
  now roughly 2.5x faster than the original pre-optimization baseline
  (20.58s->6.54s). Full suite (Release + ASAN/UBSan) green, including a
  new push/pop regression test targeting the set's rebuild-after-pop
  invariant. **Merge-gate (jobs 113950 Bland, 113951 SOI) and
  campaign-gate (job 113952, Bland only, matching the SOI-stays-opt-in
  precedent) both clean**: 0 crash/wrong-answer across all three. See
  `docs/run-log.json`.
- **Competitiveness roadmap item 6 (2026-07-26, done): defer
  `LraTableau::check()` to CaDiCaL's `cb_has_external_clause`.**
  `OnlineLraPropagator::notify_assignment_lit` used to run the full
  Check/Pivot repair eagerly after *every* assigned literal (decisions
  *and* propagated ones) -- the code's own comment already flagged this
  as a known cost. Verified against CaDiCaL's own source
  (`contrib/src/cadical/src/{internal,external_propagate}.cpp`): in
  eager external-propagator mode (unchanged, `is_lazy` still defaults to
  `false`), `cb_has_external_clause()` is asked exactly once per
  quiescence point (once per decision plus once per conflict/backjump,
  not once per literal), and `cb_propagate()` is always fully drained
  first -- so same-slack/row-based propagation (which reads only bound
  state, never the current assignment) is completely unaffected by
  moving the repair there. Commit `28f573d`. A new `needs_check_` flag
  replaces the inline `tableau_.check()` call; `run_deferred_check_if_
  needed()` (called from both `cb_has_external_clause` overloads)
  resolves it lazily. **A real bug was found and fixed during local
  testing** (the existing black-box test
  `atoms_sharing_identical_polynomial_solve_correctly` caught it, not
  fuzzing): a direct same-variable bound conflict found on a later
  literal in a batch could leave an earlier literal's `needs_check_=true`
  flag dangling, so the deferred-check helper would later overwrite the
  genuine direct conflict with a stale (wrong) `tableau_.check()` result
  -- the rejected bound is never actually stored by `assert_upper`/
  `assert_lower`'s own direct-conflict branch, so `check()` alone
  wouldn't even rediscover it. Fixed by clearing `needs_check_` in that
  branch, plus a defensive guard in the deferred-check helper itself.
  Full suite (Release + ASAN/UBSan) green, including a new black-box test
  asserting `:lra.online-check-calls` stays bounded by
  `decisions+conflicts+1` regardless of how many extra atoms propagation
  resolves alongside. Measured on the 3 profiled files:
  `:lra.online-check-calls` dropped 57-95% (largest on TM/p5, the most
  propagation-heavy file); wall-clock further improved: sc-21
  40.93s->23.11s, uart-18 12.45s->6.24s, TM/p5-zenonumeric_s5
  6.54s->4.21s. Unlike item 5, `:sat.decisions`/`:sat.conflicts`/
  `:lra.pivots` are **not** bit-identical to the pre-item-6 commit --
  expected, since deferring conflict discovery legitimately reshapes the
  search trajectory (still fully correct, just a different valid path,
  matching the same reordering effect the two allocation fixes showed
  earlier). Given this touches CaDiCaL protocol integration rather than
  an internal-only data structure, verified at the highest bar of any
  QF_LRA change this cycle: **merge-gate (jobs 114174 Bland, 114175 SOI)
  and campaign-gate (jobs 114182 Bland, 114183 SOI) all clean** on both
  pivoting paths -- 0 crash/wrong-answer throughout. See
  `docs/run-log.json`.

  **Stacked across items 0-6**, the 3 originally-profiled files went
  from the pre-optimization baseline (sc-21 56.28s, uart-18 21.27s,
  TM/p5-zenonumeric_s5 20.58s) to sc-21 23.11s, uart-18 6.24s,
  TM/p5-zenonumeric_s5 4.21s -- roughly 2.4x-4.9x faster depending on the
  file. None of the 3 come close to z3/cvc5's sub-1s times on these same
  files either way: these items closed real constant-factor and
  call-volume costs, not the underlying competitiveness gap item 0
  measured.

  **Broader-sample check (2026-07-26, job 114200): re-ran item 0's exact
  237-file comparison against the post-item-6 code.** Solved: primo
  108/237 (45.6%, up from 102/43.0%), z3 167/237 (unchanged), cvc5
  150/237 (unchanged). PAR2: primo 2676s (down from 2799s, ~4.4%), z3/cvc5
  unchanged within jitter. Zero 3-way disagreements, same as before. Per
  previously-lagging family (primo solved/15, prior count in parens): TM
  6 (4), sc 5 (4), uart 4 (3), 2019-ezsmt 3 (3, unchanged), tta_startup 9
  (8), miplib 1 (1, unchanged). A real but modest aggregate gain -- exactly
  what the 3-file profiling result predicts, since a per-file speedup
  only flips a timeout to solved within this sample's fixed 10s budget if
  the post-fix time actually lands under 10s (uart-18's case would;
  sc-21's 23.11s still wouldn't). 2019-ezsmt and miplib not moving at all
  are the clearest candidates for a fresh profiling pass if this gap is
  revisited. See `docs/run-log.json`. Rendered as a colored per-instance
  HTML table via the new `scripts/render_solver_compare_html.py`
  (styled after `people.ciirc.cvut.cz/~janotmik/llm2smt/primo.html`) --
  now the standard way to report a solver-comparison result.

- **Competitiveness roadmap item 7 (2026-07-26, done, opt-in, not
  promoted): bidirectional row-based propagation.** Profiling one of
  item 0's 9 dramatic-gap files (`TM/p6-zenonumeric_s5.smt2`: 22.56s vs.
  z3 0.18s/cvc5 0.56s) and checking the *reference solvers' own* stats
  found the real story: z3's final LP tableau is tiny (36 rows/728
  columns) and cvc5 spent only 8ms of its 645ms runtime pivoting
  (`theory::arith::pivots = 129`, `unatePropagateCalls = 15,595`) --
  both win via far more complete propagation, not cheaper pivoting. A
  full Bromberger/Weidenbach-style implied-equality-basis computation
  was considered and set aside (paper-sized undertaking, and its own
  experiments don't measure SMT search speedup); instead this item
  implements item 2's own explicitly-flagged-as-deferred "reverse
  direction" -- `compute_implied_child_bound` derives a bound on one of
  a row's *other* terms from the row's own bound plus its remaining
  terms, instead of only deriving the row's own bound. Opt-in via
  `--lra-bidirectional-row-propagation` (requires `--lra-row-propagation`
  also on), its own smaller size caps, new stat
  `:lra.bidirectional-row-propagations`. Found and worked around a real
  pre-existing quirk while writing the tests (not introduced by this
  item): `single_variable_slack` only registers a bare variable's
  *positive*-coefficient normalized form, so `>=` atoms (which normalize
  to a negated, coefficient-(-1) polynomial) never get an entry --
  asserting both `<=` and `>=` on the same variable splits its bound
  across two slacks with only one direction visible on each. **Item H1a
  (2026-07-28) removed this quirk at the root**: `single_variable_slack` is
  gone entirely, and both directions now land on the variable itself.

  **Empirical result on the 9 dramatic-gap files: genuinely mixed, not a
  clear win.** Item 2 alone (already-shipped) turned out to help far
  more on this file set than its original narrow quick-eval suggested --
  pivot reductions on 7/9 files, sometimes dramatically (TM/p6:
  113,806->58,736 pivots, ~48% fewer). This item's *own* addition only
  ever fires on TM/uart's Boolean-guarded-equality structure (never on
  sc's min-max comparator chains) and is net-negative as often as
  positive: wall-clock wins on 3/9 (TM/p6 -44%, uart-16 -11%, uart-17
  -25%), losses on 4/9 (TM/p-DepotsNum, sc-17, sc-21, uart-18), roughly
  flat on 2/9 -- and the extra per-assertion scanning overhead
  measurably slows sc-17 down even on the zero occasions it fires there.
  **Not recommended for default-on promotion on this evidence** -- a
  genuinely file-shape-dependent lever. Merge-gate Slurm fuzz (job
  114203, both new flags explicitly enabled: 500 cases, 483 PASS/17
  TIMEOUT/0 CRASH/0 WRONG) clean; campaign-tier not pursued given the
  promotion decision, matching the precedent for other opt-in,
  not-promoted features (e.g. SOI-Simplex pivoting). See
  `docs/run-log.json`.

  Item 7 closed out items 0-7 of the roadmap; item 8 below picks the
  underlying TM/sc/uart-vs-z3/cvc5 gap back up from a different angle.

- **Competitiveness roadmap item 8 (2026-07-26/27, done, opt-in, pending a
  promotion decision): theory-guided decisions via CaDiCaL's `cb_decide`.**
  A follow-up literature pass ("are we missing something?", prompted by
  item 7's own "gap remains substantially open" framing) first chased and
  explicitly **retracted** a dead end: CaDiCaL's IPASIR-UP interface
  freezes every observed variable, disabling CaDiCaL's own inprocessing
  on them (Fazekas et al., "IPASIR-UP: User Propagators for CDCL", SAT
  2023, Section 2.4, calls this an open problem) -- but that same paper's
  own Table 2 shows cvc5-ipasirup (the identical frozen-variable
  architecture) *solving more* `QF_LinearRealArith` benchmarks in *less*
  aggregate time than native cvc5 (1,766 vs. 1,721 solved, 113,919s vs.
  130,408s), refuting the freezing theory for this division. The real
  lead, from the same paper plus a companion talk (Preiner, "CaDiCaL(T):
  CaDiCaL as CDCL(T) Engine in cvc5", Shonan 2023): CaDiCaL's `cb_decide`
  callback lets an external theory supply its own decision literal
  instead of leaving every decision to generic VSIDS -- cvc5-ipasirup
  uses it for "theory decisions (required): decision strategies used by
  theory solvers". `grep -rn cb_decide src` found **zero** overrides in
  either `OnlineEufPropagator` or `OnlineLraPropagator` -- every one of
  Primo's 119,793 decisions on `TM/p6-zenonumeric_s5.smt2` (vs. z3's 322
  and cvc5's 498 on the same file) was, and until this item remains by
  default, pure VSIDS.

  **First attempt (commit `17f8faf`, retired): a per-candidate cost
  ranking.** Implementing the "required" scope explicitly chosen by the
  user over a smaller "opportunistic, free-wins-only" cut, `cb_decide()`
  scanned a bounded, rotating window of not-yet-decided arithmetic atoms
  and ranked each candidate polarity via a new `estimate_assertion_cost`
  probe (counting how many rows referencing the candidate's variable
  would have their live-assignment-based implied value pushed outside
  their own existing bound), supplying the cheapest candidate's literal.
  On the 9 dramatic-gap files this was **genuinely mixed with real
  regressions**: two wins (`TM/p6-zenonumeric_s5` -76%, `TM/p-DepotsNum_s8`
  -13%) plus one small win, but three files flipped from solved to
  timed-out at the same 60s budget (`sc-17`/`sc-19`/`sc-21`) and two got
  markedly slower while still solving (`uart-16` ~3x, `uart-18` ~1.6x).

  **Re-investigated after being asked to check for a missed performance
  issue -- the ranking itself was the bug, not just its overhead.** A
  debug build that bypassed `estimate_assertion_cost` entirely (supply
  the next unassigned arithmetic atom's literal, no ranking at all --
  "cheap mode") beat *both* plain VSIDS *and* the cost-ranked design on
  8/9 files, sometimes by 40-96%, and was the only one of the three that
  solved `sc-27` at all within the shared budget. The cost ranking wasn't
  merely expensive -- it was actively steering search into worse
  territory (the three solved-to-timeout regressions were VSIDS beating
  the *smarter*-looking heuristic outright). Commit `fc28dbc` replaces
  `cb_decide()` with exactly that "cheap mode": supply the next
  not-yet-decided arithmetic atom's TRUE literal each call, advancing a
  persistent round-robin cursor, no per-candidate arithmetic at all.
  Removed `estimate_assertion_cost`, its fanout/scan-budget caps, and the
  now-unmeasurable `:lra.theory-decision-free` stat.

  **Final empirical result on the 9 dramatic-gap files (corrected design):
  a near-uniform win, zero net regressions.**

  | file | baseline | round-robin | delta |
  |---|---|---|---|
  | TM/p-DepotsNum_s8 | 53.3s | 11.1s | -79% |
  | TM/p6-zenonumeric_s5 | 59.1s | 2.0s | -97% |
  | sc-17 | 26.3s | 11.2s | -57% |
  | sc-19 | 42.9s | 18.8s | -56% |
  | sc-21 | 50.9s | 29.7s | -42% |
  | sc-27 | timeout (60s) | 22.3s, **solved** | unsolved -> solved |
  | uart-16 | 14.5s | 14.5s | flat (dec -31%, piv +21%) |
  | uart-17 | 19.3s | 18.3s | -5% |
  | uart-18 | 13.0s | 13.2s | flat (dec -59%, piv -31%) |

  7/9 clear wins (often 40-97%), 2/9 flat (both still show substantially
  fewer decisions, `uart-18` -59%), and `sc-27` -- unsolved by *both*
  plain VSIDS and the original cost-ranked design at this budget -- is
  now solved. No file regresses. Merge-gate Slurm fuzz (job 114234: 500
  cases, 482 PASS/18 TIMEOUT/0 CRASH/0 WRONG) and campaign-gate Slurm
  fuzz (job 114235: fuzzsmt 3000/3000 -- 2911 PASS/89 TIMEOUT/0 CRASH/0
  WRONG; opfuzz 14/14 real seeds valid/0 bug triggers; yinyang 2 shards/0
  bug triggers) both clean on the corrected design. Two black-box tests
  rewritten for the round-robin's actual semantics (decision order and
  conflict-driven correctness, not cost ranking). Full suite (Release +
  ASAN/UBSan) green throughout. See `docs/run-log.json` for the full
  trail, including the earlier (retired) cost-ranked design's own fuzz
  confirmation (jobs 114204/114205/114214) kept on record for the
  historical trail even though that design was replaced.

  **Broader 237-file re-verification (job 114244, per the user's explicit
  request before any promotion decision): the dramatic 9-file win does
  NOT generalize.** Re-ran item 0's stratified sample (only the primo
  task, diffed against the unchanged z3.csv/cvc5.csv from job 114200 --
  per the standing "don't rerun unchanged reference solvers" practice).
  Aggregate result is roughly flat, slightly negative: solved 108->106/237
  (-2), PAR2 2675.6s->2682.0s (+0.2%), 0 wrong-answer disagreements, but 4
  files regress from solved to unsolved (`LassoRanker/CooperatingT2/
  p-43...`, `sal/gasburner-prop3-19`, `tta_startup/simple_startup_8nodes...`,
  `uart-10.base`) against only 2 that newly solve. The round-robin order
  that dominates on the TM/sc/uart "dramatic-gap" family (Boolean-guarded
  chains of many similar arithmetic atoms, where *any* stable decision
  order beats VSIDS's churn) is a wash-to-net-negative once families with
  different structure (LassoRanker, sal, tta_startup) are included --
  those 9 files were never a representative sample, they were specifically
  the ones z3/cvc5 found easy and Primo didn't, which turns out to
  correlate with exactly the file shape this heuristic helps. Rendered as
  a colored per-instance HTML table via `scripts/render_solver_compare_
  html.py`, matching standard practice.

  This closes out all of the QF_LRA competitiveness roadmap's
  currently-planned items (0-8). **Staying opt-in, not recommended for
  default-on promotion** on the broader evidence -- despite being the
  most dramatic *narrow*-sample win in the roadmap, it doesn't clear the
  bar item 0-6's unconditional/promoted changes did (broad-sample
  improvement, not narrow-sample-only). A useful lever for exactly the
  TM/sc/uart Boolean-guarded-chain file shape (worth keeping available via
  the flag), not a general-purpose default. This is also a concrete
  lesson for how this roadmap evaluates future items: the 9-file
  dramatic-gap sample is useful for *finding* techniques worth trying, but
  not sufficient on its own for a promotion decision -- the 237-file
  broader sample is the real bar, as it was for items 0-6.

- **Item 0 follow-up (2026-07-27, done, unconditional): deferred
  `occurs_in_` rebuild in `LraTableau::pop()`.** Asked to gprof-profile
  `TM/p5-zenonumeric_s5.smt2` (one of the 3 files item 0's follow-up
  originally profiled) turned up a real, previously-missed bug:
  `rebuild_occurs_in_index()` showed up at ~9% self-time in the flat
  profile, second only to `pivot()` itself, with a call count (17,665)
  almost exactly matching the file's total pivot count (18,097). Root
  cause: `undo()` called it once per `PivotTrailEntry`/`FRowTrailEntry`
  it undid, not once per `pop()` call -- so a single `pop()` unwinding N
  pivots at once (a full multi-decision-level backtrack) did N redundant
  full `O(vars_.size())` rebuilds instead of one. The function only ever
  reads `rows_`/`is_basic` (never `occurs_in_` itself), so there's no
  ordering dependency across the undo loop -- deferring it to run once at
  the end of `pop()`, mirroring the existing `rebuild_violated_vars()`
  precedent, is sound. Re-profiling confirms the call count drops to 535
  (-97%) on the same file.

  Verified with a paired same-node, same-time A/B on the Slurm remote
  (alternating before/after binary runs to control for cluster-load drift,
  which had made a naive historical comparison misleading -- the cluster
  was running noticeably slower than when items 0-6 were first measured,
  confounding a straight before/after against old documented numbers):
  `TM/p5-zenonumeric_s5` -10.6% (9.92s->8.87s avg of 3 runs),
  `sc-21.induction3` -4.2% (50.51s->48.40s), `uart-18.induction` -9.4%
  (13.18s->11.95s). `:sat.decisions`/`:lra.pivots` bit-identical
  before/after on all three (pure perf change, no selection-order
  change, matching every other item-0-follow-up fix's own invariant).
  Full suite (Release + ASAN/UBSan) green. Since this touches
  unconditional core Check/Pivot code, verified at both merge-gate (jobs
  114707 Bland: 500 cases, 472 PASS/28 TIMEOUT/0 CRASH/0 WRONG; 114708
  SOI: 500 cases, 341 PASS/159 TIMEOUT/0 CRASH/0 WRONG) and campaign-gate
  (jobs 114709 Bland: fuzzsmt 3000/3000 -- 2882 PASS/118 TIMEOUT/0
  CRASH/0 WRONG, opfuzz 14/14 valid/0 bugs, yinyang 0 bugs; 114710 SOI:
  fuzzsmt 3000/3000 -- 2035 PASS/965 TIMEOUT/0 CRASH/0 WRONG, opfuzz
  14/14 valid/0 bugs, yinyang 0 bugs) tiers, on both pivoting paths,
  matching the precedent set by the original `occurs_in_`/`rows_`/
  `soi_directions_` vectorization fix and the `violated_vars_` worklist
  item. See `docs/run-log.json` for the full trail.

**Next round of heuristics (2026-07-27, designed, not implemented):** see
[](qf-lra-heuristics.md) for a prioritized design note picking the
remaining 61-file gap back up from fresh profiling and tableau-shape
instrumentation at commit `35878c4`. Its headline findings: ~54% of runtime
is allocator/GMP-copy overhead on *tiny* numbers (correcting item 0's
follow-up conclusion that the remaining cost was inherent GCD
canonicalization -- measured at under 2%); 36-95% of tableau rows on the
lagging families are trivial single-variable slack rows that Dutertre & de
Moura's own Section 2 would not create at all; and rows densify 3-5x during
search (one variable reaching 2,127 `occurs_in_` entries) while none of
Bland/Dantzig/SOI prices a pivot by its cost. The note also reclassifies
the gap into three distinct bottleneck classes (pure-LP single-`check()`,
pivot-cost-dominated, and assignment-volume-dominated) rather than one.

**Not yet scheduled** (natural next candidates, not designed in detail):
Devex or Steepest-Edge pivoting (Milestone 4a covered only Dantzig, and
SOI-Simplex, item 4, is now done without closing the quick-eval sample's gap --
neither pricing rule choice nor a global objective alone seems to be the
bottleneck for the "hard for everyone" families in that narrow sample); a
campaign-tier confirmation specifically for `--lra-pivoting-rule dantzig`/`soi`
if either is ever considered for becoming the default. Scale-invariant
polynomial matching, formerly roadmap item 3, is implemented by H1b; see
[](qf-lra-heuristics.md).
