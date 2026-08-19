# QF_LIA, Stage 1: Branch-and-Bound on the Existing Simplex

Implemented 2026-08-19. QF_LIA is now accepted (`(set-logic QF_LIA)`) via a
plain branch-and-bound decision procedure layered entirely on top of the
existing `LraTableau` -- no change to `LraTableau` or `OnlineLraPropagator`
themselves. This is deliberately stage 1 of two: cutting planes (Gomory
cuts and friends) are a separate, later plan, not implemented here.

Two papers grounded the design:

- Dutertre & de Moura, *Integrating Simplex with DPLL(T)* (SRI-CSL-06-01) --
  already the basis of `LraTableau`; see `docs/papers/fast-linear-arithmetic-dpllt.md`.
  No integer content; cited here only because every QF_LIA bound assertion
  still goes through the exact same Assert-Bound/Check machinery it
  describes.
- Bjørner & Nachmanson, *Arithmetic Solving in Z3* (CAV 2024), Sect. 4.4:
  on a fractional integer basic variable with value β(x), split on
  `x <= floor(β(x))` vs `x >= floor(β(x))+1`. The paper is explicit that
  branch-and-bound alone is not a complete decision procedure -- it is one
  layer of a waterfall (patch → cube → GCD → branch-and-bound/Gomory) --
  which is exactly why this stage accepts and reports its own
  incompleteness (see [Known incompleteness](#known-incompleteness) below)
  rather than hiding it.

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

## Node limit

`CheckSatOptions::lia_branch_limit` (default 100000), CLI
`--lia-branch-limit` (`kGroupLia`, "QF_LIA theory options" in `--help`).
Not yet empirically tuned against real QF_LIA benchmarks -- picked
generously enough to clear small/medium instances while still bailing out
in reasonable time. A genuinely unbounded integer-infeasible system (e.g.
`2x - 2y = 1` with `x, y` unbounded) hits this deterministically regardless
of the limit's value, since no cut/GCD test exists yet to detect that
infeasibility directly; see the next section.

## Known incompleteness

Plain branch-and-bound is not a complete decision procedure for QF_LIA on
its own: an integer-infeasible system whose LRA relaxation is unbounded in
the relevant direction can branch forever without ever exhausting the
search (Bjørner & Nachmanson's waterfall exists precisely because plain
branch-and-bound needs GCD tests, cube tests, or cutting planes alongside
it for completeness). `tests/unit/lia_branch_and_bound_tests.cpp`'s
`unbounded_polytope_hits_the_node_limit` test pins this down directly:
`2x - 2y = 1` with both variables unbounded returns `LimitReached` rather
than looping, by construction (every node's LRA relaxation stays feasible,
so the search never runs out of new fractional variables to split on).

This is the reason stage 2 (Gomory cuts, GCD consistency, cube tests --
Bjørner & Nachmanson Sect. 4.2-4.3, 4.5) is a separate, planned follow-up
rather than a "someday" note.

## Explicitly out of scope for stage 1

- Gomory cuts / GCD test / cube test -- stage 2, a separate plan, after
  stage 1 is fuzzed at Slurm scale.
- CDCL-integrated branch literals (Z3-style, injecting a fresh SAT literal
  from a callback) -- a possible future performance phase, not needed for
  correctness.
- `div`/`mod`, mixed QF_LIRA (`Int` + `Real` together).
- Any change to `LraTableau` or `OnlineLraPropagator` itself.

(Fuzzing itself is *not* out of scope -- see the next section. What is
still missing is a Slurm-scale campaign; the smoke-level local fuzzing
that already ran is what found and confirmed the fix for the two bugs
above.)

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

**Status**: smoke-verified twice on the actual Slurm cluster (not just
locally) -- job 127439 (50 cases, found bug 3) and job 127440 (50 cases,
post-fix: 48/50 PASS on the answer-diff, 2 benign solver timeouts, 0 wrong
answers; 40/40 `sat` cases pass Dolmen model validation, 0 `model-error`).
The full default-sized run (8 shards x 500 cases) is the natural next
step, not yet done as of this writing -- see `docs/run-log.json` for
whichever of these two states is current by the time you're reading this.

## Tests

- `tests/unit/rational_tests.cpp`: `floor()`/`ceil()`/`is_integer()`,
  including the big (`mpq_class`) representation path.
- `tests/unit/lra_atom_tests.cpp`: confirms `normalize_atom` stays
  sort-agnostic (an `Int` `Lt` atom comes out exactly like a `Real` one) --
  see the bug writeups above for where the actual integer-strictness
  handling lives instead.
- `tests/unit/lia_branch_and_bound_tests.cpp`: white-box tests of
  `branch_and_bound` directly against `LraTableau` -- no branching needed,
  one branch to a solution, every branch infeasible (`Unsat`), and the
  node-limit/incompleteness case above.
- `tests/unit/qf_lia_solver_tests.cpp`: black-box, end-to-end via
  `check_sat_smtlib`, covering the same shapes through the full frontend
  plus `distinct`, nested/nontrivial branching, the strict-tightening bug
  above exercised end to end, the frontend rejections
  (UF/`Real`-under-QF_LIA/division/`Int`-under-other-logics), and the node
  limit.
