#include "theory/arithmetic/lia_patching.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "theory/arithmetic/lia_integer_values.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {

namespace {

util::Rational magnitude(const util::Rational& value) { return value.sign() < 0 ? -value : value; }

// How far either side of a variable's current value to look for an integer
// target. The two bracketing integers are not enough: in Bjorner &
// Nachmanson's own Example 3 (rows y = x/2, z = x/3, x starting at 3) the
// answer is x = 6, which corresponds to y = 3 -- two integers above y's
// current 3/2, so a floor/ceil-only candidate set never proposes it. Widening
// the window is what lets one move satisfy several rows at once, which is the
// whole point of scoring globally.
//
// Kept small deliberately: each extra target costs a full
// non_integer_count_if_moved scan, and the far ones are the least likely to
// stay inside a freedom interval. Not yet tuned against benchmarks.
constexpr int kTargetWindow = 4;

// Integers near `value`, nearest first. Ordered by distance so the caller's
// tie-break on |delta| stays meaningful and the cheapest useful move is seen
// early.
std::vector<util::Rational> integer_targets(const DeltaRational& value) {
  const util::Rational base = delta_floor(value);
  std::vector<util::Rational> targets;
  targets.reserve(2 * kTargetWindow);
  for (int offset = 0; offset < kTargetWindow; ++offset) {
    targets.push_back(base - util::Rational(offset));
    targets.push_back(base + util::Rational(offset + 1));
  }
  std::stable_sort(targets.begin(), targets.end(),
                   [&value](const util::Rational& lhs, const util::Rational& rhs) {
                     return magnitude(lhs - value.rational) < magnitude(rhs - value.rational);
                   });
  return targets;
}

// `nonbasic`'s coefficient in `basic_var`'s row, or nullopt if it does not
// occur there.
std::optional<util::Rational> row_coefficient(const LraTableau& tableau, LraVarId basic_var,
                                              LraVarId nonbasic) {
  for (const auto& entry : tableau.row(basic_var)) {
    if (entry.var == nonbasic) {
      return entry.coeff;
    }
  }
  return std::nullopt;
}

// How many declared Int variables would still be non-integral after moving
// nonbasic `moved` to `value`. This is the score every candidate move is
// judged by, and scoring globally rather than per-variable is what keeps the
// sweep from oscillating: the locally smallest |delta| that integralizes one
// basic variable routinely pulls a different one off an integer, and two such
// moves can undo each other forever. Bjorner & Nachmanson's Example 3 is
// exactly that shape -- with rows y = x/2 and z = x/3, the nearest fix for y
// alone moves x to 4 and breaks z, while the answer that fixes both is x = 6.
std::size_t non_integer_count_if_moved(const LraTableau& tableau,
                                       const std::vector<LraVarId>& integer_vars, LraVarId moved,
                                       const DeltaRational& value) {
  const DeltaRational delta = value - tableau.assignment(moved);
  std::size_t count = 0;
  for (const LraVarId var : integer_vars) {
    if (var == moved) {
      if (!is_integer_valued(value)) {
        ++count;
      }
      continue;
    }
    if (!tableau.is_basic(var)) {
      if (!is_integer_valued(tableau.assignment(var))) {
        ++count;
      }
      continue;
    }
    const std::optional<util::Rational> coefficient = row_coefficient(tableau, var, moved);
    const DeltaRational projected = coefficient.has_value()
                                        ? tableau.assignment(var) + delta.scaled(*coefficient)
                                        : tableau.assignment(var);
    if (!is_integer_valued(projected)) {
      ++count;
    }
  }
  return count;
}

struct Candidate {
  LraVarId var = 0;
  DeltaRational value;
  std::size_t resulting_count = 0;
  util::Rational distance;
};

// Every legal move that could integralize `target`: either `target` itself if
// it is nonbasic, or one of the nonbasics in its row otherwise. A move is
// legal only if it stays inside the moved variable's freedom interval, which
// is by construction the range over which it may move with every basic
// variable staying inside its bounds -- that is what makes feasibility
// preservation structural here rather than something to re-check.
void collect_candidates(const LraTableau& tableau, const std::vector<LraVarId>& integer_vars,
                        LraVarId target, std::vector<Candidate>& candidates) {
  const auto consider = [&](LraVarId moved, const DeltaRational& value) {
    if (!tableau.freedom_interval(moved).contains(value)) {
      return;
    }
    const DeltaRational delta = value - tableau.assignment(moved);
    candidates.push_back(Candidate{moved, value,
                                   non_integer_count_if_moved(tableau, integer_vars, moved, value),
                                   magnitude(delta.rational)});
  };

  const DeltaRational& current = tableau.assignment(target);
  const std::vector<util::Rational> targets = integer_targets(current);

  if (!tableau.is_basic(target)) {
    // The case the paper does not have to describe -- see the .hpp.
    for (const util::Rational& integer_target : targets) {
      consider(target, DeltaRational::from_rational(integer_target));
    }
    return;
  }

  for (const auto& entry : tableau.row(target)) {
    if (tableau.is_basic(entry.var) || entry.coeff.is_zero()) {
      continue;
    }
    const util::Rational inverse_coeff = util::Rational(1) / entry.coeff;
    for (const util::Rational& integer_target : targets) {
      // Moving `entry.var` by delta moves `target` by `entry.coeff * delta`,
      // so the delta that lands `target` exactly on `integer_target` is
      // `(integer_target - beta(target)) / entry.coeff`.
      const DeltaRational delta =
          (DeltaRational::from_rational(integer_target) - current).scaled(inverse_coeff);
      consider(entry.var, tableau.assignment(entry.var) + delta);
    }
  }
}

std::size_t count_non_integer(const LraTableau& tableau,
                              const std::vector<LraVarId>& integer_vars) {
  std::size_t count = 0;
  for (const LraVarId var : integer_vars) {
    if (!is_integer_valued(tableau.assignment(var))) {
      ++count;
    }
  }
  return count;
}

}  // namespace

