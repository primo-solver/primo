#pragma once

#include <unordered_map>

#include "core/ids.hpp"
#include "core/term_manager.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {

// A normalized linear combination sum_i coeff_i * var_i + constant, over
// core::TermIds acting as opaque real-valued variables. Implements the
// paper's normal form for linear terms (Dutertre & de Moura, "A Fast
// Linear-Arithmetic Solver for DPLL(T)", Section 2).
class LinearPolynomial {
 public:
  LinearPolynomial() = default;
  explicit LinearPolynomial(util::Rational constant);

  // Builds the normalized polynomial for a Real-sorted term by recursing
  // through Plus (n-ary sum) and Times (coefficient * one child) -- see the
  // TermKind comment in core/term.hpp for why those are the only two
  // "compound" arithmetic kinds. Anything else (Constant, Application, ...)
  // is treated as an opaque variable leaf with coefficient 1.
  static LinearPolynomial from_term(const core::TermManager& terms, core::TermId term);

  void add_term(core::TermId variable, util::Rational coefficient);
  void add_constant(util::Rational value);
  void add(const LinearPolynomial& other);
  void scale(const util::Rational& factor);

  const util::Rational& constant() const { return constant_; }
  const std::unordered_map<core::TermId, util::Rational>& terms() const { return terms_; }
  bool is_constant() const { return terms_.empty(); }

 private:
  std::unordered_map<core::TermId, util::Rational> terms_;
  util::Rational constant_{0};
};

}  // namespace primo::theory::arithmetic
