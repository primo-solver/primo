#include "smt/component_dispatcher.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace primo::smt {
namespace {

std::size_t variable(int literal) {
  if (literal == 0 || literal == std::numeric_limits<int>::min()) {
    throw std::logic_error("invalid literal in component dispatcher");
  }
  return static_cast<std::size_t>(literal < 0 ? -literal : literal);
}

sat::Clause implication_clause(const theory::TheoryCheckResult& result) {
  sat::Clause clause{result.entailed_lit};
  for (const auto reason : result.reasons) {
    clause.push_back(sat::Lit{-reason.value});
  }
  return clause;
}

}  // namespace

ComponentDispatcher::ComponentDispatcher(std::span<theory::TheoryComponent* const> components,
                                         std::function<void(sat::Lit)> publish_phase)
    : components_(components.begin(), components.end()),
      deltas_(components.size()),
      dirty_(components.size(), false),
      publish_phase_(std::move(publish_phase)) {
  touched_.reserve(components.size());
  for (auto* component : components_) {
    if (component == nullptr || !component->descriptor().capabilities.incremental) {
      throw std::logic_error("online dispatch requires incremental components");
    }
  }
}

void ComponentDispatcher::register_atoms(std::size_t component,
                                         std::span<const theory::ComponentAtom> atoms) {
  if (component >= components_.size() || !trail_.empty() || !levels_.empty()) {
    throw std::logic_error("routing may grow only at a detached preparation boundary");
  }
  for (const auto& atom : atoms) {
    const auto var = variable(atom.lit.value);
    if (var >= routes_.size()) {
      routes_.resize(var + 1);
      assignment_.resize(var + 1, 0);
    }
    if (routes_[var].has_value()) {
      throw std::logic_error("ambiguous component SAT variable owner");
    }
    routes_[var] = Route{component, atom};
  }
}

void ComponentDispatcher::notify_assignment(const std::vector<int>& lits) {
  touched_.clear();
  for (const auto literal : lits) {
    const auto var = variable(literal);
    if (var >= routes_.size() || !routes_[var].has_value()) {
      continue;
    }
    if (assignment_[var] != 0) {
      if (assignment_[var] != literal) {
        throw std::logic_error("opposite assignment without component backtrack");
      }
      continue;
    }
    const auto& route = *routes_[var];
    if (deltas_[route.component].empty()) {
      touched_.push_back(route.component);
    }
    deltas_[route.component].push_back(
        {route.atom.term, route.atom.lit, (literal > 0) == (route.atom.lit.value > 0)});
    assignment_[var] = literal;
    trail_.push_back(literal);
    ++statistics_.routed_literals;
  }
  for (const auto component : touched_) {
    components_[component]->assign(deltas_[component]);
    deltas_[component].clear();
    dirty_[component] = true;
    ++statistics_.assignment_batches;
  }
  ++epoch_;
}

void ComponentDispatcher::notify_new_decision_level() {
  levels_.push_back(trail_.size());
  for (auto* component : components_) {
    component->push_level();
  }
}

void ComponentDispatcher::notify_backtrack(std::size_t level) {
  if (level > levels_.size()) {
    throw std::logic_error("component backtrack beyond current level");
  }
  if (level < levels_.size()) {
    const auto keep = levels_[level];
    while (trail_.size() > keep) {
      const auto literal = trail_.back();
      trail_.pop_back();
      assignment_[variable(literal)] = 0;
      reasons_.erase(literal);
      reasons_.erase(-literal);
    }
    levels_.resize(level);
  }
  for (auto* component : components_) {
    component->pop_to_level(level);
  }
  std::fill(dirty_.begin(), dirty_.end(), true);
  external_clause_.reset();
  clause_cursor_ = 0;
  reason_literal_ = 0;
  reason_cursor_ = 0;
  ++epoch_;
}

