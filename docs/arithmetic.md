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
  (`--lra-pivoting-rule {bland,dantzig}`) -- **default stays Bland**, the
  already campaign-fuzzed behavior; `kStatLraBlandFallbacks` reports how
  often the fallback engages.
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
- **Competitiveness roadmap item 1 follow-up (2026-07-25): Gaussian
  elimination over asserted linear equality systems.** The plain
  recognizer above only catches `x = t` with a bare variable on one side;
  `--gaussian-elimination` (opt-in, default off) additionally row-reduces
  (Gauss-Jordan, exact rationals) the whole system of asserted Real
  equalities, catching single equations with a coefficient (`2x = 5`) and
  genuine multi-equation systems (`2x+3y=5` and `x-y=1` jointly solving
  for both `x` and `y`) that the plain recognizer can't. Merges into the
  same candidate list/accept-reject pipeline the plain recognizer uses --
  no new correctness logic there. Full suite (Release + ASAN/UBSan) and
  new unit tests pass. Opt-in specifically because it's materially new
  algorithmic machinery (not yet campaign-fuzzed the way the rest of
  solve-eqs has) -- see `docs/preprocessing.md`'s "Gaussian Elimination"
  section for the design and `docs/run-log.json` for verification status;
  promote to default-on once campaign-tier Slurm confirmation lands.

**Not yet scheduled** (natural next candidates, not designed in detail):
scale-invariant polynomial matching or row-based/interval propagation
(Milestone 3b's real-benchmark finding suggests either would move the
still-hard families further than same-slack propagation alone); Devex or
Steepest-Edge pivoting (Milestone 4a covered only Dantzig); a
campaign-tier confirmation specifically for `--lra-pivoting-rule dantzig`
if it's ever considered for becoming the default.
