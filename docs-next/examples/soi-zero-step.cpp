#include <cassert>
#include <iostream>

#include "theory/arithmetic/lra_tableau.hpp"

// One debug example for the SOI chapter's zero-length-breakpoint limitation.
// Keep assertions enabled: this is a standalone diagnostic, not a benchmark.
using primo::sat::Lit;
using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::LraTableau;
using primo::util::Rational;

DeltaRational value(long n) { return DeltaRational::from_rational(Rational(n)); }

int main() {
  LraTableau t(LraTableau::PivotingRule::Soi);
  const auto x = t.add_variable(), y = t.add_variable();
  const auto s = t.add_variable(), r = t.add_variable();
  t.add_row(s, {{x, Rational(1)}, {y, Rational(1)}});
  t.add_row(r, {{x, Rational(1)}, {y, Rational(-1)}});
  assert(!t.assert_lower(y, value(0), Lit{1}));
  assert(!t.assert_upper(y, value(0), Lit{2}));
  assert(!t.assert_lower(s, value(1), Lit{3}));
  assert(!t.assert_lower(r, value(0), Lit{4}));
  assert(!t.check());
  std::cout << "pivots=" << t.pivot_count() << " x=" << t.assignment(x).rational.to_string()
            << " y=" << t.assignment(y).rational.to_string() << '\n';
  assert(t.assignment(x) == value(1) && t.assignment(y) == value(0));
}
