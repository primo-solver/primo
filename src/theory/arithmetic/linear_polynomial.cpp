#include "theory/arithmetic/linear_polynomial.hpp"

#include <stdexcept>
#include <utility>

#include "core/term.hpp"

namespace primo::theory::arithmetic {

LinearPolynomial::LinearPolynomial(util::Rational constant) : constant_(std::move(constant)) {}

LinearPolynomial LinearPolynomial::from_term(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  switch (term.kind) {
    case core::TermKind::Rational:
      return LinearPolynomial(term.rational_value);
    case core::TermKind::Plus: {
      LinearPolynomial result;
      for (const core::TermId child : term.children) {
        result.add(from_term(terms, child));
      }
      return result;
    }
    case core::TermKind::Times: {
      LinearPolynomial result = from_term(terms, term.children.at(0));
      result.scale(term.rational_value);
      return result;
    }
    case core::TermKind::Ite:
    case core::TermKind::IntDiv:
    case core::TermKind::IntMod:
    case core::TermKind::Abs:
      // The default arm below turns anything it does not recognise into an
      // opaque variable with coefficient 1. That is right for a constant or
      // an uninterpreted application, and badly wrong for these four: an
      // unlowered (div x 3) would enter the tableau as an unconstrained
      // variable and the solver could answer sat for an unsatisfiable goal.
      // Lowering is unconditional, so reaching here is a bug in the pipeline,
      // and it must fail loudly rather than silently.
      throw std::invalid_argument("ite/div/mod/abs reached the linear arithmetic core unlowered");
    default: {
      LinearPolynomial result;
      result.add_term(term_id, util::Rational(1));
      return result;
    }
  }
}

void LinearPolynomial::add_term(core::TermId variable, util::Rational coefficient) {
  if (coefficient.is_zero()) {
    return;
  }
  const auto found = terms_.find(variable);
  if (found == terms_.end()) {
    terms_.emplace(variable, std::move(coefficient));
    return;
  }
  found->second += coefficient;
  if (found->second.is_zero()) {
    terms_.erase(found);
  }
}

void LinearPolynomial::add_constant(util::Rational value) { constant_ += value; }

void LinearPolynomial::add(const LinearPolynomial& other) {
  for (const auto& [variable, coefficient] : other.terms_) {
    add_term(variable, coefficient);
  }
  constant_ += other.constant_;
}

void LinearPolynomial::scale(const util::Rational& factor) {
  if (factor.is_zero()) {
    terms_.clear();
    constant_ = util::Rational(0);
    return;
  }
  for (auto& [variable, coefficient] : terms_) {
    coefficient *= factor;
  }
  constant_ *= factor;
}

}  // namespace primo::theory::arithmetic
