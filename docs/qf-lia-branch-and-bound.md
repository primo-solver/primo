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
- Every branch infeasible: build a `sat::Clause` and feed it back through
  the existing `backend.add_clause(...)` + re-`solve()` loop, the same
  mechanism an ordinary LRA conflict already uses just above it. Stage 1
  built that clause coarsely -- every arithmetic atom's blocking literal,
  not a minimized explanation. Sound (it excludes at least this
  assignment) but so weak that the outer loop degenerated into model
  enumeration; **stage 4 replaced it with the integer stages' own
  derivation, see "Conflict generalization" below.**
- Node limit hit: `Unknown`, `"QF_LIA branch-and-bound node limit reached"`.

## The search: `theory::arithmetic::branch_and_bound`

`src/theory/arithmetic/lia_branch_and_bound.{hpp,cpp}`. Given an
already-LRA-sat tableau and the list of `Int`-sorted tableau variables, an
**explicit-stack DFS** (per this project's standing no-recursion
convention) finds the first fractional `Int` variable, splits on
`floor`/`floor+1`, and backtracks by re-pushing/popping the tableau at
each frame's checkpoint. `kDerivedBoundReason` (`sat::Lit{0}`) is used as
the reason literal for every branch bound -- the established codebase
convention (see `check_sat.cpp`'s `augmented_lra_atoms` sites) for a
tableau bound with no real SAT literal behind it. Stage 1 discarded the
conflict certificates `check()` derived from those bounds; stage 4 keeps
them, minus the synthetic literal itself (see "Conflict generalization").

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
- Every *optional* preprocessing pass under `options.preprocessing_mode`
  (Gaussian elimination, monotone elimination, finite-domain
  at-most-one, UF symmetry breaking, ...) is turned off for QF_LIA
  (`PreprocessingMode::Off`, forced once `frontend.logic() == "QF_LIA"`
  is known). Most of that pipeline has nothing to fire on anyway --
  QF_LIA's scope excludes uninterpreted functions -- but Gaussian
  elimination and monotone elimination specifically are not yet known
  sound over Int (Gaussian elimination divides by a pivot coefficient;
  monotone elimination's model reconstruction can produce a fractional
  witness for an Int variable), so turning the whole pipeline off
  sidesteps auditing each pass individually rather than risking a silent
  wrong answer. `lower_real_equalities` and Boolean flattening are
  unaffected -- they are not gated by `preprocessing_mode`.
  `docs/open-leads.md` lead 12 tracks the QF_LIA preprocessing/search-gap
  investigation this note is a summary of.
- Bare-form solved-constant substitution (`x = t`, `x` a nullary
  declared symbol not occurring in `t` -> replace every occurrence of
  `x` with `t`) is enabled by default for QF_LIA regardless -- called
  directly, in a small capped loop, bypassing the large pipeline above
  entirely. It is pure syntactic substitution with no coefficient
  scaling or division, so it needed no Int-specific audit the way the
  passes above do. `arithmetic_atom_canonicalization` was also
  attempted and reverted: `primitive_integer_scale` reduces
  already-integer coefficients by their gcd, which is exact for Real
  but needs the same ceil/floor rhs tightening `bound_assertion()`
  already does elsewhere -- caught by this project's own test suite
  before it shipped. See lead 12a in `docs/open-leads.md` for the full
  writeup, including the separate `build_qf_lia_model` bug (defaulted
  an eliminated variable's value to 0) this change's own testing found
  and fixed.

### Where integer strictness actually gets handled, and three bugs found here

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

**Bug 3 (found by a real-world benchmark evaluation, not fuzzing -- see
[Real-world benchmark evaluation and the fuzzing gap](#real-world-benchmark-evaluation-and-the-fuzzing-gap)
below): `ceil()`/`floor()` tightening applied to a non-integer *slack*.**
Bug 2's fix made `bound_assertion()`'s strict-bound tightening correct for
any *integer-valued* `rhs`-bearing variable, but `PreparedAtom::is_integer`
was still set from a single check -- "is the original atom's term
`Int`-sorted" -- with no regard for *which* tableau variable the bound
actually lands on. For a single-variable atom that variable is the
declared `Int` variable itself, always genuinely integer-valued, so bug
2's fix is sound there. But for a **compound** (multi-variable) atom, the
bound lands on a fresh slack satisfying `slack = canonical.polynomial`,
i.e. the original integer-coefficient polynomial divided by
`canonical.scale` (`PreparedLraTheory`'s scale-canonicalization, item
H1b) -- and `slack` is only itself integer-valued when that division was
by `+-1`. For any other scale (routine for terms with a large
leading-variable coefficient, e.g. a bit-position weight like `2^32` in
the `convert` family below), `slack` takes on genuinely fractional values,
and `ceil()`/`floor()`-tightening its own value is unsound: it can
fabricate a bound that excludes integer polynomial values the original
atom never ruled out. Concretely, an atom whose scaled `rhs` was
`-5/2^32` (visibly close to, but not, an integer) got tightened to a
lower bound of `0` as though the slack itself had to land on a whole
number -- when the atom's real content only ruled out `slack < -5/2^32`,
a much weaker constraint. **Fix**: `PreparedLraTheory` now only sets
`is_integer` true for a compound atom's slack when `canonical.scale` is
exactly `+-1`; otherwise `bound_assertion()` falls back to the always-sound
Real delta-trick for that atom, the same fallback bug 1 originally lacked.
This mirrors a distinction `lia_gomory_cuts.cpp`'s `find_eligible_row`
already had to make (see its own comment): a slack's row can carry
non-integer coefficients, so a slack is not guaranteed integer-valued the
way a declared `Int` problem variable is.

**Fuzzing evidence for bugs 1-2** (recorded at the time, before bug 3 was
found): ~850 FuzzSMT-generated QF_LIA cases across multiple seeds and size
ranges, answer-differential against z3, 0 wrong answers; ~400 of the `sat`
cases' models independently re-checked by substituting primo's own
reported values back into the original formula and confirming z3 still
accepts it, 0 bad models -- all done locally with
`contrib/local/bin/{fuzzsmt,z3}`. This same FuzzSMT-based fuzzing (later
run at full Slurm-campaign scale too, several times, across all three
stages -- see [Fuzzing](#fuzzing) below) never found bug 3, for a
structural reason, not luck: see
[Real-world benchmark evaluation and the fuzzing gap](#real-world-benchmark-evaluation-and-the-fuzzing-gap),
which also has the post-bug-3-and-4 verification numbers.

## GCD-consistency

Stage 2, `theory::arithmetic::gcd_inconsistent_row`
(`src/theory/arithmetic/lia_gcd_consistency.{hpp,cpp}`). A read-only
structural check over the tableau's own row data -- zero `LraTableau`
mutation, called once per outer solve round, right before
`branch_and_bound` runs, in `solve_qf_lia_impl`.

**Statement.** For a row `x_b = Σ_j a_j·x_j`, fold every currently-*fixed*
variable in the equation `x_b - Σ_j a_j·x_j = 0` (both `x_b` itself and
any fixed `x_j`) into a residual constant, leaving an equation over the
remaining free variables. A row's *term* variables (`x_j`) are always
declared QF_LIA problem variables (see `PreparedLraTheory`'s row
construction), so they always range over all of Z; `x_b` needs its own
check for this (see the bug note below) since it can be a
scale-canonicalized slack instead. Once every free variable is confirmed
to range over Z, the reduced equation `Σ free_coeff·free_var = residual`
is satisfiable over the integers iff `gcd(free_coeff)` divides `residual`;
if not, no integer assignment can satisfy the row at all -- a sound,
unconditional conflict, independent of every other bound in the problem.
If `x_b` is unfixed and not confirmed integer, the row carries no GCD
information at all and is reported consistent without being examined
further -- see the bug note.

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

**Bug 4 (found by the same real-world benchmark evaluation as bug 3,
same underlying mistake in a second place -- see
[Real-world benchmark evaluation and the fuzzing gap](#real-world-benchmark-evaluation-and-the-fuzzing-gap)):
an unfixed slack treated as ranging over Z.** The original implementation
folded `x_b` into `free_coefficients` (coefficient `1`, implicitly
assuming it ranges over all of Z) whenever it was not *fixed* --
regardless of whether `x_b` was a declared `Int` problem variable or a
scale-canonicalized slack. A slack representing the original polynomial
divided by a non-unit scale is not integer-valued at all (the same
distinction bug 3 above and `lia_gomory_cuts.cpp`'s `find_eligible_row`
both had to make), so treating it as Z-ranging could fold a fractional
row term's value into the residual as though it had to land on a whole
number, fabricating a conflict on a row whose actual value range was
never inconsistent with anything. Concretely: a slack `x_b = x + y/2^20`
with `x`, `y` fixed integers and `x_b` itself bounded only on one side by
a strict, non-integer-tightened bound (i.e. genuinely free within a
narrow real range, never claimed to be an integer by anything) was
flagged Unsat purely because `x_b`'s own assumed-integer coefficient
made the residual's gcd division come out non-integer. **Fix**:
`gcd_inconsistent_row` now takes the same `integer_vars` list
`branch_and_bound`/`apply_gomory_cuts` already use, and only folds an
unfixed `x_b` into `free_coefficients` when it is a member; otherwise the
row is reported consistent immediately (see the updated Statement above).
This does not weaken the check for any row whose basic variable is
genuinely a declared `Int` variable (including one that became basic
through an unrelated pivot) -- only for the case a fractionally-scaled
slack is basic and unfixed, where the old code had nothing sound to
conclude anyway.

**Bug 5 (found by mutation fuzzing, not the real-world benchmarks --
see [Fuzzing](#fuzzing)'s "Bug 4" writeup for the full repro and fix):
bug 4's own fix was incomplete.** It checked `is_declared_integer` for
`x_b` but not for the row's *other* terms, on the reasoning that those
always come from `PreparedLraTheory`'s row construction and so are always
declared variables. True of a row's pristine, freshly-built state, but
not preserved by Simplex pivoting, which can rewrite both rows in a pivot
pair in terms of each other -- so a former basic slack can end up as an
ordinary term in some other (now-basic) declared variable's row. Fixed by
checking `is_declared_integer` for every row term too, the same treatment
`x_b` already got.

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

## Patching

Stage 5 (2026-08-21), and the first stage here that tries to **find** an
integer model rather than refute a candidate. Bjørner & Nachmanson Sect. 4.1;
see [](papers/arithmetic-solving-in-z3.md) for the full Section 4 summary.

Re-reading that paper showed Primo's integer waterfall began at the paper's
*third* layer. Its first two — patching and the cube test — were both absent,
and both exist to find models. Primo's own corpus data says the same thing
independently: against cvc5 over all 13,306 files, Primo is **ahead by 147 on
unsat and behind by 661 on sat**. Every stage 1-4 layer is refutation
machinery, so the entire deficit sits where none of them can help.

Patching was chosen over the cube test because 249 of 300 sampled instances
contain equality atoms. An asserted equality fixes a slack, and tightening a
fixed slack in both directions makes the cube system contradictory, so the cube
test would be inconclusive on most rounds without also building z3's
difference/octagon twist. Patching is equality-insensitive: a fixed variable
simply has an empty freedom interval while others still move.

**Two shapes, and the first is one the paper does not have to describe.** The
paper assumes every non-basic sits at a bound and is therefore already
integral. Primo violates that twice over — `LraTableau` deliberately lets
non-basic variables sit strictly inside their bounds, and scale-canonicalization
stores literally fractional bounds on `Int` variables (`(<= (* 3 x) 5)` becomes
a bound of `5/3`). So a fractional non-basic is simply rounded in place, while a
fractional basic `xb` is fixed by moving a non-basic `y` from its row: with
coefficient `a`, moving `y` by `δ` moves `xb` by `a·δ`, so
`δ = (n − β(xb))/a`.

**Feasibility is structural, not re-checked.** `freedom_interval(y)` is exactly
the range over which `y` may move with *every* basic variable staying inside
its bounds. No move outside one is ever taken, so no `check()` call is needed.

**Patching decides nothing.** It only improves the assignment; every Sat/Unsat
conclusion still flows through GCD-consistency, cuts and branch-and-bound. When
it integralizes everything, `find_fractional_variable` finds nothing to branch
on and branch-and-bound returns `Sat` at zero nodes through unchanged code.
That keeps this stage's soundness surface far smaller than stages 2-4: the
failure mode is not a wrong answer but a wrong *model*, which is why
verification centres on model validation.

### Two things the obvious implementation gets wrong

Both were found by building the paper's own Example 3 (`y = x/2`, `z = x/3`,
`x∈[3,10]`, `y∈[−3,4]`, `z∈[−5,12]`) as a test rather than trusting the
derivation:

1. **Scoring a move by |δ| alone oscillates.** The nearest fix for `y` moves `x`
   to 4 and breaks `z`; the nearest fix for `z` then moves it straight back.
   Candidates are instead scored **globally**, by how many `Int` variables end
   up integral, and only strictly improving moves are taken. That is the same
   monotonicity argument `diversify_shared_model` already makes for itself, and
   it carries two more properties for free: termination (each move strictly
   decreases a count bounded by `|integer_vars|`) and the guarantee that the
   assignment never comes out worse than it went in — which matters because
   `set_nonbasic_value` is not trailed and there is no undo.
2. **Floor and ceiling alone are too narrow a candidate set.** Example 3's
   answer `x = 6` corresponds to `y = 3`, two integers above `y`'s current
   `3/2`, so a floor/ceil-only search never proposes it. A small bounded window
   (`kTargetWindow`) is searched instead, which is what lets one move satisfy
   several rows at once.

### The one `LraTableau` change

Stages 1-4 needed no tableau change at all. This stage adds exactly one thing:
`set_nonbasic_value`, a narrow public wrapper over the existing private
`update()` (which is the only correct way to move a non-basic value, since it
also repairs dependent basic assignments and refreshes violation status).

Its comment carries the crux: **it is not trailed outside
`PivotingRule::Soi`**, because `pop()` restores bounds only. A move made
through it cannot be undone by backtracking. `diversify_shared_model`
deliberately runs against a scratch copy of the assignment for exactly this
reason. Patching may mutate the live tableau only because
`solve_qf_lia_impl`'s `lia_tableau` is a per-round copy that is discarded and
rebuilt every round — the same argument `lia_gomory_cuts.cpp` already makes for
calling `add_row()` mid-search.

`delta_floor` also moved out of `lia_branch_and_bound.cpp` into
`lia_integer_values.hpp`, alongside a new `delta_ceil` and `is_integer_valued`,
so the two stages cannot drift apart on delta-awareness.

### Result

Per-family sample (94 files, 10s), same binary with `--lia-patch-limit 0` as
the control:

| | solved | sat | unsat | wrong | PAR2 |
|---|---|---|---|---|---|
| patching off | 54 | 26 | 28 | 0 | 8.72 |
| patching on | **58** | **30** | 28 | 0 | **7.84** |

**+4 gained, 0 lost**, every gain on a satisfiable instance and every one
decisive rather than boundary (10s timeout → 0.01-0.62s): two `prime-cone`
and two `CAV_2009_benchmarks/coef-size` instances. Zero answer disagreements
between the arms.

Models were validated on 142 corpus instances covering every file where
patching moved something: **66/66 sat models confirmed, zero wrong models**.

**That local check used z3 directly, not Dolmen, and the reason is worth
recording so it is not mistaken for a Dolmen limitation.** Dolmen is this
project's model checker and it does validate QF_LIA `Int` models — the
`39/45 model-error` episode recorded in
`scripts/slurm_qf_lia_model_fuzz_gate.sbatch` was Dolmen's *strict-mode
grammar* check rejecting FuzzSMT's `(* <let-bound-literal> <compound>)` terms,
and it was fixed there by passing `--dolmen-arg=--strict=false`, after which
all 45 passed through Dolmen directly. What actually happened here is narrower:
Dolmen is not installed in this development environment and `make
contrib-dolmen` fails locally, so `scripts/validate_model.py` fell through to
its z3 Real-only fallback and reported `validation-unavailable` for every
QF_LIA sat case. Passing `--dolmen-arg=--strict=false` would not have helped
without the binary.

The authoritative model check therefore remains
`scripts/slurm_qf_lia_model_fuzz_gate.sbatch` on the cluster, where Dolmen is
present and is listed in the job's own `required_tools`. The z3 check is a
local stand-in, not a replacement.

`CheckSatOptions::lia_patch_limit` (default 100) and `--lia-patch-limit`;
**0 disables patching**, which is the A/B control. `:lia.patch-sweeps`,
`:lia.patched-variables` and `:lia.patch-rounds-fully-integral` report what
happened.

## Conflict generalization

Stage 4 (2026-08-21). Stages 1-3 all decide *whether* a Boolean+LRA
assignment has an integer completion; this is about what gets said when the
answer is no.

Until stage 4, the answer was the coarse clause described under "Where
branching lives": the blocking literal of every arithmetic atom that fed
the tableau. That clause is sound but excludes essentially one Boolean
assignment, so the outer loop degenerated into model enumeration. Measured
by instrumenting a build over the per-family sample (3 files/family, 94
files, 10s): `calypto/problem-001542` emitted a 219-literal clause on each
of 6,528 rounds, and `convert/convert-jpg2gif-query-1141` emitted a
1,266-literal clause on each of 19,630+ rounds -- 28M literals of clause in
20 seconds, and it timed out.

**The certificate was already there.** `LraTableau` returns a Farkas-style
conflict from `assert_upper`/`assert_lower`/`check()`, and QF_LRA has
always used it directly as its lemma (`check_lra_theory_in_place` returns
`tableau.check()` as the clause). QF_LIA called the same operations and
threw the result away: `apply_gomory_cuts` collapsed it to `bool`,
`gcd_inconsistent_row` returned only a row id, `try_branch` only a flag.
Those conflicts were 5 literals on both instances above.

**Why it could not simply be forwarded.** Every one of those 5-literal
conflicts contained exactly one `kDerivedBoundReason` -- the synthetic
`sat::Lit{0}` a cut's threshold bound (and a branch's split bound) is
asserted with, because neither names a SAT atom. Forwarding it verbatim
would put literal `0` into a clause. Resolving it away is the entire fix.

**`LiaExplanation`** (`src/theory/arithmetic/lia_explanation.{hpp,cpp}`) is
one accumulator threaded through all three stages -- one per round, not one
per stage, because a cut derived by stage 3 stays in the tableau while
branch-and-bound runs, so a conflict branch-and-bound finds can rest on
stage 3's antecedents. It collects conflict literals and drops
`kDerivedBoundReason`, which is sound because each stage supplies what its
derived bound rests on:

- **Gomory cut**: the bound reasons of its row's terms. The cut is implied
  by exactly those bounds; the row equation it is read off is a tableau
  invariant, not an assumption, and contributes nothing. Recorded for every
  cut applied, including cuts that do not themselves conflict -- those stay
  in the tableau and can underpin a later conflict.
- **GCD-consistency**: both bound reasons of every *fixed* variable in the
  inconsistent row. It is the pair of equal bounds that pins the value the
  residual is built from; free variables contribute only coefficients.
- **Branch-and-bound**: nothing extra. `x <= n` or `x >= n+1` is a
  tautology over the integers, so once both sides are refuted the union of
  the two sides' explanations refutes the parent without mentioning the
  split.

**Soundness is checked, not argued.** `generalized_lia_lemma()` in
`check_sat.cpp` verifies that every literal of the derived explanation
already appears in the coarse clause, and falls back to the coarse clause
otherwise. That one containment test is what makes this incapable of
turning a correct answer into a wrong one, and it covers both failure
shapes at once: a literal that is not blocking for the current assignment
would fail to exclude it and spin the outer loop forever, and a leaked
synthetic literal is not in the coarse clause either. A superset of the
true explanation is always a valid (if weaker) lemma, which is why the
accumulation rules above are allowed to be conservative. An empty
explanation also falls back -- the empty clause claims the whole problem is
unsat, too strong a claim to reach by accident.

**Result** (60s, same instances):

| instance | coarse | generalized |
|---|---|---|
| `calypto/problem-001542` | unsat, 1.10s | unsat, 0.02s (6,528 rounds -> 2) |
| `calypto/problem-001553` | unsat, 0.86s | unsat, 0.02s |
| `convert/convert-jpg2gif-query-1141` | **unknown, timeout** | unsat, 0.04s (19,630+ -> 6) |

Zero fallbacks on any of them. `CheckSatOptions::lia_conflict_generalization`
(default on) and CLI `--no-lia-conflict-generalization` keep the coarse form
A/B-measurable; `:lia.generalized-lemmas`,
`:lia.generalized-lemma-literals`, `:lia.coarse-lemma-literals` and
`:lia.generalization-fallbacks` report what happened.

**Full-corpus effect, and where it costs.** All 13,306 QF_LIA files at
60s, running *both* arms of the same binary so the coarse arm is a control
(Slurm job 127949): 8,706 solved coarse -> 8,725 generalized, PAR2 43.12
-> 42.94, zero wrong and zero answer disagreements between the arms. The
coarse arm reproduces the pre-change baseline (8,704), which is what makes
the +19 attributable to this change rather than to drift.

The churn behind that +19 is asymmetric in the right direction -- 27 of
the 30 gains solve in under 5s (many at 0.02s), while 9 of the 11 losses
had already taken over 45s in the coarse arm and are timeout-boundary
noise. But one loss is real and worth understanding:
`rings_preprocessed/ring_2exp4_4vars_3ite_unsat.smt2` goes from 0.18s to
unsolved at 300s. Coarse runs 995 SAT solve calls in 0.18s (~5,500
rounds/s); generalized runs 814 in 300s (~2.7 rounds/s). The generalized
arm needs *fewer* rounds -- the lemma is doing its logical job -- but each
round costs about 2,000x more. Lemma size is 91 literals coarse vs 23
generalized, and that is the whole story: a 91-literal clause is nearly
inert and propagates almost nothing, while a 23-literal clause drives
heavy unit propagation and conflict analysis in CaDiCaL. **A stronger
lemma is not free**, and on a family whose rounds are already extremely
cheap the tradeoff inverts. Preferring the derived lemma only when it is
substantially smaller, or only when the coarse round rate is low, is the
obvious refinement and is not attempted here.

This does **not** move branching into CDCL -- branch literals are still
theory-internal, and the "Where branching lives" tradeoff stands. It only
stops discarding the explanation the tableau already computes. See
[](open-leads.md)'s lead 12d.

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
sibling of `scripts/slurm_qf_lra_model_fuzz_gate.sbatch`, with the same
two kinds of stage. FuzzSMT shards (default 8) generate fresh instances
per array task, each checked answer-differentially against z3 and, for
`sat` results, model-validated with Dolmen (`scripts/validate_model.py`).
An opfuzz task and two YinYang/typefuzz tasks (default: 1 and 2) mutate a
real QF_LIA seed corpus instead -- added 2026-08-20, closing the gap
[Real-world benchmark evaluation and the fuzzing gap](#real-world-benchmark-evaluation-and-the-fuzzing-gap)
below documents: FuzzSMT's own QF_LIA generator cannot produce the large,
non-unit scale-canonicalization factors that section's bug needed, so
only real benchmark formulas (mutated, to explore around them rather than
just replaying them unchanged) can exercise that shape at all.

**The QF_LIA seed corpus** (`benchmarks/seeds/qf_lia_division`, gitignored
like its QF_LRA counterpart, not committed): built with
`scripts/build_fuzz_seed_corpus.py --root sandbox/non-incremental/QF_LIA
--out benchmarks/seeds/qf_lia_division --per-family 2 --max-bytes 200000`
-- the smallest 1-2 files from every one of the division's 32 families
(61 seeds, 2.2MB total), including `convert` (the family that found the
scale-canonicalization bug; both chosen `convert` seeds independently
verified to still carry dozens of the large scale-canonicalizing
coefficients that matter, despite being under 10KB each). Unlike
`sandbox/`, which is 7.7GB and wholesale `.gitignore`'d with no staged
fallback root on a fresh SLURM workspace, this corpus is small enough to
stage directly (e.g. `scp`/`rsync` alongside a workspace sync) rather than
needing `slurm_qf_lra_model_fuzz_gate.sbatch`'s own
build-it-from-a-staged-root fallback -- `slurm_qf_lia_model_fuzz_gate.sbatch`
fails loudly instead if the corpus is missing, rather than assuming a
`tmp/QF_LIA` root that (unlike QF_LRA's) has no established staging
convention yet.

**Mutation configs**: `benchmarks/fuzz-configs/qf_lia_operator_mutations.txt`
(opfuzz) and `benchmarks/fuzz-configs/qf_lia_typefuzz.txt` (typefuzz)
restrict mutations to stay inside Primo's QF_LIA -- linear arithmetic,
Boolean structure, comparisons -- dropping nonlinear `*`, `div`/`mod`
(not recognized by Primo's grammar at all, unlike `/`, which parses and
is then explicitly rejected) and `abs` (also not a recognized symbol).
Structurally identical to the QF_LRA configs' own restrictions, since
Primo's QF_LIA is QF_LRA's arithmetic core plus an `Int` sort rather than
a separately-restricted grammar.

**A real quirk, not a bug, found while smoke-testing this stage locally**:
typefuzz can take tens of seconds per seed on formulas with deeply nested
`let` bindings (e.g. the `Averest` family's hardware-style encodings --
confirmed CPU-bound, not hung, by timing a single 54-line seed directly:
33.5s user time out of a 40s wall-clock budget). opfuzz does not show this
slowdown on the same corpus (190 solver calls in 60s locally). This is the
same class of concern `build_fuzz_seed_corpus.py`'s own docstring already
flags for QF_LRA's `latendresse`/`LassoRanker` families -- some seed
shapes are inherently expensive for a mutation tool to process -- and the
gate's `--seconds 1500` wall-clock per mutation task is designed to absorb
it: partial seed coverage within one run is expected and acceptable, not
a failure (`yinyang_fuzz.sh` itself treats hitting the wall as time-boxed
completion, not an error).

**The corpus needs re-staging after every workspace sync, not just the
first time.** `scripts/slurm_prepare_benchmark_workspace.py` syncs only
tracked `HEAD` files; `benchmarks/seeds/qf_lia_division` is gitignored
(same as its QF_LRA counterpart -- see `.gitignore`'s own comment on
that directory), so a sync run *after* the corpus was already staged
wipes it again. Hit directly in this session: the corpus was scp'd onto
the SLURM workspace, a code fix landed, `slurm_prepare_benchmark_workspace.py`
ran again to sync that fix, and the very next gate submission failed
immediately with "seed corpus is empty or missing" -- the sync had
deleted it. Re-`scp` the corpus after every sync that follows staging it,
not just once per workspace.

**Bug 4 (this section's own numbering; [GCD-consistency](#gcd-consistency)'s
"Bug 5" for the same fix), found by the mutation stage's first real run
(2026-08-20, job 127488, opfuzz): the GCD-consistency fix above did not
fully close its own bug.** Two `calypto`-family mutants disagreed with z3 (primo `unsat`,
z3 `sat`), minimized to `(assert (distinct (* 2 dz) (ite true 1 1)))`.
The GCD-consistency fix earlier in this same investigation (see
[GCD-consistency](#gcd-consistency)'s bug 4) only checked whether a row's
*basic* variable was a declared Int problem variable before assuming it
ranges over Z, reasoning that a row's *other* terms are always declared
variables straight from `PreparedLraTheory`'s row construction. True of a
row's pristine, freshly-built state, but not preserved by Simplex
pivoting, which can swap which variable is basic in a given linear
relationship and rewrite both rows in terms of each other -- so a former
basic slack can end up as an ordinary *term* in some other (now-basic)
declared variable's row. That is exactly what the minimized repro does:
`t` (the `ite`'s auxiliary variable) gets forced to `1`, `dz` becomes
basic in a row whose *term* is the scale-canonicalized slack for `2*dz -
t`, and the term-processing loop (unlike the already-fixed basic-variable
case) still folded that slack's coefficient into `free_coefficients`
unconditionally. **Fix**: `is_declared_integer` is now checked for every
row term too, not just `basic_var` -- see
`src/theory/arithmetic/lia_gcd_consistency.cpp`'s updated comment and
`tests/unit/lia_gcd_consistency_tests.cpp`'s
`does_not_flag_a_non_integer_slack_as_an_ordinary_row_term`, which builds
the post-pivot row shape directly. Reverified after the fix: both
minimized reproducers, the two original `convert` repro files, the
delta-infinitesimal repro, 500 further local FuzzSMT cases, ~2000 further
local opfuzz solver calls, and a full Slurm re-run all clean (job 127511
below).

**Status (mutation-fuzzing gate)**: first real run, 2026-08-20, job
127488 (11 tasks: 8 FuzzSMT shards + 1 opfuzz + 2 typefuzz) -- FuzzSMT
shards clean (3916 PASS, zero wrong answers, zero model-error), but the
opfuzz task found the two `calypto` disagreements above (bug 4), the
concrete payoff of adding this stage in the first place: neither of the
prior fuzzing approaches (FuzzSMT campaigns, local smoke fuzzing) had
ever found it. After the fix (commit with bug 4's writeup above), job
127511 reran the identical gate clean end to end: FuzzSMT shards 3907
PASS, 190 benign timeouts, 1 benign `UNKNOWN_INVOLVED`, **zero wrong
answers**; model validation 3482 pass, 454 correctly `not-sat`, 61 benign
timeouts, 3 benign `solver-unknown`, **zero `model-error`**; opfuzz
16,662 solver calls, **zero bugs**; both typefuzz tasks, **zero bugs**.
See `docs/run-log.json`'s `2026-08-20-qf-lia-mutation-fuzz-gate-slurm-verification`
entry for the complete record of both jobs.

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

## Real-world benchmark evaluation and the fuzzing gap

Stages 1-3 were each verified via thousands of FuzzSMT-generated cases
(the Fuzzing section above) before being called done, with zero wrong
answers reported every time. A subsequent evaluation against a
size-stratified representative sample of `sandbox/non-incremental/QF_LIA`
(SMT-LIB's real-world QF_LIA benchmarks, 13,306 files across 32 families,
428 files sampled) immediately found 11 wrong `unsat` answers -- all in
the `convert` family (Alberto Griggio's JPEG/GIF/TIFF converter
symbolic-execution benchmarks, industrial category), all `:status sat`,
all fast (not timeouts). Root-causing this (see bug 3 under
[Where integer strictness actually gets handled](#where-integer-strictness-actually-gets-handled-and-three-bugs-found-here)
and bug 4 under [GCD-consistency](#gcd-consistency)) found two soundness
bugs sharing one root cause, and fixing the first of those two exposed a
third, latent completeness/crash bug (see the third fix's own commit,
"Fix a latent delta-infinitesimal bug in branch-and-bound and Gomory
cuts") that fuzzing had never triggered either.

**Verification after all three fixes.** The full `convert` family (303
files with a declared `:status`, not just the 428-file sample) re-checked
locally: **zero wrong answers**. Local FuzzSMT smoke fuzzing (500 default-
parameter cases) and the full Slurm gate (`scripts/slurm_qf_lia_model_fuzz_gate.sbatch`,
job 127473, 2026-08-20): **3898 PASS, 207 benign timeouts, 3 benign
`UNKNOWN_INVOLVED`, zero wrong answers, zero crashes, zero model errors** --
see `docs/run-log.json`'s `2026-08-20-qf-lia-real-world-benchmark-evaluation`
and `2026-08-20-qf-lia-scale-canonicalization-fix-slurm-verification`
entries for the complete record, including job 127465's failure (23
`solver-error` crashes from the delta-infinitesimal bug above) on the
first fix alone, before the second fix landed.

**Why FuzzSMT's existing gates never found this.** `contrib/src/fuzzsmt`'s
own QF_LIA defaults (`FuzzSMT.java`, the `QF_LIA` case in its
per-logic parameter table) are `maxNumVars = 3`, `maxNumConsts = 3`,
`maxBW = 4` -- every generated integer constant is a random `BigInteger`
with 1 to 4 *bits*, i.e. magnitude at most 15. None of the fuzz gates run
so far (stages 1-3's own verification, all using these defaults) ever
override `-Mbw`/`-Mv`/`-Mc`. The `convert` family's actual trigger --
scale-canonicalization (item H1b) dividing by a large leading-variable
coefficient (`2^32` for these bit-position-weighted terms, from
`(* sigma_N (- 4294967296))`-shaped polynomials) -- is essentially
unreachable at `maxBW = 4`: the largest possible scale is 15, giving a
denominator far too coarse to reproduce the same failure mode. Confirmed
empirically, not just argued: 500 cases at the *default* parameters
against the pre-fix (buggy) binary found **zero** disagreements (matching
every prior stage's own "zero wrong" fuzz results, now understood to be a
structural blind spot rather than actual absence of the bug) --
reproduced directly for this section by building the parent commit
(`0c8314d`, pre-fix) in a separate git worktree and re-running the exact
same FuzzSMT-seeded batch against it.

**Widening the bitwidth alone is not sufficient either -- an honest
negative result.** A second local batch, same seed, with
`-mbw 1 -Mbw 32 -mv 2 -Mv 8 -mc 2 -Mc 8` (up to 32-bit coefficients, 2-8
variables and constants, closer to the `convert` family's own shape),
run against the *same pre-fix binary*: still **zero** disagreements across
500 cases. The bug needs more than a large coefficient in isolation -- the
`convert` family's actual trigger is *many* atoms sharing one
scale-canonicalized slack (`PreparedLraTheory`'s `slack_by_polynomial`
dedup) via proportional polynomials with a range of small, close-together
thresholds that collapse together after the large-scale division (see
bug 3's writeup). That combination -- proportional compound polynomials
recurring across many atoms in one instance -- is a natural consequence of
a real symbolic-execution encoding (many byte/bit-position comparisons
against the same underlying value) but is comparatively rare for FuzzSMT's
independent, layer-by-layer random atom generation to produce even with a
wider coefficient range, so it did surface as a crash on the *new* binary
during this same wide-bitwidth batch (see the third bug/fix above) but
not as the original wrong-answer bug on the old one, in 500 cases either
way.

**The actionable gap -- closed 2026-08-20.** `scripts/slurm_qf_lia_model_fuzz_gate.sbatch`'s
own comment had already flagged this direction as "natural future work
once real QF_LIA benchmarks are collected" back when stage 1 shipped; this
evaluation made it demonstrably load-bearing rather than hypothetical.
Real QF_LIA benchmarks are collected now (`sandbox/non-incremental/QF_LIA`),
and the same pattern `scripts/build_fuzz_seed_corpus.py` already
establishes for QF_LRA now has a QF_LIA counterpart: a generated (not
committed, same as QF_LRA's) seed corpus at
`benchmarks/seeds/qf_lia_division` covering all 32 families including
`convert`, an opfuzz task and two YinYang/typefuzz tasks added to
`scripts/slurm_qf_lia_model_fuzz_gate.sbatch` alongside its existing
FuzzSMT-only stage, and restricted mutation configs
(`benchmarks/fuzz-configs/qf_lia_{operator_mutations,typefuzz}.txt`) --
see [Fuzzing](#fuzzing) above for the full setup and a first Slurm-scale
run's results.

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
