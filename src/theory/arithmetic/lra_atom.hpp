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

// Normalizes a Le/Lt/Equality-on-Real core::Term (as produced by
// check_sat.cpp's build_term) into its LraAtom form. Throws
// std::invalid_argument if term_id isn't one of those kinds.
LraAtom normalize_atom(const core::TermManager& terms, core::TermId term_id);

}  // namespace primo::theory::arithmetic
