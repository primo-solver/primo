#include "theory/arithmetic/lra_atom.hpp"

#include <stdexcept>
#include <utility>

#include "core/term.hpp"

namespace primo::theory::arithmetic {

LraAtom normalize_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  LraRelation relation = LraRelation::Le;
  switch (term.kind) {
    case core::TermKind::Le:
      relation = LraRelation::Le;
      break;
    case core::TermKind::Lt:
      relation = LraRelation::Lt;
      break;
    case core::TermKind::Equality:
      relation = LraRelation::Eq;
      break;
    default:
      throw std::invalid_argument("term is not an LRA atom");
  }

  LinearPolynomial difference = LinearPolynomial::from_term(terms, term.children.at(0));
  LinearPolynomial rhs_polynomial = LinearPolynomial::from_term(terms, term.children.at(1));
  rhs_polynomial.scale(util::Rational(-1));
  difference.add(rhs_polynomial);

  const util::Rational rhs_constant = -difference.constant();
  difference.add_constant(rhs_constant);

  return LraAtom{std::move(difference), relation, rhs_constant};
}

}  // namespace primo::theory::arithmetic
