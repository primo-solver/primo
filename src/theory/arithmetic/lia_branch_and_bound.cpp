#include "theory/arithmetic/lia_branch_and_bound.hpp"

#include <optional>

#include "sat/lit.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {

namespace {

std::optional<LraVarId> find_fractional_variable(const LraTableau& tableau,
                                                 const std::vector<LraVarId>& integer_vars) {
  for (const LraVarId var : integer_vars) {
    const DeltaRational& value = tableau.assignment(var);
    if (!value.infinitesimal.is_zero() || !value.rational.is_integer()) {
      return var;
    }
  }
  return std::nullopt;
}

// Asserts one side of the split (var <= floor_value, or var >=
// floor_value + 1) and checks it. Returns whether that side is feasible;
// on false, whatever the assert/check touched is left in place for the
// caller to pop().
bool try_branch(LraTableau& tableau, LraVarId var, const util::Rational& floor_value,
                bool low_side) {
  const std::optional<std::vector<sat::Lit>> assert_conflict =
      low_side
          ? tableau.assert_upper(var, DeltaRational::from_rational(floor_value), sat::Lit{0})
          : tableau.assert_lower(var, DeltaRational::from_rational(floor_value + util::Rational(1)),
                                 sat::Lit{0});
  if (assert_conflict.has_value()) {
    return false;
  }
  return !tableau.check().has_value();
}

}  // namespace

LiaBranchAndBoundResult branch_and_bound(LraTableau& tableau,
                                         const std::vector<LraVarId>& integer_vars,
                                         std::size_t node_limit) {
  struct Frame {
    LraVarId var = 0;
    util::Rational floor_value;
    std::size_t checkpoint = 0;
    bool tried_high = false;
  };
  std::vector<Frame> stack;
  const std::size_t entry_checkpoint = tableau.push();
  std::size_t nodes = 0;

  while (true) {
    const std::optional<LraVarId> fractional = find_fractional_variable(tableau, integer_vars);
    if (!fractional.has_value()) {
      return LiaBranchAndBoundResult{LiaBranchAndBoundResult::Status::Sat};
    }
    if (nodes >= node_limit) {
      tableau.pop(entry_checkpoint);
      return LiaBranchAndBoundResult{LiaBranchAndBoundResult::Status::LimitReached};
    }
    ++nodes;

    Frame frame;
    frame.var = *fractional;
    frame.floor_value = tableau.assignment(*fractional).rational.floor();
    frame.checkpoint = tableau.push();
    stack.push_back(frame);

    if (try_branch(tableau, frame.var, frame.floor_value, /*low_side=*/true)) {
      continue;  // Descend: re-scan for the next fractional variable.
    }

    // The low side of the frame just pushed failed. Unwind the stack,
    // trying each frame's untried high side in turn, until one succeeds
    // (resume descending from there) or the stack empties (Unsat).
    bool resumed = false;
    while (!stack.empty()) {
      Frame& top = stack.back();
      if (!top.tried_high) {
        tableau.pop(top.checkpoint);
        top.checkpoint = tableau.push();
        top.tried_high = true;
        if (try_branch(tableau, top.var, top.floor_value, /*low_side=*/false)) {
          resumed = true;
          break;
        }
      }
      tableau.pop(top.checkpoint);
      stack.pop_back();
    }
    if (!resumed) {
      return LiaBranchAndBoundResult{LiaBranchAndBoundResult::Status::Unsat};
    }
  }
}

}  // namespace primo::theory::arithmetic