std::size_t patch_integer_variables(LraTableau& tableau, const std::vector<LraVarId>& integer_vars,
                                    std::size_t patch_limit) {
  if (patch_limit == 0 || integer_vars.empty()) {
    return 0;
  }
  std::size_t remaining = count_non_integer(tableau, integer_vars);
  if (remaining == 0) {
    return 0;
  }
  const std::size_t initial = remaining;

  // Only strictly improving moves are taken. That single rule carries both
  // properties this sweep needs, and it is the same argument
  // diversify_shared_model already makes for its own monotonicity: the
  // assignment can never come out worse than it went in (so there is nothing
  // to roll back, which matters because set_nonbasic_value is not trailed),
  // and termination is free, since each accepted move strictly decreases a
  // count bounded by |integer_vars|.
  std::vector<Candidate> candidates;
  for (std::size_t attempt = 0; attempt < patch_limit && remaining > 0; ++attempt) {
    std::optional<Candidate> best;
    for (const LraVarId var : integer_vars) {
      if (is_integer_valued(tableau.assignment(var))) {
        continue;
      }
      candidates.clear();
      collect_candidates(tableau, integer_vars, var, candidates);
      for (const Candidate& candidate : candidates) {
        if (candidate.resulting_count >= remaining) {
          continue;
        }
        const bool better = !best.has_value() ||
                            candidate.resulting_count < best->resulting_count ||
                            (candidate.resulting_count == best->resulting_count &&
                             candidate.distance < best->distance);
        if (better) {
          best = candidate;
        }
      }
      // A move that integralizes everything cannot be beaten; stop looking.
      if (best.has_value() && best->resulting_count == 0) {
        break;
      }
    }
    if (!best.has_value()) {
      break;
    }
    tableau.set_nonbasic_value(best->var, best->value);
    remaining = best->resulting_count;
  }

  return initial - remaining;
}

}  // namespace primo::theory::arithmetic