void ComponentDispatcher::accept_result(std::size_t component, theory::TheoryCheckResult result) {
  if (result.status == theory::TheoryCheckStatus::LearnedClause) {
    dirty_[component] = true;
    external_clause_ = Explanation{component, std::move(result.clause)};
    clause_cursor_ = 0;
  } else if (result.status == theory::TheoryCheckStatus::EntailedLiteral) {
    dirty_[component] = true;
    external_clause_ = Explanation{component, implication_clause(result)};
    clause_cursor_ = 0;
  } else if (result.status != theory::TheoryCheckStatus::Consistent) {
    // The owning solve loop must inspect this before accepting the SAT result.
    // Resource/cancellation/unsupported is never encoded as an empty clause.
    failure_ = std::move(result);
  }
}

bool ComponentDispatcher::partial_check() {
  if (failure_.has_value() || external_clause_.has_value()) {
    return !external_clause_.has_value();
  }
  for (std::size_t index = 0; index < components_.size(); ++index) {
    if (!dirty_[index]) {
      continue;
    }
    dirty_[index] = false;
    ++statistics_.partial_checks;
    accept_result(index, components_[index]->check(theory::TheoryCheckEffort::Partial, epoch_, {}));
    if (publish_phase_) {
      for (const auto phase : components_[index]->phase_literals()) {
        publish_phase_(phase);
      }
    }
    if (external_clause_.has_value() || failure_.has_value()) {
      return !external_clause_.has_value();
    }
  }
  return true;
}

bool ComponentDispatcher::cb_check_found_model(const std::vector<int>& model) {
  notify_assignment(model);
  return partial_check();
}

int ComponentDispatcher::cb_decide() {
  if (!failure_.has_value() && !external_clause_.has_value()) {
    for (auto* component : components_) {
      const auto literal = component->suggest_literal();
      if (literal.value != 0) {
        const auto var = variable(literal.value);
        if (var < routes_.size() && routes_[var].has_value() && assignment_[var] == 0) {
          return literal.value;
        }
      }
    }
  }
  return 0;
}

int ComponentDispatcher::cb_propagate() {
  if (failure_.has_value() || external_clause_.has_value()) {
    return 0;
  }
  for (std::size_t index = 0; index < components_.size(); ++index) {
    while (auto result = components_[index]->propagate()) {
      if (result->status != theory::TheoryCheckStatus::EntailedLiteral) {
        accept_result(index, std::move(*result));
        return 0;
      }
      const auto literal = result->entailed_lit.value;
      const auto var = variable(literal);
      if (var >= routes_.size() || !routes_[var].has_value()) {
        throw std::logic_error("component propagated an unregistered literal");
      }
      if (assignment_[var] == literal) {
        continue;
      }
      if (assignment_[var] == -literal) {
        accept_result(index, std::move(*result));
        return 0;
      }
      reasons_.insert_or_assign(literal, Explanation{index, implication_clause(*result)});
      ++statistics_.propagations;
      return literal;
    }
  }
  return 0;
}

int ComponentDispatcher::cb_add_reason_clause_lit(int propagated_lit) {
  const auto found = reasons_.find(propagated_lit);
  if (found == reasons_.end()) {
    throw std::logic_error("missing component propagation explanation");
  }
  if (reason_literal_ != propagated_lit) {
    reason_literal_ = propagated_lit;
    reason_cursor_ = 0;
  }
  if (reason_cursor_ == found->second.clause.size()) {
    reason_literal_ = 0;
    reason_cursor_ = 0;
    return 0;
  }
  return found->second.clause[reason_cursor_++].value;
}

bool ComponentDispatcher::cb_has_external_clause(bool& is_forgettable) {
  is_forgettable = true;
  return cb_has_external_clause();
}

bool ComponentDispatcher::cb_has_external_clause() {
  static_cast<void>(partial_check());
  return external_clause_.has_value();
}

int ComponentDispatcher::cb_add_external_clause_lit() {
  if (!external_clause_.has_value()) {
    throw std::logic_error("no pending component clause");
  }
  if (clause_cursor_ == external_clause_->clause.size()) {
    external_clause_.reset();
    clause_cursor_ = 0;
    return 0;
  }
  return external_clause_->clause[clause_cursor_++].value;
}

}  // namespace primo::smt
