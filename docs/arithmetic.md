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
- **Disequality**: `Real`-sorted `=` is desugared to `(t1 <= t2) AND (t2 <= t1)`
  at parse time (`Frontend::mk_theory_equality`, since Real is a total
  order), not handled as a special relation in the tableau at all. `NOT(t1=t2)`
  is then just `NOT(AND(Le,Le))`, already correctly handled by
  `BooleanClausifier`'s existing Tseitin/CNF machinery for arbitrary
  Boolean structure -- ordinary CDCL branching over the two underlying
  `Le` atoms explores `t1<t2` vs `t1>t2` for free. No case-splitting or
  lemma-learning code exists anywhere in `LraTableau`/`OnlineLraPropagator`;
  `LraAtom`'s `Eq` relation is unreachable through the frontend (kept as
  defensive dead code, not removed, in case a future code path builds a
  `Real` equality term some other way).

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
Milestones 1-3a (above) are done, plus disequality support. Remaining:

- **Milestone 3b** (deferred, not scheduled, but see the real-benchmark
  finding below): proactive bound propagation in
  `OnlineLraPropagator::cb_propagate()` -- the paper's "deriving satisfied
  atoms" mechanism. Explicitly *not* required for DPLL(T) completeness
  (conflict detection alone, which 3a already provides, is sufficient) --
  but a real-benchmark evaluation against `tmp/QF_LRA` (a local corpus of
  1753 `.smt2` files across 17 families, not committed) found that even a
  single moderate-size real formula (107 assertions) makes genuine but slow
  progress without ever finishing (6869 decisions, 690 conflicts, 66373
  pure-Boolean propagations in 15s, zero theory propagations) -- i.e.
  *not* a completeness problem (search does progress) but a strong signal
  that the lack of propagation is a real practical bottleneck, independent
  of the completeness argument that originally justified deferring it.
  Worth prioritizing before Milestone 4.
- **Milestone 4**: anti-cycling/pivoting-rule hardening (mostly already
  done -- Bland's rule has been in `LraTableau::check()` since Milestone 1),
  campaign-tier fuzz gate, before describing QF_LRA as supported/default
  rather than a first-cut milestone.
