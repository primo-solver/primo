#include "theory/arithmetic/lra_tableau.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace primo::theory::arithmetic {

LraVarId LraTableau::add_variable() {
  const LraVarId id = vars_.size();
  vars_.push_back(VarState{});
  return id;
}

void LraTableau::add_row(LraVarId basic_var, const std::vector<std::pair<LraVarId, util::Rational>>& terms) {
  Row row;
  row.reserve(terms.size());
  DeltaRational assignment = DeltaRational::from_rational(util::Rational(0));
  for (const auto& [var, coefficient] : terms) {
    if (coefficient.is_zero()) {
      continue;
    }
    row.emplace_back(var, coefficient);
    occurs_in_[var].push_back(basic_var);
    assignment = assignment + vars_.at(var).assignment.scaled(coefficient);
  }
  vars_.at(basic_var).is_basic = true;
  vars_.at(basic_var).assignment = assignment;
  rows_.emplace(basic_var, std::move(row));
}

std::optional<std::vector<sat::Lit>> LraTableau::assert_upper(LraVarId var, DeltaRational value,
                                                               sat::Lit reason) {
  VarState& state = vars_.at(var);
  if (state.upper.has_value() && value >= state.upper->value) {
    return std::nullopt;
  }
  if (state.lower.has_value() && value < state.lower->value) {
    return std::vector<sat::Lit>{state.lower->reason, reason};
  }
  trail_.push_back(BoundTrailEntry{var, true, state.upper});
  state.upper = Bound{value, reason};
  if (!state.is_basic && state.assignment > value) {
    update(var, value);
  }
  return std::nullopt;
}

std::optional<std::vector<sat::Lit>> LraTableau::assert_lower(LraVarId var, DeltaRational value,
                                                               sat::Lit reason) {
  VarState& state = vars_.at(var);
  if (state.lower.has_value() && value <= state.lower->value) {
    return std::nullopt;
  }
  if (state.upper.has_value() && value > state.upper->value) {
    return std::vector<sat::Lit>{state.upper->reason, reason};
  }
  trail_.push_back(BoundTrailEntry{var, false, state.lower});
  state.lower = Bound{value, reason};
  if (!state.is_basic && state.assignment < value) {
    update(var, value);
  }
  return std::nullopt;
}

void LraTableau::update(LraVarId nonbasic_var, const DeltaRational& new_value) {
  VarState& state = vars_.at(nonbasic_var);
  const DeltaRational delta = new_value - state.assignment;
  trail_.push_back(AssignmentTrailEntry{nonbasic_var, state.assignment});
  state.assignment = new_value;
  const auto found = occurs_in_.find(nonbasic_var);
  if (found == occurs_in_.end()) {
    return;
  }
  for (const LraVarId basic_var : found->second) {
    for (const auto& [var, coefficient] : rows_.at(basic_var)) {
      if (var == nonbasic_var) {
        VarState& basic_state = vars_.at(basic_var);
        trail_.push_back(AssignmentTrailEntry{basic_var, basic_state.assignment});
        basic_state.assignment = basic_state.assignment + delta.scaled(coefficient);
        break;
      }
    }
  }
}

void LraTableau::pivot(LraVarId basic_var, LraVarId entering_var) {
  PivotTrailEntry trail_entry;
  trail_entry.basic_var = basic_var;
  trail_entry.entering_var = entering_var;
  trail_entry.old_basic_row = rows_.at(basic_var);

  Row old_row = std::move(rows_.at(basic_var));
  rows_.erase(basic_var);

  util::Rational entering_coeff(0);
  Row remainder;
  remainder.reserve(old_row.size());
  for (const auto& [var, coeff] : old_row) {
    if (var == entering_var) {
      entering_coeff = coeff;
    } else {
      remainder.emplace_back(var, coeff);
    }
    auto& occurrences = occurs_in_[var];
    occurrences.erase(std::remove(occurrences.begin(), occurrences.end(), basic_var),
                      occurrences.end());
  }

  const util::Rational inv = util::Rational(1) / entering_coeff;
  Row new_entering_row;
  new_entering_row.reserve(remainder.size() + 1);
  new_entering_row.emplace_back(basic_var, inv);
  for (const auto& [var, coeff] : remainder) {
    new_entering_row.emplace_back(var, -(coeff * inv));
  }

  vars_.at(basic_var).is_basic = false;
  vars_.at(entering_var).is_basic = true;

  const std::vector<LraVarId> affected = occurs_in_[entering_var];
  occurs_in_.erase(entering_var);

  for (const LraVarId other_basic : affected) {
    Row other_row = rows_.at(other_basic);
    trail_entry.old_other_rows.emplace_back(other_basic, other_row);
    rows_.erase(other_basic);

    std::unordered_map<LraVarId, util::Rational> combined;
    util::Rational entering_in_other(0);
    for (const auto& [var, coeff] : other_row) {
      if (var == entering_var) {
        entering_in_other = coeff;
        continue;
      }
      combined[var] += coeff;
      auto& occurrences = occurs_in_[var];
      occurrences.erase(std::remove(occurrences.begin(), occurrences.end(), other_basic),
                        occurrences.end());
    }
    for (const auto& [var, coeff] : new_entering_row) {
      combined[var] += entering_in_other * coeff;
    }

    Row rebuilt;
    rebuilt.reserve(combined.size());
    for (auto& [var, coeff] : combined) {
      if (coeff.is_zero()) {
        continue;
      }
      rebuilt.emplace_back(var, coeff);
      occurs_in_[var].push_back(other_basic);
    }
    rows_.emplace(other_basic, std::move(rebuilt));
  }

  for (const auto& [var, coeff] : new_entering_row) {
    occurs_in_[var].push_back(entering_var);
  }
  rows_.emplace(entering_var, std::move(new_entering_row));

  trail_.push_back(std::move(trail_entry));
}

