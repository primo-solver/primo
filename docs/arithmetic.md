# Arithmetic

QF_LRA (quantifier-free linear real arithmetic) is implemented through
Milestone 4 of the plan described below: a full SMT-LIB QF_LRA frontend
plus a bounded-variable Simplex theory solver integrated into DPLL(T) via a
trail-synced CaDiCaL external propagator, with both conflict detection and
same-slack bound propagation, following Dutertre & de Moura's "A Fast
Linear-Arithmetic Solver for DPLL(T)". **Supported**, not just
experimental, as of the 2026-07-24 campaign-tier fuzz gate (3000 fuzzsmt
cases plus opfuzz/yinyang mutation seeded from real benchmarks, all clean
-- see `docs/run-log.json`'s Milestone 4b entries and Milestone 4b below).
Difference logic (QF_IDL/QF_RDL) remains unimplemented; `difference_logic.hpp`
is an empty placeholder reserved for that, separate future work.

See [](papers/fast-linear-arithmetic-dpllt.md) for the base algorithm and
[](papers/lra-related-work.md) for a literature survey of extensions and
alternatives considered before/while implementing it.

## Implemented (Milestones 1-4, supported)

- **Grammar/frontend** (`src/smt/check_sat.cpp`): `(set-logic QF_LRA)`,
  `Real`-sorted declarations, `+ - * <= < >= > =` per the SMT-LIB QF_LRA
  grammar (verified against the spec, not assumed) -- n-ary `+`/chainable
  comparisons, arity-disambiguated `-`, `*`/`/` requiring a literal
  operand (with the literal recognized after `build_term`, so let-bound
  aliases and constant-folded sub-expressions work, not just raw numeral
  tokens). `Real`-sorted `=` is a QF_LRA atom, not an EUF one --
  `contains_equality`/`contains_arithmetic_atom` classify by operand sort.
- **Term representation** (`src/core/term.hpp`, `term_manager.{hpp,cpp}`):
  `SortKind::Real`; `TermKind::{Rational, Plus, Times, Le, Lt}`. `Times` is
  a *unary* scale node (coefficient * one child), not general
  multiplication -- see the TermKind comment for why (a coefficient has to
  attach to some node; repeated addition can't represent non-integer
  coefficients).
- **Exact rationals** (`src/util/rational.{hpp,cpp}`): GMP-backed
  (vendored via `contrib/install_gmp.py`, `make contrib-gmp`).
- **Normalization** (`src/theory/arithmetic/linear_polynomial.{hpp,cpp}`,
  `lra_atom.{hpp,cpp}`): `LinearPolynomial` (sum of coeff*var + constant,
  Section 2's normal form), `LraAtom` (canonical "t ~ c" form, Section 2).
- **Decision procedure** (`src/theory/arithmetic/lra_tableau.{hpp,cpp}`):
  `LraTableau` implements Assert-Bound, Check/Pivot (Bland's rule, for
  termination), and Farkas-certificate conflict explanation (Section 3,
  3.2). Strict inequalities via `DeltaRational` (q + k*delta, exact, no
  floating point). Incremental: `push()`/`pop(checkpoint)` are genuinely
  trail-based (Section 4), mirroring `FlatCongruenceClosure`'s discipline
  -- every bound change, assignment change, and pivot's row rewrite pushes
  a `TrailEntry`, and `pop` replays them backwards. Pivots ARE undone on
  pop (a row's equation and the assignment it was computed under are
  coupled; see the class comment for why the paper's "pivots don't track
  search depth" framing doesn't mean "don't undo them").
- **DPLL(T) wiring** (`check_sat.cpp`): `PreparedLraTheory` precomputes the
  tableau's structural part exactly once, mirroring `PreparedFlatTheory` --
  one row per *distinct polynomial*, not per atom: atoms that normalize to
  the exact same `LinearPolynomial` share a slack variable (`PolynomialKey`/
  `PolynomialKeyHash`, exact-content matching only, not scale-invariant),
  with an `atoms_by_slack` index recording which atoms share which slack.
  `OnlineLraPropagator` (Milestone 3a) is a trail-synced
  `CaDiCaL::ExternalPropagator`, owning one `LraTableau` (copied once from
  the prepared base) kept in sync via `notify_new_decision_level`/
  `notify_backtrack` calling `LraTableau::push()`/`pop()` -- this is what
  actually exercises push/pop for cross-round reuse. `solve_qf_lra` always
  prefers this when CaDiCaL's external-propagator API is available
  (`#if PRIMO_HAVE_IPASIR`); `find_incremental_lra_theory_conflict` (copy
  the base tableau and reassert every round, no push/pop reuse) survives
  only as the fallback when it isn't.
- **Same-slack bound propagation** (Milestone 3b, `check_sat.cpp`): after
  each successful bound assertion, `OnlineLraPropagator` scans the other
  atoms sharing that slack (via `atoms_by_slack`) and eagerly propagates
  any now entailed true/false, using `LraTableau::upper_bound()`/
  `lower_bound()` (new read accessors) compared against `true_threshold`/
  `false_threshold` (the same `DeltaRational` values `assert_lra_atom_bound`
  itself would assert) -- Dutertre & de Moura Section 4.2's "deriving
  satisfied atoms". Each propagation's reason is exactly the one bound's
  own asserting literal (a 2-literal Horn clause), computed eagerly at scan
  time rather than lazily re-derived, since the check is exact and cheap
  (no candidate/budget/backoff machinery like EUF's propagation needs).
  Gated behind `CheckSatOptions::lra_propagation` (default on),
  `--lra-propagation`/`--no-lra-propagation` CLI flags.
- **Row-based/interval bound propagation** (competitiveness-roadmap item 2,
  2026-07-25, `check_sat.cpp`): generalizes same-slack propagation to a
  *derived* rather than only directly-asserted bound. For a row `basic_var
  = sum(coeff_j * var_j)`, `compute_implied_bound` sums each term's own
  maximum/minimum contribution (`var_j`'s upper bound if `coeff_j` is
  positive, lower if negative, for the implied-upper case; symmetric for
  implied-lower) into an implied interval for `basic_var`, then checks it
  against `atoms_by_slack[basic_var]` exactly like same-slack propagation
  does. Triggered from the same `notify_assignment_lit` hook, right after
  the same-slack scan, on whatever slack was just tightened.
  **Bounds in this tableau design only ever live on slacks** (every atom,
  even a bare single-variable one like `(<= x 2)`, gets its own dedicated
  slack/row -- structural variables like `x` never receive a bound
  directly), so a row's *term* variables need an extra indirection to find
  their current bound at all: `PreparedLraTheory::single_variable_slack`
  (built once at construction, alongside the existing `atoms_by_slack`)
  maps a raw variable to the slack that wraps it alone (coefficient
  exactly 1 -- a deliberate first-cut scope limit, not a fundamental one),
  when such an atom exists in the formula; `variable_for_single_slack` is
  its reverse, used on the trigger side to decide whether a just-changed
  slack is even relevant to rescan. Reasons are genuinely multi-literal
  (one antecedent per row term used, unlike same-slack propagation's
  single-literal reason) -- `OnlineLraPropagator`'s `pending_`/`reasons_`/
  `cb_add_reason_clause_lit` were widened from a single `sat::Lit` to
  `vector<sat::Lit>` to carry this (same-slack entries become singleton
  vectors, unchanged behavior). Two defensive, non-adaptive caps
  (`kRowBasedPropagationMaxRowSize`, `kRowBasedPropagationMaxFanout`,
  both 64) bound the O(row size) per-affected-row cost this technique has
  that same-slack propagation's O(atoms sharing a slack) doesn't. Reported
  by `:lra.row-propagations`, alongside the existing aggregate
  `:lra.theory-propagations`. **Opt-in**
  (`CheckSatOptions::lra_row_propagation = false`,
  `--lra-row-propagation`), only takes effect when `lra_propagation` is
  also enabled -- materially new algorithmic machinery, not yet
  campaign-fuzzed. Only derives a row's *own* implied bound and checks its
  *own* slack's atoms -- does not (yet) rearrange a row to tighten one of
  its *children's* bounds from the basic variable's own bound (Section 4's
  fuller bidirectional treatment), a natural, bigger follow-up.
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
  (`--lra-pivoting-rule {bland,dantzig,soi}`) -- **default stays Bland**, the
  already campaign-fuzzed behavior; `kStatLraBlandFallbacks` reports how
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
    standard QuickXplain (Junker 2001) minimization via
    `soi_subset_still_conflicts()`'s restricted-objective oracle.
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
  - Reported by `:lra.soi-multi-row-conflicts`/`:lra.soi-quickxplain-calls`,
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
- **Disequality**: `Real`-sorted `=` is lowered to `(t1 <= t2) AND (t2 <= t1)`
  (since Real is a total order), not handled as a special relation in the
  tableau at all. `NOT(t1=t2)` is then just `NOT(AND(Le,Le))`, already
  correctly handled by `BooleanClausifier`'s existing Tseitin/CNF machinery
  for arbitrary Boolean structure -- ordinary CDCL branching over the two
  underlying `Le` atoms explores `t1<t2` vs `t1>t2` for free. No
  case-splitting or lemma-learning code exists anywhere in
  `LraTableau`/`OnlineLraPropagator`; `LraAtom`'s `Eq` relation is
  unreachable through the frontend (kept as defensive dead code, not
  removed, in case a future code path builds a `Real` equality term some
  other way). The lowering itself (`lower_real_equalities` in
  `preprocessing.cpp`) runs once, as the last step of the preprocessing
  pipeline, immediately before the `has_equality`/`has_arithmetic` theory
  dispatch -- **not** at parse time as originally implemented (changed
  2026-07-25) -- so solve-eqs and the other Equality-aware preprocessing
  passes see genuine Real `Equality` nodes first. See
  `docs/preprocessing.md`'s "Real-Equality Lowering" section.

### Implemented subset vs. deviations vs. limitations

- **Real-only.** No `Int` sort, no QF_LIA/QF_UFLRA. Mixing `Real` equality
  with uninterpreted-sort equality is explicitly rejected (`Unsupported`),
  not silently mishandled -- would need real Nelson-Oppen combination.
- ~~Disequality (negated `=`) is rejected~~ -- **fixed 2026-07-24**, see
  above. Was the dominant cause of `UnsupportedInput` in every fuzz
  campaign (394-418/500 cases); after the fix, zero `UnsupportedInput` in
  the same 500-case campaign.
- **`*` is more permissive than strict QF_LRA syntax**: the non-literal
  operand may be a compound linear term (`(* 2 (+ x y))`), not only a bare
  variable -- harmless (still linear) and simpler to implement than
  enforcing the stricter grammar.
- ~~No pivoting-rule choice yet~~ -- **added 2026-07-24** (Milestone 4a):
  `LraTableau::PivotingRule::Dantzig`, opt-in, default stays Bland. Devex
  and Steepest-Edge (`docs/papers/lra-related-work.md`'s survey) remain
  unimplemented -- Dantzig alone already closes most of the practical gap
  per the cited comparative benchmark (fastest per-instance wall time),
  and adding either of the other two is a bigger, separate undertaking.
- **Polynomial dedup is exact-match only, not scale-invariant**: `x<=5` and
  `2x<=10` still get separate slacks (and so can't propagate against each
  other), even though they constrain the same line. A documented,
  deliberate scope cut for Milestone 3b, not an oversight -- see the
  `PolynomialKey` comment in `check_sat.cpp`.
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

**Not yet scheduled** (natural next candidates, not designed in detail):
scale-invariant polynomial matching (item 3, would extend both same-slack
and row-based propagation's reach by letting proportional polynomials like
`x<=5`/`2x<=10` share a slack); the row-based propagation follow-up noted
above (tightening a row's *children* from its own bound, not just deriving
the row's own implied bound); Devex or Steepest-Edge pivoting (Milestone
4a covered only Dantzig, and SOI-Simplex, item 4, is now done without
closing the quick-eval sample's gap -- neither pricing rule choice nor a
global objective alone seems to be the bottleneck for the "hard for
everyone" families in that narrow sample); a campaign-tier confirmation
specifically for `--lra-pivoting-rule dantzig`/`soi` if either is ever
considered for becoming the default; and, now the concrete priority per
item 0's broader empirical baseline (done, see above): profiling what's
actually slow on a representative TM/sc/uart/2019-ezsmt/tta_startup/
miplib timeout case (these solve fine for z3/cvc5 within the same
budget, unlike the narrow sample's "hard for everyone" families) --
likely the most informative next step before committing further
engineering effort to items 1/2/4's own directions on faith alone.
