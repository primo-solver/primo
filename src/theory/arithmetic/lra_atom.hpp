#pragma once

#include "core/term_manager.hpp"
#include "theory/arithmetic/linear_polynomial.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {

enum class LraRelation {
  Le,
  Lt,
  Eq,
};

// Canonical "t ~ c" atom form (Dutertre & de Moura, "A Fast Linear-
// Arithmetic Solver for DPLL(T)", Section 2): a linear polynomial with no
// constant term of its own, related to a separate rational constant. Built
// by moving every constant from both sides of the original atom onto the
// right-hand side.
struct LraAtom {
  LinearPolynomial polynomial;
  LraRelation relation = LraRelation::Le;
  util::Rational rhs;
};

// Normalizes a Le/Lt/Equality core::Term over Real or Int operands (as
// produced by check_sat.cpp's build_term) into its LraAtom form. Throws
// std::invalid_argument if term_id isn't one of those kinds.
//
// Sort-agnostic by design: QF_LIA's integer-specific strictness handling
// lives in check_sat.cpp's bound_assertion(), not here. An earlier version
// tightened Int `<` atoms to `<=` at this layer, but PreparedLraTheory's
// scale-canonicalization (dividing a compound atom's polynomial and rhs by
// its leading coefficient) runs *after* normalize_atom, and can turn an
// already-tightened integer rhs into a non-integer one (e.g. `5x < 0`
// tightened here to `5x <= -1`, then canonicalized to `x <= -1/5`) --
// which a later naive "+/-1" fix-up would then get wrong. bound_assertion
// uses floor()/ceil() on the final, possibly-scaled rhs instead, which is
// correct regardless of what coefficient the original atom had.
LraAtom normalize_atom(const core::TermManager& terms, core::TermId term_id);

}  // namespace primo::theory::arithmetic
