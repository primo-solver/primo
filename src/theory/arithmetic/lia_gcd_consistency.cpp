#include "theory/arithmetic/lia_gcd_consistency.hpp"

#include <vector>

#include "util/rational.hpp"

namespace primo::theory::arithmetic {

namespace {

// `var`'s fixed value, if it currently has one (equal, delta-free upper
// and lower bounds). Basic or non-basic: which variable in a row happens
// to be basic can change after a pivot without changing which underlying
// variables are actually fixed, so this check must not assume `var` is
// basic or non-basic either way.
std::optional<util::Rational> fixed_value(const LraTableau& tableau, LraVarId var) {
  const auto upper = tableau.upper_bound(var);
  const auto lower = tableau.lower_bound(var);
  if (!upper.has_value() || !lower.has_value() || upper->value != lower->value) {
    return std::nullopt;
  }
  // QF_LIA never asserts a strict bound with a nonzero infinitesimal
  // (lra_atom.cpp/bound_assertion() keep every Int bound delta-free); if
  // one ever showed up here anyway, treat it as not fixed rather than
  // risk a false conflict on a value that isn't really achievable at all.
  if (!upper->value.infinitesimal.is_zero()) {
    return std::nullopt;
  }
  return upper->value.rational;
}

// `basic_var`'s row is `basic_var = sum(entry.coeff * entry.var)`, i.e.
// `1*basic_var - sum(entry.coeff * entry.var) = 0`. Folds every currently
// *fixed* variable in that equation -- basic_var itself included, since a
// pivot can move which variable in a given linear relationship happens to
// be basic without changing which underlying variables are fixed -- into
// a residual constant, leaving `sum(free_coeff * free_var) = residual`
// over the remaining free (Int-sorted, ranging over all of Z) variables.
// That is achievable iff gcd(free_coeff) divides residual; if there are
// no free variables left, every quantity in the row is already
// known/fixed and this derives nothing beyond what ordinary LRA
// consistency already checked.
bool row_is_gcd_consistent(const LraTableau& tableau, LraVarId basic_var) {
  std::vector<util::Rational> free_coefficients;
  util::Rational residual(0);

  if (const auto basic_fixed = fixed_value(tableau, basic_var); basic_fixed.has_value()) {
    residual -= *basic_fixed;  // basic_var's own signed coefficient is +1.
  } else {
    free_coefficients.push_back(util::Rational(1));
  }

  for (const auto& entry : tableau.row(basic_var)) {
    if (const auto term_fixed = fixed_value(tableau, entry.var); term_fixed.has_value()) {
      residual += entry.coeff * (*term_fixed);
    } else {
      free_coefficients.push_back(entry.coeff);
    }
  }

  if (free_coefficients.empty()) {
    return true;
  }

  // Recover integer coefficients + residual consistently: row coefficients
  // (and the residual, built from bound values) can be non-integer here
  // even when the original atoms' were, from PreparedLraTheory's
  // scale-canonicalization -- the same trap stage 1's bound_assertion()
  // bug (docs/qf-lia-branch-and-bound.md) already showed up once.
  std::vector<util::Rational> combined = free_coefficients;
  combined.push_back(residual);
  const util::Rational scale = util::Rational::primitive_integer_scale(combined);

  std::vector<util::Rational> scaled_coefficients;
  scaled_coefficients.reserve(free_coefficients.size());
  for (const util::Rational& coefficient : free_coefficients) {
    scaled_coefficients.push_back(coefficient * scale);
  }
  const util::Rational divisor = util::Rational::gcd(scaled_coefficients);
  const util::Rational scaled_residual = residual * scale;
  return (scaled_residual / divisor).is_integer();
}

}  // namespace

std::optional<LraVarId> gcd_inconsistent_row(const LraTableau& tableau) {
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    if (!tableau.is_basic(var)) {
      continue;
    }
    if (!row_is_gcd_consistent(tableau, var)) {
      return var;
    }
  }
  return std::nullopt;
}

}  // namespace primo::theory::arithmetic
