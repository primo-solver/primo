#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#include "sat/trail_view.hpp"
#include "theory/theory_component.hpp"

namespace primo::theory {

// Atom-restricted read-only projection. This owns registration metadata only;
// all truth, order, and decision boundaries belong to the SMT assignment owner.
// Original protocol and lifetime checklist: docs/dpllt.md.
class ComponentAssignmentView {
 public:
  void bind(sat::TrailView view) { view_ = view; }
  void register_atoms(std::span<const ComponentAtom> atoms) {
    for (const auto& atom : atoms) {
      const auto var = variable(atom.lit);
      if (var >= by_variable_.size()) by_variable_.resize(var + 1, kAbsent);
      if (by_variable_[var] != kAbsent) throw std::logic_error("duplicate component atom variable");
      by_variable_[var] = atoms_.size();
      atoms_.push_back(atom);
    }
  }
  void check_delta(std::span<const AssignedTheoryLiteral> delta) const {
    for (const auto& literal : delta) {
      const auto var = variable(literal.lit);
      if (var >= by_variable_.size() || by_variable_[var] == kAbsent)
        throw std::logic_error("assignment of an unregistered component atom");
      const auto& atom = atoms_[by_variable_[var]];
      if (atom.term != literal.atom || atom.lit.value != literal.lit.value)
        throw std::logic_error("component assignment atom/variable mismatch");
      const int expected = literal.value ? literal.lit.value : -literal.lit.value;
      if (view_.literal(static_cast<int>(var)) != expected)
        throw std::logic_error("component assignment was not committed by its owner");
    }
  }
  [[nodiscard]] bool complete() const {
    return std::ranges::all_of(atoms_, [&](const auto& atom) {
      return view_.literal(static_cast<int>(variable(atom.lit))) != 0;
    });
  }
  [[nodiscard]] std::vector<AssignedTheoryLiteral> full_assignment(bool trail_order = false) const {
    std::vector<AssignedTheoryLiteral> result;
    result.reserve(atoms_.size());
    for (const auto& atom : atoms_) {
      const int assigned = view_.literal(static_cast<int>(variable(atom.lit)));
      if (assigned == 0) throw std::logic_error("full component check has an unassigned atom");
      result.push_back({atom.term, atom.lit, assigned == atom.lit.value});
    }
    if (trail_order) {
      std::ranges::sort(result, [&](const auto& lhs, const auto& rhs) {
        return view_.position(static_cast<int>(variable(lhs.lit))) <
               view_.position(static_cast<int>(variable(rhs.lit)));
      });
    }
    return result;
  }

 private:
  static std::size_t variable(sat::Lit literal) {
    if (literal.value == 0 || literal.value == std::numeric_limits<int>::min())
      throw std::logic_error("invalid component assignment literal");
    return static_cast<std::size_t>(literal.value < 0 ? -literal.value : literal.value);
  }
  static constexpr std::size_t kAbsent = std::numeric_limits<std::size_t>::max();
  sat::TrailView view_;
  std::vector<ComponentAtom> atoms_;
  std::vector<std::size_t> by_variable_;
};
}  // namespace primo::theory