std::vector<sat::Lit> LraTableau::build_conflict(LraVarId violated, bool too_low) const {
  const VarState& violated_state = vars_.at(violated);
  std::vector<sat::Lit> reasons;
  reasons.push_back(too_low ? violated_state.lower->reason : violated_state.upper->reason);
  for (const auto& [var, coeff] : rows_.at(violated)) {
    const VarState& var_state = vars_.at(var);
    const bool positive = coeff.sign() > 0;
    const bool want_upper = (too_low && positive) || (!too_low && !positive);
    reasons.push_back(want_upper ? var_state.upper->reason : var_state.lower->reason);
  }
  return reasons;
}

std::optional<std::vector<sat::Lit>> LraTableau::check() {
  while (true) {
    LraVarId violated = vars_.size();
    bool too_low = false;
    for (LraVarId id = 0; id < vars_.size(); ++id) {
      const VarState& state = vars_[id];
      if (!state.is_basic) {
        continue;
      }
      if (state.lower.has_value() && state.assignment < state.lower->value) {
        violated = id;
        too_low = true;
        break;
      }
      if (state.upper.has_value() && state.assignment > state.upper->value) {
        violated = id;
        too_low = false;
        break;
      }
    }
    if (violated == vars_.size()) {
      return std::nullopt;
    }

    LraVarId entering = vars_.size();
    for (const auto& [var, coeff] : rows_.at(violated)) {
      const VarState& var_state = vars_.at(var);
      const bool positive = coeff.sign() > 0;
      const bool can_move =
          too_low ? (positive ? (!var_state.upper.has_value() || var_state.assignment < var_state.upper->value)
                              : (!var_state.lower.has_value() || var_state.assignment > var_state.lower->value))
                 : (positive ? (!var_state.lower.has_value() || var_state.assignment > var_state.lower->value)
                              : (!var_state.upper.has_value() || var_state.assignment < var_state.upper->value));
      if (can_move && var < entering) {
        entering = var;
      }
    }
    if (entering == vars_.size()) {
      return build_conflict(violated, too_low);
    }

    const DeltaRational target =
        too_low ? vars_.at(violated).lower->value : vars_.at(violated).upper->value;
    pivot(violated, entering);
    update(violated, target);
  }
}

void LraTableau::pop(std::size_t checkpoint) {
  while (trail_.size() > checkpoint) {
    undo(trail_.back());
    trail_.pop_back();
  }
}

void LraTableau::undo(const TrailEntry& entry) {
  if (const auto* bound = std::get_if<BoundTrailEntry>(&entry)) {
    VarState& state = vars_.at(bound->var);
    if (bound->is_upper) {
      state.upper = bound->previous;
    } else {
      state.lower = bound->previous;
    }
    return;
  }
  if (const auto* assignment = std::get_if<AssignmentTrailEntry>(&entry)) {
    vars_.at(assignment->var).assignment = assignment->previous;
    return;
  }
  const auto& pivot_entry = std::get<PivotTrailEntry>(entry);
  rows_.erase(pivot_entry.entering_var);
  rows_[pivot_entry.basic_var] = pivot_entry.old_basic_row;
  vars_.at(pivot_entry.basic_var).is_basic = true;
  vars_.at(pivot_entry.entering_var).is_basic = false;
  for (const auto& [other_basic, old_row] : pivot_entry.old_other_rows) {
    rows_[other_basic] = old_row;
  }
  rebuild_occurs_in_index();
}

void LraTableau::rebuild_occurs_in_index() {
  occurs_in_.clear();
  for (const auto& [basic_var, row] : rows_) {
    for (const auto& [var, coefficient] : row) {
      occurs_in_[var].push_back(basic_var);
    }
  }
}

}  // namespace primo::theory::arithmetic
