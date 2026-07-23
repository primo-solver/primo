# Arithmetic

QF_LRA (quantifier-free linear real arithmetic) is implemented, Milestone 1
of the plan described below: a full SMT-LIB QF_LRA frontend plus a batch
(non-incremental) bounded-variable Simplex theory solver, following
Dutertre & de Moura's "A Fast Linear-Arithmetic Solver for DPLL(T)".
Difference logic (QF_IDL/QF_RDL) remains unimplemented; `difference_logic.hpp`
is an empty placeholder reserved for that, separate future work.

See [](papers/fast-linear-arithmetic-dpllt.md) for the base algorithm and
[](papers/lra-related-work.md) for a literature survey of extensions and
alternatives considered before/while implementing it.

## Implemented (Milestone 1)

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
  floating point). Batch/non-incremental -- no push/pop yet.
- **DPLL(T) wiring** (`check_sat.cpp`): `solve_qf_lra`/`find_lra_theory_conflict`
  rebuild a fresh tableau from the current SAT assignment each round,
  mirroring EUF's `Lazy` engine shape (`find_theory_conflict`).

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
Milestone 1 (above) is done. Remaining:

- **Milestone 2**: incremental, trail-based tableau (push/pop), mirroring
  `FlatCongruenceClosure`'s discipline.
- **Milestone 3**: `OnlineLraPropagator`, full CaDiCaL external-propagator
  integration mirroring `OnlineEufPropagator` (mid-search propagation, not
  just conflict-on-full-assignment).
- **Milestone 4**: anti-cycling/pivoting-rule hardening, campaign-tier fuzz
  gate, before describing QF_LRA as supported/default rather than a
  Milestone-1 first cut.
