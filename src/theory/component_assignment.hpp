#pragma once

#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#include "theory/theory_component.hpp"

namespace primo::theory {

// Component-local assignment ownership. Routes are built once; assignment and
// undo cost O(delta). Full-model extraction alone scans the registered atoms.
class ComponentAssignment {
 public:
  void register_atoms(std::span<const ComponentAtom> atoms) {
    if (!levels_.empty() || !trail_.empty()) {
      throw std::logic_error("component atom growth requires reset assignment state");
    }
    for (const auto& atom : atoms) {
      const auto var = variable(atom.lit);
      if (var >= by_variable_.size()) {
        by_variable_.resize(var + 1, kAbsent);
      }
      if (by_variable_[var] != kAbsent) {
        throw std::logic_error("duplicate component atom variable");
      }
      by_variable_[var] = atoms_.size();
      atoms_.push_back(atom);
      values_.push_back(0);
    }
  }
  void reset() {
    for (const auto index : trail_) {
      values_[index] = 0;
    }
    trail_.clear();
    levels_.clear();
  }
  void push_level() { levels_.push_back(trail_.size()); }
  void pop_to_level(std::size_t level) {
    if (level > levels_.size()) {
      throw std::logic_error("component assignment backtrack beyond current level");
    }
    if (level == levels_.size()) {
      return;
    }
    while (trail_.size() > levels_[level]) {
      values_[trail_.back()] = 0;
      trail_.pop_back();
    }
    levels_.resize(level);
  }
  void assign(std::span<const AssignedTheoryLiteral> delta) {
    for (const auto& literal : delta) {
      const auto var = variable(literal.lit);
      if (var >= by_variable_.size() || by_variable_[var] == kAbsent) {
        throw std::logic_error("assignment of an unregistered component atom");
      }
      const auto index = by_variable_[var];
      if (atoms_[index].term != literal.atom || atoms_[index].lit.value != literal.lit.value) {
        throw std::logic_error("component assignment atom/variable mismatch");
      }
      const signed char value = literal.value ? 1 : -1;
      if (values_[index] != 0) {
        if (values_[index] != value) {
          throw std::logic_error("component assignment changed without backtracking");
        }
        continue;
      }
      values_[index] = value;
      trail_.push_back(index);
    }
  }
  [[nodiscard]] std::vector<AssignedTheoryLiteral> full_assignment() const {
    std::vector<AssignedTheoryLiteral> result;
    result.reserve(atoms_.size());
    for (std::size_t index = 0; index < atoms_.size(); ++index) {
      if (values_[index] == 0) {
        throw std::logic_error("full component check has an unassigned atom");
      }
      result.push_back({atoms_[index].term, atoms_[index].lit, values_[index] > 0});
    }
    return result;
  }

 private:
  static std::size_t variable(sat::Lit literal) {
    if (literal.value == 0 || literal.value == std::numeric_limits<int>::min()) {
      throw std::logic_error("invalid component assignment literal");
    }
    return static_cast<std::size_t>(literal.value < 0 ? -literal.value : literal.value);
  }
  static constexpr std::size_t kAbsent = std::numeric_limits<std::size_t>::max();
  std::vector<ComponentAtom> atoms_;
  std::vector<std::size_t> by_variable_;
  std::vector<signed char> values_;
  std::vector<std::size_t> trail_;
  std::vector<std::size_t> levels_;
};

}  // namespace primo::theory
