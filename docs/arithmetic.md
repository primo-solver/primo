# Arithmetic

QF_LRA (quantifier-free linear real arithmetic) is implemented through
Milestone 3a of the plan described below: a full SMT-LIB QF_LRA frontend
plus a bounded-variable Simplex theory solver integrated into DPLL(T) via a
trail-synced CaDiCaL external propagator (conflict detection only, no
proactive bound propagation -- see Milestone 3a's note on why), following
Dutertre & de Moura's "A Fast Linear-Arithmetic Solver for DPLL(T)".
Difference logic (QF_IDL/QF_RDL) remains unimplemented; `difference_logic.hpp`
is an empty placeholder reserved for that, separate future work.

See [](papers/fast-linear-arithmetic-dpllt.md) for the base algorithm and
[](papers/lra-related-work.md) for a literature survey of extensions and
alternatives considered before/while implementing it.

## Implemented (Milestones 1-3a)

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
  tableau's structural part (one row per arithmetic atom) exactly once,
  mirroring `PreparedFlatTheory`. `OnlineLraPropagator` (Milestone 3a) is a
  trail-synced `CaDiCaL::ExternalPropagator`, owning one `LraTableau`
  (copied once from the prepared base) kept in sync via `notify_new_decision_level`/
  `notify_backtrack` calling `LraTableau::push()`/`pop()` -- this is what
  actually exercises push/pop for cross-round reuse. `solve_qf_lra` always
  prefers this when CaDiCaL's external-propagator API is available
  (`#if PRIMO_HAVE_IPASIR`); `find_incremental_lra_theory_conflict` (copy
  the base tableau and reassert every round, no push/pop reuse) survives
  only as the fallback when it isn't. `OnlineLraPropagator::cb_propagate()`
  always returns 0 -- deliberately conflict-detection only, see Milestone
  3a below for why.

### Implemented subset vs. deviations vs. limitations

- **Real-only.** No `Int` sort, no QF_LIA/QF_UFLRA. Mixing `Real` equality
  with uninterpreted-sort equality is explicitly rejected (`Unsupported`),
  not silently mishandled -- would need real Nelson-Oppen combination.
- **Disequality (negated `=`) is rejected**, not supported. `t != c` needs
  case-splitting (`t < c OR t > c`), which this milestone doesn't implement.
  Found to matter in practice: fuzzsmt-generated QF_LRA input hits this
  constantly (see the 2026-07-23 fuzz run-log entry).
- **`*` is more permissive than strict QF_LRA syntax**: the non-literal
  operand may be a compound linear term (`(* 2 (+ x y))`), not only a bare
  variable -- harmless (still linear) and simpler to implement than
  enforcing the stricter grammar.
- **No pivoting-rule choice yet** (Bland's rule only, for termination
  correctness, not speed -- see `docs/papers/lra-related-work.md`'s survey
  of faster alternatives for a later milestone).
- **Doesn't reuse `--pure-theory-filter`**: that optimization's soundness
  was established for EUF specifically; reusing it for LRA without
  independent verification was judged too risky for a first milestone.
- **A pre-existing, theory-agnostic robustness gap, found via this
  milestone's fuzz-gate but not caused by it**: deeply-nested S-expressions
  can stack-overflow `smtlib::SExpr`'s recursive copy constructor. QF_LRA's
  typically-deeper fuzzer output hits this more often than QF_UF's, but it
  reproduces for QF_UF too given large enough input. Not fixed here --
  tracked as a follow-up, out of scope for arithmetic specifically.

## Milestone roadmap

See `/home/user/.claude/plans/piped-wondering-fairy.md` for the full plan.
Milestones 1-3a (above) are done. Remaining:

- **Milestone 3b** (deferred, not scheduled): proactive bound propagation
  in `OnlineLraPropagator::cb_propagate()` -- the paper's "deriving
  satisfied atoms" mechanism. Explicitly *not* required for DPLL(T)
  completeness (conflict detection alone, which 3a already provides, is
  sufficient); this is a pure search-efficiency optimization, deferred per
  user direction rather than bundled into "the online propagator
  milestone" by default.
- **Milestone 4**: anti-cycling/pivoting-rule hardening, campaign-tier fuzz
  gate, before describing QF_LRA as supported/default rather than a
  first-cut milestone.
