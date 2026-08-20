# QF_LIA: Branch-and-Bound, GCD-Consistency, and Gomory Cuts on the Existing Simplex

Stage 1 implemented 2026-08-19, stages 2 and 3 implemented 2026-08-20.
QF_LIA is accepted (`(set-logic QF_LIA)`) via a plain branch-and-bound
decision procedure layered entirely on top of the existing `LraTableau`,
a read-only GCD-consistency pre-check, and a Gomory-cut pre-check --
together closing several specific, documented gaps in plain
branch-and-bound's completeness, though not all of them (see
[Known incompleteness](#known-incompleteness)). None of the three stages
change `OnlineLraPropagator`, and only stage 3 touches `LraTableau` at
all, and even then only by *calling* its existing, already-public
`add_row()`/`add_variable()` API -- see
[Gomory cuts](#gomory-cuts) for why no change to the class itself was
needed.

Three sources grounded this design:

- Dutertre & de Moura, *Integrating Simplex with DPLL(T)* (SRI-CSL-06-01) --
  already the basis of `LraTableau`; see `docs/papers/fast-linear-arithmetic-dpllt.md`.
  No integer content; cited here only because every QF_LIA bound assertion
  still goes through the exact same Assert-Bound/Check machinery it
  describes.
- Bjørner & Nachmanson, *Arithmetic Solving in Z3* (CAV 2024), Sect. 4:
  stage 1 uses Sect. 4.4's plain branch-and-bound (on a fractional integer
  basic variable with value β(x), split on `x <= floor(β(x))` vs
  `x >= floor(β(x))+1`); stage 2 uses Sect. 4.3's GCD-consistency test (see
  [GCD-consistency](#gcd-consistency) below for the exact statement used,
  including a likely erratum found in the paper's own inequality direction
  and worked around by independent re-derivation rather than trusted
  as-printed). This paper motivated *wanting* Gomory cuts (Sect. 4.5) but
  gives no cut-coefficient formula of its own -- it points elsewhere for
  that, which is where the third source comes in.
- Tim King, *Effective Algorithms for the Satisfiability of
  Quantifier-Free Formulas Over Linear Real and Integer Arithmetic* (PhD
  thesis, NYU 2014, `docs/papers/king_tim.pdf`) -- already cited in
  `lra_tableau.hpp` for pivoting-rule details. Stage 3's Gomory cut comes
  from Sect. 4.4.3, Lemma 4.1, eq. 4.9 ("Mixed Gomory Cuts"); see
  [Gomory cuts](#gomory-cuts) for the formula and, importantly, why this
  specific variant was chosen over a self-derived alternative.

All three techniques are single layers of a larger waterfall (patch →
cube → GCD → branch-and-bound/Gomory, per Bjørner & Nachmanson); none of
them, individually or together, is claimed to make this a complete
decision procedure -- see [Known incompleteness](#known-incompleteness),
which this doc accepts and reports rather than hiding.

## Scope

QF_LIA per SMT-LIB: `Int`-sorted variables only, linear terms
(`+ - *`(literal coefficient)), atoms `< <= > >= = distinct`. No `div`/`mod`
(not yet implemented), no mixing `Int` with `Real` in one problem (that is
QF_LIRA, out of scope), no uninterpreted functions.

## Where branching lives

Z3 injects the branch literal into CDCL itself (`cb_decide()`-style), so
conflict-driven learning generalizes across branches. Doing the same thing
here would mean calling `new_observed_var()`/`add_clause()` from inside a
callback on the live `OnlineLraPropagator` -- a class that is already
dense, performance-tuned, and carries a documented soundness argument that
depends on exactly *when* CaDiCaL calls `cb_check_found_model` versus
`cb_has_external_clause` (see that class's own comment in `check_sat.cpp`).
Bolting a new mid-search literal-injection path onto that machinery was
judged out of scope for a first, deliberately simple implementation.

Instead, `solve_qf_lia_impl` (a new function in `check_sat.cpp`,
structurally a close parallel of `solve_qf_lra_impl` -- not a modification
of it) hooks in at the point that function already reaches once a full
Boolean+LRA assignment is confirmed consistent, right before it would
return `Sat`: a **self-contained, theory-internal branch-and-bound DFS**
runs on a private tableau copy, using only
`LraTableau::push()`/`pop()`/`assert_upper()`/`assert_lower()`/`check()`/
`assignment()` -- all already incremental and reusable exactly as they are,
since branch-and-bound only ever asserts new bounds on *existing* tableau
variables (rows/variables are fixed after the first `push()`, which
branch-and-bound respects).

- All-integer, or the search finds an integer completion: build the model
  from the winning tableau state, same as QF_LRA does today.
- Every branch infeasible: build a `sat::Clause` blocking the whole current
  arithmetic-atom assignment (coarse -- every arithmetic atom's blocking
  literal, not a minimized explanation) and feed it back through the
  existing `backend.add_clause(...)` + re-`solve()` loop, the same
  mechanism an ordinary LRA conflict already uses just above it. Sound
  (excludes at least this assignment) but not tight; a known quality gap,
  not a soundness gap. Tightening this (e.g. deriving a real Farkas-style
  certificate from the failed branches, or moving to CDCL-integrated
  branch literals so conflict learning generalizes) is future work, not
  blocking for stage 1.
- Node limit hit: `Unknown`, `"QF_LIA branch-and-bound node limit reached"`.

## The search: `theory::arithmetic::branch_and_bound`

`src/theory/arithmetic/lia_branch_and_bound.{hpp,cpp}`. Given an
already-LRA-sat tableau and the list of `Int`-sorted tableau variables, an
**explicit-stack DFS** (per this project's standing no-recursion
convention) finds the first fractional `Int` variable, splits on
`floor`/`floor+1`, and backtracks by re-pushing/popping the tableau at
each frame's checkpoint. `sat::Lit{0}` is used as the reason literal for
every branch bound -- the established codebase convention (see
`check_sat.cpp`'s `augmented_lra_atoms` sites) for a tableau bound with no
real SAT literal behind it; these synthetic-reason bounds and whatever
conflict certificate `check()` derives from them never escape the
function, since a failed branch is just discarded, not turned into a
clause.

**Contract on the tableau's final state**: on `Sat`, the tableau is left
exactly at the winning leaf. On `Unsat`/`LimitReached`, every bound this
call asserted has been popped -- but per `LraTableau::pop()`'s own
documented contract, that restores bounds only, not the assignment or
basis (Dutertre & de Moura Section 3.2.4's bounds-only backtracking
scheme). A caller comparing pre/post state must compare bounds
(`upper_bound()`/`lower_bound()`), not `assignment()`; see the tests in
`tests/unit/lia_branch_and_bound_tests.cpp` for the exact shape of that
check.

## Int in the frontend

- `core::SortKind::Int`, registered in `SymbolTable`'s constructor next to
  `Bool`/`Real`; `SymbolTable::int_sort()` mirrors `real_sort()`.
- `TermManager::mk_rational/mk_plus/mk_times/mk_le/mk_lt` are now
  sort-parametric (Real or Int, matching between operands) rather than
  hardcoded to `real_sort()`; `mk_eq` was already sort-agnostic. A
  one-argument `mk_rational(value)` overload still defaults to
  `real_sort()` so the ~25 pre-existing QF_LRA call sites (production and
  test) needed no changes.
- `LraAtom::normalize_atom` (`lra_atom.cpp`) is unchanged, and deliberately
  stays sort-agnostic -- see the next section for why the integer-specific
  strictness handling lives in `check_sat.cpp`'s `bound_assertion()`
  instead of here.
- `lower_real_equalities` (`preprocessing.cpp`) -- the pass that rewrites
  `Equality` into `And(Le, Le)` so that `NOT(a=b)` reduces to ordinary
  Boolean structure instead of needing case-split machinery in
  `LraTableau` -- now also fires on `Int` operands (was Real-only). This
  is not optional for QF_LIA: without it, `distinct`/disequality
  atoms reach the tableau as a raw negated `Eq`, which
  `assert_lra_atom_bound` explicitly rejects ("disequality... not yet
  supported").
- `check_sat.cpp`'s `SetLogic` whitelist accepts `QF_LIA`; `declare-fun`
  rejects uninterpreted functions and `Real` constants under it (and
  rejects `Int` constants under any other logic).
- Every *optional* preprocessing pass (Gaussian elimination, monotone
  elimination, solved-constant substitution, atom canonicalization, ...)
  is turned off for QF_LIA (`options.preprocessing_mode =
  PreprocessingMode::Off`, forced once `frontend.logic() == "QF_LIA"` is
  known). None of those passes have been audited for `Int` soundness (e.g.
  Gaussian elimination divides by a pivot coefficient, which does not
  preserve integrality); turning the whole optional pipeline off sidesteps
  that audit rather than risking a silent wrong answer. `lower_real_equalities`
  and Boolean flattening are unaffected -- they are not gated by
  `preprocessing_mode`.

### Where integer strictness actually gets handled, and two bugs fuzzing found

QF_LIA needs *some* mechanism to turn a strict `<`/`>` atom (or a
non-strict `Le` atom assigned **false** -- see below) into an integer
threshold instead of the Real-only infinitesimal-`delta` trick. Two
designs were tried during development; the first shipped and was then
disproven by local FuzzSMT differential fuzzing against z3
(`scripts/fuzz_external.py --fuzzer fuzzsmt --logic QF_LIA`), which is why
this section documents the wrong one too -- both bugs are exactly the kind
a change here could reintroduce.

**Where the strictness actually originates.** It is not only explicit
`<`/`>` atoms. `finish_ite` desugars a non-Boolean `ite` and `distinct`
lowers to pairwise `NOT(=)`, and `lower_real_equalities` turns every
`Equality` (Real or Int) into `And(Le, Le)` -- so `NOT(a=b)` becomes
`NOT(And(Le, Le))`, i.e. each `Le` can be assigned **false** by the SAT
search. A non-strict `Le` atom assigned false is exactly as "strict" as an
explicit `Lt` atom assigned true (`t <= c` false means `t > c`), and this
is the *normal* path for every QF_LIA disequality, not an edge case.
`bound_assertion()` in `check_sat.cpp` is the one place both cases funnel
through (it already existed, computing the Real delta-trick bound from an
atom's relation/rhs/orientation/truth value) -- so it is the one place
that needs to become integer-aware, not `LraAtom::normalize_atom`.

**Bug 1 (fixed in the same change that added QF_LIA): missing the
negated-`Le` path entirely.** The first attempt only tightened atoms whose
*syntactic* relation was `Lt`, inside `normalize_atom` (`e < c` rewritten
to `e <= c - 1`, before `PreparedAtom` even exists). That left every
negated-`Le` case still using the Real delta-trick, so a `distinct` over
two Int constants left a `DeltaRational` with a nonzero `infinitesimal` on
one variable -- `find_fractional_variable` correctly saw it as "not an
integer" forever, but no integer bound assertion could ever clear a delta
no integer bound assertion had produced. A tightly-bounded 2-variable
`distinct` (`a, b in [1,2], a != b`) branched past 100,000 nodes on what
should have been a handful.

**Bug 2 (found by fuzzing, fixed the same day): a naive `rhs +/- 1` on a
possibly-non-integer `rhs`.** The first fix moved strictness handling into
`bound_assertion()`, gated by a new `PreparedAtom::is_integer` field, and
applied `rhs + (is_upper ? -1 : 1)`. This is only correct when `rhs` is
already an integer. `PreparedAtom::rhs` is *not* the original atom's
right-hand side -- `PreparedLraTheory`'s scale-canonicalization (item
H1b) divides a single-variable atom's polynomial and rhs by that
polynomial's own coefficient, so e.g. `5x < 0`'s tightened, canonicalized
form has `rhs = -1/5`, not an integer, even though the original atom and
its variable are both `Int`-sorted. `-1/5 - 1 = -6/5` is nonsense as an
integer threshold. 100 local FuzzSMT cases against z3 turned up 8 wrong
`unsat` answers (`OUR_UNSAT_REF_SAT`) from exactly this; the minimized
repro was `(assert (ite (< x (* x (- 4))) (= (* x (- 4)) x) (>= (* x (- 4)) x)))`,
satisfiable at `x = 0`.

**The fix, and the invariant to preserve.** `bound_assertion()` now uses
`rhs.ceil() - 1` for a strict upper bound and `rhs.floor() + 1` for a
strict lower bound (`util::Rational::ceil()`/`floor()`, added alongside
`is_integer()` for this purpose) -- correct whether or not `rhs` is
already an integer, since `ceil()`/`floor()` are identity on an integer
input. `LraAtom::normalize_atom` was reverted to fully sort-agnostic (the
Bug-1 fix's atom-level tightening is now redundant and was removed, since
`bound_assertion()` alone handles both the explicit-`Lt`-true and
negated-`Le`-false cases correctly, from the one place that sees the
final, possibly-scaled `rhs`). **The invariant this restores, and that any
future change here must preserve**: no `DeltaRational` assigned to an
`Int`-sorted tableau variable may ever carry a nonzero `infinitesimal`,
regardless of what coefficient the original atom had before
canonicalization. `find_fractional_variable` depends on this silently; a
violation shows up as a variable that looks permanently fractional no
matter how tightly it gets bounded (bug 1's symptom) or as a wrong answer
from a bound that was never actually an integer threshold (bug 2's
symptom).

**Fuzzing evidence after both fixes**: ~850 FuzzSMT-generated QF_LIA cases
across multiple seeds and size ranges, answer-differential against z3, 0
wrong answers; ~400 of the `sat` cases' models independently re-checked by
substituting primo's own reported values back into the original formula
and confirming z3 still accepts it, 0 bad models. All done locally with
`contrib/local/bin/{fuzzsmt,z3}` (both already built in this checkout);
see [Fuzzing](#fuzzing) below for what a real Slurm-scale campaign (the
next step, not yet run) adds on top of this.

## GCD-consistency

Stage 2, `theory::arithmetic::gcd_inconsistent_row`
(`src/theory/arithmetic/lia_gcd_consistency.{hpp,cpp}`). A read-only
structural check over the tableau's own row data -- zero `LraTableau`
mutation, called once per outer solve round, right before
`branch_and_bound` runs, in `solve_qf_lia_impl`.

**Statement.** For a row `x_b = Σ_j a_j·x_j`, fold every currently-*fixed*
variable in the equation `x_b - Σ_j a_j·x_j = 0` (both `x_b` itself and
any fixed `x_j`) into a residual constant, leaving an equation over the
remaining free variables. Since every QF_LIA variable is `Int`-sorted,
that reduced equation `Σ free_coeff·free_var = residual` is satisfiable
over the integers iff `gcd(free_coeff)` divides `residual`; if not, no
integer assignment can satisfy the row at all -- a sound, unconditional
conflict, independent of every other bound in the problem.

`x_b` itself is folded in on the same footing as any other term, not
treated as special, because *which* variable in a given linear
relationship happens to be basic is a Simplex pivoting detail that can
change without changing which underlying variables are actually fixed --
confirmed necessary while implementing this: an earlier version that only
checked "is the row's *basic* variable fixed" silently missed the
motivating `2x - 2y = 1` example whenever a pivot happened to leave `x` or
`y` (rather than the equality's own slack) basic.

**Recovering integer coefficients.** `PreparedLraTheory`'s
scale-canonicalization (item H1b) can leave a row's *stored* coefficients
non-integer even when the original atom's were integer (`2x - 2y = 1`
canonicalizes to coefficients `(1, -1)` with the fixed value `1/2`, not
`1`) -- the same "canonicalization can introduce fractions" trap
`bound_assertion()`'s stage-1 bug already burned us on once (see below).
The fix is analogous but simpler: feed the free coefficients and the
residual together into `util::Rational::primitive_integer_scale`
(already used for exactly this in `canonical_polynomial`) to get one
consistent integer scale, then take `util::Rational::gcd` (a new utility,
alongside `primitive_integer_scale` in `rational.cpp`) of the scaled
coefficients and check the scaled residual divides it evenly.

**Scope of the check, deliberately narrow.** This implements only the
"basic"/degenerate case from Bjørner & Nachmanson Sect. 4.3 (their
Example 5: every variable in the row is fixed, folding the whole row to a
single residual) -- not their "extended" generalization (a
bounded-but-not-*fixed* subset of a row's variables, using induced bounds
`l = a·lb(x)`, `u = a·ub(x)`). Re-reading the paper's printed inequality
for the extended case at 600dpi during stage-2 planning found what looks
like a direction erratum (`⌊u/gcd⌋ > ⌈l/gcd⌉` as printed, versus what a
hand-derivation from the degenerate case `l=u` actually implies); rather
than trust either the paper's direction or a guessed correction, the
extended case is left for a future stage once it can be independently
verified. The interleaved-with-branching cube test (Sect. 4.2,
Bromberger–Weidenbach rounding) is also deferred -- it needs a genuine
strengthened-LP solve and only helps find `sat` faster, it doesn't address
the `Unknown`-on-`unsat` gap this stage targets.

**Not interleaved into `branch_and_bound`'s own per-node loop.** The check
runs once, before branching starts. Running it again after each branch
(which could catch cases that only become GCD-detectable once branching
has narrowed a row's free variables) is a natural, easy-to-add follow-up,
deferred here since the single pre-check already closes the documented
gap.

## Gomory cuts

Stage 3, `theory::arithmetic::apply_gomory_cuts`
(`src/theory/arithmetic/lia_gomory_cuts.{hpp,cpp}`). Runs in
`solve_qf_lia_impl` right after stage 2's `gcd_inconsistent_row`
pre-check and before `branch_and_bound`, on the same per-round
`lia_tableau` copy those two already operate on.

**The formula**, King's thesis (NYU 2014) Sect. 4.4.3, Lemma 4.1, eq.
4.9, "Mixed Gomory Cuts" -- chosen over an independently-derived
pure-integer variant for a specific, load-bearing reason: King's cut
requires only that the *basic* variable be integer-valued in every model
and currently fractional. It places **no integrality requirement on
non-basic bound values**, which matters here because
`PreparedLraTheory`'s scale-canonicalization can leave a literal
fractional bound on a genuinely `Int`-sorted variable (`(<= (* 3 x) 5)`
stores a bound of `5/3`, not `1`) -- the same effect stage 1's
`bound_assertion()` bug and stage 2's `primitive_integer_scale` usage
both already had to account for. A self-derived cut assuming integer
bounds would need to handle that case separately and risks being
*wrong*, not just weaker, if the assumption were silently violated;
King's mixed cut sidesteps the question by not needing it.

Uses `LraTableau::Row`'s own representation directly -- King's tableau
row `x_i = Σ_{j∈N} T_{i,j} x_j` (basic variable, no separate additive
constant) is exactly how `LraTableau::Row` already stores a row, so no
convention translation was needed anywhere in this derivation.

For a basic, integer-sorted `x_i` with fractional current value
`a_i = β(x_i)`, let `f_0 = a_i - ⌊a_i⌋` (so `f_0 ∈ (0,1)`, never `0` or
`1`, since `a_i` is fractional by construction -- both `f_0` and `1-f_0`
below are always safe, nonzero divisors). Partition the row's non-basic
terms by which bound each currently sits at and the sign of its
coefficient `T_{i,j}`: `J⁺` (at lower bound `l_j`, `T_{i,j}>0`), `J⁻`
(lower, `T_{i,j}<0`), `K⁺` (at upper bound `u_j`, `T_{i,j}>0`), `K⁻`
(upper, `T_{i,j}<0`). The cut:

```
Σ_{J⁺} T_{i,j}/(1-f_0) · (x_j - l_j)  -  Σ_{J⁻} T_{i,j}/f_0 · (x_j - l_j)
  -  Σ_{K⁺} T_{i,k}/f_0 · (x_k - u_k)  +  Σ_{K⁻} T_{i,k}/(1-f_0) · (x_k - u_k)  ≥  1
```

**Eligibility**: `x_i` is basic, drawn from the caller-supplied
`integer_vars` (the declared `Int` problem variables -- exactly what
`branch_and_bound`'s own `find_fractional_variable` already scans, not
slacks: a slack's row can carry non-integer coefficients from
scale-canonicalization, so a slack is not guaranteed integer-valued in
every model the way a declared `Int` variable is, and King's theorem
needs that guarantee specifically for the row's basic variable), and
`β(x_i)` is not integer; every term in its row must have its variable
currently *exactly* at one of its bounds (`β(x_j) == lower_bound(x_j)` or
`== upper_bound(x_j)`, not merely bounded -- `LraTableau`'s own
documented deviation is that a non-basic variable may sit strictly
*within* its bounds, and a term doing that disqualifies the *whole* row,
which is then skipped entirely rather than partially cut).

**Translating to a tableau row + bound**: expand each `(x_j-l_j)`/`(x_k-u_k)`
term and move every `l_j`/`u_k` constant onto the right-hand side, giving
a per-term coefficient and one threshold constant, computable directly
from the row and each term's bound (`src/theory/arithmetic/lia_gomory_cuts.cpp`
has the exact expansion). `add_variable()` a fresh cut slack,
`add_row(cut_slack, {...})` with those coefficients, then
`assert_lower(cut_slack, threshold, synthetic_reason)`. Then `check()` --
either it finds a conflict (this branch is proven infeasible, blocked
with the same coarse whole-assignment SAT-level lemma stage 1/2's own
`Unsat` path already builds) or the tableau is strictly tighter and
`apply_gomory_cuts` looks for the next eligible row (up to
`--lia-gomory-cut-limit` rounds).

**No `LraTableau` change was needed for mid-search `add_row()`, once the
actual cut lifetime is thought through.** `add_row()`'s body computes the
new row's assignment from whatever the *current* nonbasic assignments
already are -- it does not assume a pristine, pre-search tableau -- and
touches no `trail_` state. Cuts here live entirely inside
`solve_qf_lia_impl`'s per-round `lia_tableau`, a plain copy of the live
search tableau rebuilt fresh every outer SAT round. Since that copy is
discarded at the end of every round regardless, a cut added to it never
needs to be *undone* -- it only needs to survive for the rest of that one
round, which `add_row()` followed immediately by `assert_lower` (whose
`is_basic` branch already calls `update_violation_status()`) and then
`check()` already handle correctly, matching the existing production
precedent for calling `add_row()` on a live, already-pushed tableau
(`prepare_lra_equality_probe_context`, `check_sat.cpp` ~line 6822). A
speculative `RowTrailEntry`/backtrackable-row `LraTableau` extension was
considered while planning this stage and turned out to be unnecessary --
not added.

**Worked examples** (hand-derived and independently verified by direct
boundary enumeration before any code was written; both are now
`tests/unit/lia_gomory_cuts_tests.cpp` test oracles):

- All-`J⁺`: row `x_i = 2x_1 + 3x_2`, `x_1` at lower bound `0`, `x_2` at
  lower bound `1/2` (`x_i = 3/2`, `f_0 = 1/2`). Cut: `4x_1 + 6x_2 >= 4`.
  The current fractional vertex `(0, 1/2)` gives `3 < 4` (excluded); the
  nearest achievable integer point `(0, 2/3)` (`x_i=2`) gives exactly `4`
  (included, tight).
- Mixed `J⁺`/`K⁻`: row `x_i = 2x_1 - 3x_2`, `x_1` at lower bound `1/3`,
  `x_2` at upper bound `-1` (`x_i = 11/3`, `f_0 = 2/3`). Cut:
  `6x_1 - 9x_2 >= 12`. The current vertex `(1/3, -1)` gives `11 < 12`
  (excluded); the nearest achievable integer point `(1/3, -10/9)`
  (`x_i=4`) gives exactly `12` (included, tight).

**A concrete extension demonstration**: `3x + 5y = 7, x,y >= 0` has no
non-negative integer solution (`y=0` needs `x=7/3`; `y=1` needs `3x=2`;
`y>=2` alone exceeds `7`), confirmed independently against z3 (`unsat`).
The LP relaxation is feasible (e.g. `x=7/3, y=0`), so plain
branch-and-bound needs to actually branch to discover the infeasibility
-- with `--lia-branch-limit 1` it cannot, reporting `Unknown`. With
Gomory cuts enabled, the very first candidate model's cut alone resolves
it to `Unsat`, no branching needed at all
(`tests/unit/qf_lia_solver_tests.cpp`'s
`gomory_cut_resolves_what_branch_and_bound_alone_cannot`).

## Node limits

`CheckSatOptions::lia_branch_limit` (default 100000) and
`CheckSatOptions::lia_gomory_cut_limit` (default 100), CLI
`--lia-branch-limit`/`--lia-gomory-cut-limit` (`kGroupLia`, "QF_LIA theory
options" in `--help`). Neither is yet empirically tuned against real
QF_LIA benchmarks. Stage 2's GCD-consistency check catches the
`2x - 2y = 1`-shaped case (a row whose value is *fixed* to something its
coefficients' gcd cannot reach) before either budget is touched. Stage
3's Gomory cuts extend that to cases like `3x+5y=7, x,y>=0` above, where
no single row is ever *fixed* but a cut still proves infeasibility
directly. A structurally different divergent case -- `4x + 4y` bounded to
a *range* that contains no multiple of 4 (e.g. `[2, 3]`, with `x, y`
unbounded) -- is caught by **neither** stage 2 nor stage 3 (confirmed
directly: `x, y` unbounded means neither ever sits *at* a bound, so the
row is Gomory-ineligible too, exactly as it was already GCD-ineligible)
and still hits `--lia-branch-limit` deterministically; see the next
section.

## Known incompleteness

Branch-and-bound, GCD-consistency, and Gomory cuts together are *still*
not a complete decision procedure for QF_LIA. Two independent gaps
remain, both demonstrated directly rather than just asserted:

- An integer-infeasible system whose LRA relaxation stays feasible at
  every branch-and-bound node, and whose infeasibility isn't witnessed by
  any single fixed row or any row with every term sitting *at* a bound,
  can still branch forever. `4x + 4y` bounded to `[2, 3]` with `x, y`
  unbounded is exactly this case: GCD-consistency has nothing fixed to
  check, and Gomory cuts have nothing to cut either -- unbounded `x, y`
  never sit *at* a bound, so every candidate row is Gomory-ineligible too
  (confirmed directly: the case still hits `--lia-branch-limit` with both
  pre-checks enabled). `tests/unit/lia_branch_and_bound_tests.cpp`'s
  `unbounded_polytope_hits_the_node_limit` (exercises `branch_and_bound`
  in isolation) and `tests/unit/qf_lia_solver_tests.cpp`'s
  `gcd_uncatchable_divergent_system_hits_the_node_limit` (end to end) both
  confirm this.
- Even where cuts and branch-and-bound together resolve any *individual*
  instance, this project does not implement Gomory's specific
  lexicographic anti-cycling discipline (real MIP theory: without it, a
  naive cut-and-branch loop has no hard termination guarantee against an
  adversarial input) -- `--lia-branch-limit`/`--lia-gomory-cut-limit`
  remain the honest, blunt fallback for whatever this doesn't resolve in
  practice, reporting `Unknown` rather than hanging.

Bjørner & Nachmanson's waterfall (patch → cube → GCD → branch-and-bound/Gomory)
still has two layers this project has not implemented at all -- the cube
test, and GCD-consistency's own "extended" (bounded-but-not-fixed)
generalization, both noted as deferred where they were first discussed
above -- either of which could plausibly close some of the gap the
`4x+4y in [2,3]`-shaped case represents. A future stage, not attempted
here.

## Explicitly out of scope

- The cube test (Bjørner & Nachmanson Sect. 4.2) and GCD-consistency's
  extended/bounded-subset generalization (Sect. 4.3) -- see
  [GCD-consistency](#gcd-consistency) for why the latter specifically was
  deferred (a likely erratum in the paper's own printed formula).
- Interleaving Gomory cuts into `branch_and_bound`'s own per-node loop --
  cuts run once, as a pre-check, before branching starts, not per-branch.
- The paper's nearest-integer-distance Gomory row-selection heuristic --
  `apply_gomory_cuts` uses the first eligible row instead, matching the
  simplicity level `branch_and_bound`'s own `find_fractional_variable`
  already uses.
- Exploiting non-basic integrality for a stronger cut than King's mixed
  formula gives -- King's own thesis flags this as a known possible
  improvement, citing a further reference not investigated here.
- Gomory cuts persisting across SAT rounds -- each round's `lia_tableau`
  is discarded and rebuilt fresh; a cut derived in one round is not
  carried into the next, even if the same fractional row reappears.
- Large-coefficient temporary-scope handling for Gomory cuts (Bjørner &
  Nachmanson mention this for numeric stability on adversarial inputs) --
  `--lia-gomory-cut-limit` is the blunt safety valve instead.
- CDCL-integrated branch literals (Z3-style, injecting a fresh SAT literal
  from a callback) -- a possible future performance phase, not needed for
  correctness.
- `div`/`mod`, mixed QF_LIRA (`Int` + `Real` together).
- Any change to `LraTableau` or `OnlineLraPropagator` *themselves* --
  stages 1 and 2 stayed entirely read-only or bound-assertion-only
  against the existing API; stage 3 adds rows/bounds via `LraTableau`'s
  own already-public `add_variable()`/`add_row()`, requiring no change to
  the class (see [Gomory cuts](#gomory-cuts) for why that turned out to
  be sound without one).

(Fuzzing itself is *not* out of scope -- see the next section, including
each stage's own Slurm-scale verification.)

## Fuzzing

**Local smoke fuzzing** (already run, see the bug writeups above for
numbers): `contrib/local/bin/fuzzsmt` generates raw QF_LIA instances
directly (FuzzSMT supports the logic natively; no wrapper changes were
needed), checked against `contrib/local/bin/z3` via
`scripts/fuzz_external.py --fuzzer fuzzsmt --logic QF_LIA --refs z3`.
Models were separately spot-checked by substituting primo's own reported
values back into the original formula and re-checking with z3, since
Dolmen (the usual model-validator, see `scripts/validate_model.py`) is not
installed in every environment. Reproduce with, e.g.:

```
PATH="contrib/local/bin:$PATH" python3 scripts/fuzz_external.py \
  --fuzzer fuzzsmt --solver ./build/primo --refs z3 --logic QF_LIA \
  --count 200 --timeout 5 --seed <any> \
  --fuzzer-args '-ms 1 -Ms 4 -mv 1 -Mv 5 -mf 1 -mp 1 -ma 1 -Ma 4' \
  --out /tmp/qf-lia-fuzz-smoke
```

**Slurm-scale gate** (`scripts/slurm_qf_lia_model_fuzz_gate.sbatch`): a
fresh sibling of `scripts/slurm_qf_lra_model_fuzz_gate.sbatch`'s
FuzzSMT-shard portion -- several FuzzSMT shards generated fresh per array
task, each checked answer-differentially against z3 and, for `sat`
results, model-validated with Dolmen (`scripts/validate_model.py`).
Unlike the QF_LRA gate, there is no opfuzz/typefuzz mutation-fuzzing stage
here yet, because that stage mutates a committed seed corpus
(`benchmarks/seeds/qf_lra_division`) and no equivalent QF_LIA corpus
exists (`benchmarks/seeds/qf_lra_*` are gitignored generated directories,
not tracked; see that gate's own comment on why). Adding one is natural
future work once real QF_LIA benchmarks are collected, not a blocker for
running the FuzzSMT-only gate as-is.

**Bug 3, found running the gate itself on the cluster (2026-08-19, job
127439): Dolmen's default `--strict=true` rejects real FuzzSMT QF_LIA
output.** FuzzSMT's QF_LIA generator routinely produces terms like
`(* e5 e11)` where `e5` is a let-bound literal (`1`) and `e11` a compound
expression -- still linear (a literal times a linear expression is
linear) but stricter than QF_LIA's SMT-LIB grammar, which only allows
`(* <literal> <symbol>)`. Dolmen's default treats this as a Fatal Warning
and aborts `--check-model` before it ever evaluates the model, which fell
through to `validate_model.py`'s Real-only z3 fallback -- a no-op for an
Int model (it finds no `() Real` assignments to extract), reported as
`model-error`: 39/45 `sat` cases in that run. `--dolmen-arg=--strict=false`
(now the gate's default) fixes it: rechecked the same 45 cases, all 45
pass via Dolmen directly (job 127440, same day). The z3 fallback itself
stays Real-only/unfixed -- not needed once Dolmen accepts the input.

**Status (stage 1)**: full default-sized gate (8 shards x 500 = 4000
FuzzSMT-generated QF_LIA cases) run on the actual Slurm cluster,
2026-08-19, job 127441 (see `docs/run-log.json`'s
`2026-08-19-qf-lia-fuzz-gate-slurm-verification` entry for the complete
record, including the two smaller jobs, 127439 and 127440, that found and
confirmed the fix for bug 3 above). Aggregate results: answer-differential
3926 PASS, 73 benign solver timeouts, 1 `UNKNOWN_INVOLVED`, **zero wrong
answers**; Dolmen model validation 3528 pass, 423 correctly `not-sat`, 48
benign timeouts, 1 `solver-unknown`, **zero invalid models**. The one
non-`pass` case beyond timeouts (`fuzzsmt_227445__file_247.smt2`) is primo
honestly reporting `unknown` after exhausting `--lia-branch-limit` on a
z3-`sat` instance -- confirmed via its `solver.stderr`, which reads
exactly "QF_LIA branch-and-bound node limit reached" -- i.e. the
documented incompleteness above, not a bug. That single case did make the
gate's shard task exit nonzero the first time (the benign-outcome
whitelist didn't yet include `solver-unknown`); fixed in the gate script
afterward, with a comment on exactly what assumption ties it to being
genuinely benign and when to revisit it.

**Status (stage 2)**: GCD-consistency is a soundness-risk change (a bug
would produce a false `Unsat` on a satisfiable instance, strictly worse
than stage 1's honest `Unknown`), so verification here was deliberately
heavier: ~1000 cases of local FuzzSMT smoke fuzzing across 5 seeds first
(all clean), *then* the full default-sized gate on Slurm, 2026-08-20, job
127449 (`docs/run-log.json`'s
`2026-08-20-qf-lia-gcd-consistency-fuzz-gate-slurm-verification` entry).
All 8 shards exited 0. Aggregate: answer-differential 3925 PASS, 74 benign
solver timeouts, 1 `UNKNOWN_INVOLVED` (a solver-side timeout on a z3-`sat`
instance, individually inspected -- unrelated to the GCD check), **zero
wrong answers**, confirmed by directly grepping every shard's summary for
`OUR_SAT_REF_UNSAT`/`OUR_UNSAT_REF_SAT`/`REF_DISAGREEMENT`/`CRASH` (none
found) rather than trusting the aggregate PASS count alone. Dolmen model
validation: 3517 pass, 425 correctly `not-sat`, 58 benign timeouts,
**zero `model-error`**, same direct-grep confirmation. No soundness
regression from stage 1's own already-verified baseline.

**Status (stage 3)**: Gomory cuts are a *second* soundness-risk change,
same category as stage 2 -- a wrong cut can prove `Unsat` on a
satisfiable instance -- so the same heavier verification discipline
applied. The formula itself was hand-verified against two independently
constructed worked examples *before* any tableau-integration code was
written (see [Gomory cuts](#gomory-cuts) above), and
`tests/unit/lia_gomory_cuts_tests.cpp` locks those in as regression
tests. ~800 cases of local FuzzSMT smoke fuzzing across 4 seeds followed
(all clean), then the full default-sized gate on Slurm, 2026-08-20, job
127457 (`docs/run-log.json`'s
`2026-08-20-qf-lia-gomory-cuts-fuzz-gate-slurm-verification` entry). All
8 shards exited 0 -- cleaner even than stage 2's equivalent run, which
had one benign `solver-unknown` case; this run had none. Aggregate:
answer-differential 3920 PASS, 80 benign solver timeouts, **zero wrong
answers**, confirmed by directly grepping every shard's summary for
`OUR_SAT_REF_UNSAT`/`OUR_UNSAT_REF_SAT`/`REF_DISAGREEMENT`/`CRASH` (none
found). Dolmen model validation: 3487 pass, 456 correctly `not-sat`, 57
benign timeouts, **zero `model-error`**, same direct-grep confirmation.
The hand-derived cut formula -- the trickiest piece of numerics across
all three stages -- produced zero disagreements against z3 across the
full campaign, on top of zero disagreements in ~800 local cases and the
two hand-checked worked examples before that.

## Tests

- `tests/unit/rational_tests.cpp`: `floor()`/`ceil()`/`is_integer()`
  (stage 1) and `gcd()` (stage 2), including the big (`mpq_class`)
  representation path for each.
- `tests/unit/lra_atom_tests.cpp`: confirms `normalize_atom` stays
  sort-agnostic (an `Int` `Lt` atom comes out exactly like a `Real` one) --
  see the bug writeups above for where the actual integer-strictness
  handling lives instead.
- `tests/unit/lia_branch_and_bound_tests.cpp`: white-box tests of
  `branch_and_bound` directly against `LraTableau` -- no branching needed,
  one branch to a solution, every branch infeasible (`Unsat`), and the
  node-limit/incompleteness case above. Unchanged by stage 2 on purpose:
  it exercises `branch_and_bound` in isolation, bypassing the new GCD
  pre-check, to keep that class's own behavior pinned down independently.
- `tests/unit/lia_gcd_consistency_tests.cpp` (stage 2): white-box tests of
  `gcd_inconsistent_row` directly against `LraTableau` -- the motivating
  `2x-2y=1` row, the same row in its scale-canonicalized (fractional
  coefficient/bound) form, a coprime-coefficient row that must **not**
  false-positive (the critical soundness check -- a false positive here
  means reporting `Unsat` on something satisfiable), an unfixed
  (bounded-but-not-fixed) row that must be skipped, a trivial
  coefficient-1 row, and a row fixed to zero (always achievable regardless
  of coefficients).
- `tests/unit/lia_gomory_cuts_tests.cpp` (stage 3): white-box tests of
  `apply_gomory_cuts` directly against `LraTableau`, using the two
  hand-verified worked examples above -- an all-`J⁺` row and a mixed
  `J⁺`/`K⁻` row, each checked semantically (does the resulting tableau
  correctly *exclude* the exact fractional vertex the cut was derived
  from, and correctly *admit* the nearest genuine integer point, rather
  than asserting on the cut's exact internal coefficients) -- plus a case
  where the cut alone proves `Unsat` (variables fixed so the cut's own
  slack is forced outside its new bound), a row with one term sitting
  strictly between its bounds (must be skipped entirely, not partially
  cut), and `cut_limit=0` as a guaranteed no-op.
- `tests/unit/qf_lia_solver_tests.cpp`: black-box, end-to-end via
  `check_sat_smtlib`, covering the same shapes through the full frontend
  plus `distinct`, nested/nontrivial branching, the strict-tightening bug
  above exercised end to end, the frontend rejections
  (UF/`Real`-under-QF_LIA/division/`Int`-under-other-logics), (stage 2)
  both `gcd_inconsistent_system_is_unsat_without_branching` (the motivating
  case, now `Unsat` immediately even under a node limit far too small for
  branch-and-bound alone to ever reach it) and
  `gcd_uncatchable_divergent_system_hits_the_node_limit` (the
  `4x+4y in [2,3]` case neither GCD-consistency nor Gomory cuts catch,
  confirming the node limit is still reachable and the remaining
  incompleteness is real, not a leftover artifact of an unexercised code
  path), and (stage 3) `gomory_cut_resolves_what_branch_and_bound_alone_cannot`
  (the `3x+5y=7, x,y>=0` case: `Unknown` under `--lia-branch-limit 1`
  with cuts off, `Unsat` with cuts on, at the *same* branch budget).
