#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lia_branch_and_bound.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

using primo::sat::Lit;
using primo::theory::arithmetic::branch_and_bound;
using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::LiaBranchAndBoundResult;
using primo::theory::arithmetic::LraTableau;
using primo::theory::arithmetic::LraVarId;
using primo::util::Rational;

namespace {

DeltaRational rat(long value) { return DeltaRational::from_rational(Rational(value)); }

void no_branching_needed_when_already_integer() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(2), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(5), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  const auto result = branch_and_bound(tableau, {x}, 100);
  assert(result.status == LiaBranchAndBoundResult::Status::Sat);
  assert(tableau.assignment(x) == rat(2));
}

// s = 2x + y, s fixed at 3: the LRA relaxation lands on a fractional x
// (1.5, with y picking up whatever the row needs), but 2x + y == 3 has
// plenty of integer solutions (e.g. x=1, y=1) -- one branch on x should
// find one directly.
void single_branch_reaches_an_integer_solution() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(2)}, {y, Rational(1)}});
  assert(!tableau.assert_upper(s, rat(3), Lit{1}).has_value());
  assert(!tableau.assert_lower(s, rat(3), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  const auto result = branch_and_bound(tableau, {x, y}, 100);
  assert(result.status == LiaBranchAndBoundResult::Status::Sat);
  assert(tableau.assignment(x).rational.is_integer());
  assert(tableau.assignment(x).infinitesimal.is_zero());
  assert(tableau.assignment(y).rational.is_integer());
  assert(tableau.assignment(y).infinitesimal.is_zero());
  // The row equation must still hold at the winning leaf, not just look
  // integer in isolation.
  assert(tableau.assignment(x).scaled(Rational(2)) + tableau.assignment(y) == rat(3));
}

// s = 2x, s fixed at 3: 2x == 3 has no integer solution at all (2x is
// always even), and x has no other freedom, so both branches (x<=1,
// x>=2) fail immediately -- Unsat after one level, and every bound this
// call asserted on x must be gone afterward (branch_and_bound's failure
// contract; see LraTableau::pop()'s own bounds-only restoration -- the
// assignment itself is not part of that contract).
void every_branch_is_infeasible_returns_unsat() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(2)}});
  assert(!tableau.assert_upper(s, rat(3), Lit{1}).has_value());
  assert(!tableau.assert_lower(s, rat(3), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.assignment(x) == DeltaRational::from_rational(Rational(3, 2)));
  assert(!tableau.upper_bound(x).has_value());
  assert(!tableau.lower_bound(x).has_value());

  const auto result = branch_and_bound(tableau, {x}, 100);
  assert(result.status == LiaBranchAndBoundResult::Status::Unsat);
  assert(!tableau.upper_bound(x).has_value());
  assert(!tableau.lower_bound(x).has_value());
}

// s = 2x - 2y, s fixed at 1, x and y both unbounded: 2x - 2y == 1 has no
// integer solution (2x - 2y is always even), but with y always free to
// absorb any integer choice of x (or vice versa), the LRA relaxation
// never becomes infeasible at any node -- plain branch-and-bound
// diverges here without cuts (Bjørner & Nachmanson, Sect. 4; this is
// exactly the incompleteness docs/qf-lia-branch-and-bound.md documents).
// A small node limit must stop the search rather than hang, and must
// still leave no leftover branch bound on the way out (see the note on
// every_branch_is_infeasible_returns_unsat above about what "restore" does
// and does not cover).
void unbounded_polytope_hits_the_node_limit() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(2)}, {y, Rational(-2)}});
  assert(!tableau.assert_upper(s, rat(1), Lit{1}).has_value());
  assert(!tableau.assert_lower(s, rat(1), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(!tableau.upper_bound(x).has_value());
  assert(!tableau.lower_bound(x).has_value());
  assert(!tableau.upper_bound(y).has_value());
  assert(!tableau.lower_bound(y).has_value());

  const auto result = branch_and_bound(tableau, {x, y}, 10);
  assert(result.status == LiaBranchAndBoundResult::Status::LimitReached);
  assert(!tableau.upper_bound(x).has_value());
  assert(!tableau.lower_bound(x).has_value());
  assert(!tableau.upper_bound(y).has_value());
  assert(!tableau.lower_bound(y).has_value());
}

}  // namespace

int main() {
  no_branching_needed_when_already_integer();
  single_branch_reaches_an_integer_solution();
  every_branch_is_infeasible_returns_unsat();
  unbounded_polytope_hits_the_node_limit();
  return 0;
}
