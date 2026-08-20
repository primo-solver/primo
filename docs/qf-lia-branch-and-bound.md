# QF_LIA: Branch-and-Bound Plus GCD-Consistency on the Existing Simplex

Stage 1 implemented 2026-08-19, stage 2 implemented 2026-08-20. QF_LIA is
accepted (`(set-logic QF_LIA)`) via a plain branch-and-bound decision
procedure layered entirely on top of the existing `LraTableau`, plus a
read-only GCD-consistency pre-check that closes one specific, documented
gap in branch-and-bound's completeness. Neither stage changes `LraTableau`
or `OnlineLraPropagator` themselves. Cutting planes (Gomory cuts) remain a
separate, later stage 3, not implemented here -- see
[Stage 3 and beyond](#stage-3-and-beyond).

Two papers grounded stage 1's design, joined by a third source for stage
2:

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
  as-printed). The paper is explicit that branch-and-bound and GCD alone
  are still not jointly complete -- both are single layers of a larger
  waterfall (patch → cube → GCD → branch-and-bound/Gomory) -- which is
  exactly why this doc accepts and reports the remaining incompleteness
  (see [Known incompleteness](#known-incompleteness) below) rather than
  hiding it.

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

## Node limit

`CheckSatOptions::lia_branch_limit` (default 100000), CLI
`--lia-branch-limit` (`kGroupLia`, "QF_LIA theory options" in `--help`).
Not yet empirically tuned against real QF_LIA benchmarks -- picked
generously enough to clear small/medium instances while still bailing out
in reasonable time. Stage 2's GCD-consistency check now catches the
`2x - 2y = 1`-shaped case (a row whose value is *fixed* to something its
coefficients' gcd cannot reach) before branch-and-bound ever starts, so it
no longer needs the node limit at all. A structurally different
divergent case -- `4x + 4y` bounded to a *range* that contains no multiple
of 4 (e.g. `[2, 3]`, with `x, y` unbounded) -- is not GCD-detectable (the
row is never fixed to one value) and still hits the limit deterministically;
see the next section.

## Known incompleteness

Branch-and-bound plus GCD-consistency together are still not a complete
decision procedure for QF_LIA: an integer-infeasible system whose LRA
relaxation stays feasible at every node, and whose infeasibility isn't
witnessed by any single fixed row, can still branch forever
(Bjørner & Nachmanson's waterfall exists precisely because these two
layers need cube tests and cutting planes alongside them for
completeness).
`tests/unit/lia_branch_and_bound_tests.cpp`'s
`unbounded_polytope_hits_the_node_limit` test (unchanged by stage 2 --
it exercises `branch_and_bound` directly, bypassing the new pre-check, to
keep pinning down branch-and-bound's own behavior in isolation) and
`tests/unit/qf_lia_solver_tests.cpp`'s
`gcd_uncatchable_divergent_system_hits_the_node_limit` (the new
`4x+4y in [2,3]` case, end to end) both confirm this directly: neither
converges, by construction, since every node's LRA relaxation stays
feasible and GCD-consistency has nothing fixed to check.

This is the reason stage 3 (Gomory cuts, the extended GCD test, cube
tests) remains a separate, planned follow-up rather than a "someday"
note -- see [Stage 3 and beyond](#stage-3-and-beyond).

## Explicitly out of scope (stages 1 and 2)

- Gomory cuts / the extended GCD test / cube test -- stage 3, a separate
  plan; see [Stage 3 and beyond](#stage-3-and-beyond) for what was already
  investigated toward it.
- CDCL-integrated branch literals (Z3-style, injecting a fresh SAT literal
  from a callback) -- a possible future performance phase, not needed for
  correctness.
- `div`/`mod`, mixed QF_LIRA (`Int` + `Real` together).
- Any change to `LraTableau` or `OnlineLraPropagator` itself (both stages
  stayed entirely read-only or bound-assertion-only against the existing
  API).

(Fuzzing itself is *not* out of scope -- see the next section. Stage 2's
Slurm-scale re-verification is still outstanding as of this writing; see
`docs/run-log.json` for whichever state is current by the time you're
reading this.)

## Stage 3 and beyond

Investigated during stage 2 planning, not implemented:

- **Gomory cuts need a new tableau row**, added mid-search -- a Gomory cut
  is, in general, a genuinely new linear combination of a fractional row's
  non-basic variables, not reducible to a bound on one existing variable
  (confirmed by inspecting `LraTableau::Row`/`RowEntry`'s representation:
  strictly `basic_var = Σ coefficient·var`, with `add_row()` the only API
  that introduces one). Two findings worth recording for whoever picks
  this up:
  - `LraTableau::add_row()` is **not actually enforced** to run only
    before the first `push()` -- the class comment says so, but nothing
    asserts it, and there is already a working precedent for calling it
    mid-search on a live, already-pushed tableau:
    `prepare_lra_equality_probe_context` in `check_sat.cpp` (~line 6822)
    does exactly this on a *copy* of the live search tableau. The one real
    hazard, visible in that call site's own comment, is that
    `add_row()`-added state isn't trailed, so a `pop()` whose checkpoint
    predates the `add_row()` call silently leaves the row (and its
    variable) alive rather than undoing it.
  - Making a cut properly backtrackable (rather than permanent for the
    rest of the process) needs a small, mechanical `LraTableau` extension:
    a new `TrailEntry` variant (`RowTrailEntry{LraVarId basic_var}`),
    pushed by `add_row()`, undone by clearing `rows_[basic_var]` and
    `is_basic`. `TrailEntry`'s existing variant/`undo()`/`pop()` shape
    makes this look like a genuinely small, self-contained change (rough
    estimate: ~15-30 lines) *if* full backtrackability turns out to be
    wanted -- but deciding whether cuts should be node-scoped or
    permanent, and how they'd interact with `OnlineLraPropagator`'s own
    independently-copied live tableau, are real design questions of their
    own, not resolved here.
  - An alternative that needs none of the above: derive a cut from a
    row's current (read-only) state the same way this project already
    handles branch-and-bound's own `Unsat` case -- as a coarse SAT-level
    lemma (`backend.add_clause`) blocking the current assignment, rather
    than a tableau row Simplex can reuse across nodes. Cheaper to build,
    but the cut only blocks the exact assignment it came from instead of
    becoming an ordinary, reusable constraint at that search node and its
    descendants.
- The paper gives no cut-coefficient formula of its own for Gomory cuts
  (Sect. 4.5); it points to Dutertre & de Moura (CAV 2006, already
  `LraTableau`'s own basis) as the starting point, plus two more papers
  for a Hermite-normal-form-based alternative. Whoever implements stage 3
  needs to work the derivation from one of those sources directly.
- The extended GCD test and the cube test (see
  [GCD-consistency](#gcd-consistency) above) are the two lower-effort
  stage-3 candidates that don't need any `LraTableau` change at all --
  worth doing before Gomory cuts if stage 3 is picked up, on cost/benefit
  grounds alone.

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

**Status**: full default-sized gate (8 shards x 500 = 4000 FuzzSMT-generated
QF_LIA cases) run on the actual Slurm cluster, 2026-08-19, job 127441 (see
`docs/run-log.json`'s `2026-08-19-qf-lia-fuzz-gate-slurm-verification`
entry for the complete record, including the two smaller jobs, 127439 and
127440, that found and confirmed the fix for bug 3 above). Aggregate
results: answer-differential 3926 PASS, 73 benign solver timeouts, 1
`UNKNOWN_INVOLVED`, **zero wrong answers**; Dolmen model validation 3528
pass, 423 correctly `not-sat`, 48 benign timeouts, 1 `solver-unknown`,
**zero invalid models**. The one non-`pass` case beyond timeouts
(`fuzzsmt_227445__file_247.smt2`) is primo honestly reporting `unknown`
after exhausting `--lia-branch-limit` on a z3-`sat` instance -- confirmed
via its `solver.stderr`, which reads exactly "QF_LIA branch-and-bound node
limit reached" -- i.e. the documented incompleteness above, not a bug.
That single case did make the gate's shard task exit nonzero the first
time (the benign-outcome whitelist didn't yet include `solver-unknown`);
fixed in the gate script afterward, with a comment on exactly what
assumption ties it to being genuinely benign and when to revisit it.

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
- `tests/unit/qf_lia_solver_tests.cpp`: black-box, end-to-end via
  `check_sat_smtlib`, covering the same shapes through the full frontend
  plus `distinct`, nested/nontrivial branching, the strict-tightening bug
  above exercised end to end, the frontend rejections
  (UF/`Real`-under-QF_LIA/division/`Int`-under-other-logics), and (stage 2)
  both `gcd_inconsistent_system_is_unsat_without_branching` (the motivating
  case, now `Unsat` immediately even under a node limit far too small for
  branch-and-bound alone to ever reach it) and
  `gcd_uncatchable_divergent_system_hits_the_node_limit` (the
  `4x+4y in [2,3]` case GCD-consistency deliberately does not catch,
  confirming the node limit is still reachable and the remaining
  incompleteness is real, not a leftover artifact of an unexercised code
  path).
