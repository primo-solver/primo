#include "smt/assignment_coordinator.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace primo::smt {

AssignmentCoordinator::AssignmentCoordinator(sat::ExternalPropagator& consumer)
    : consumer_(consumer) {
  consumer_.bind_assignment_view(view());
}

AssignmentCoordinator::~AssignmentCoordinator() { consumer_.bind_assignment_view({}); }

void AssignmentCoordinator::notify_assignment(const std::vector<int>& lits) {
  // The normal path borrows the backend batch. Only delayed/reentrant events copy it.
  if (!delivering_ && !consumer_.assignment_notifications_deferred() && pending_.empty()) {
    deliver(Kind::Assignment, lits);
    flush();
  } else {
    enqueue(Event{Kind::Assignment, lits});
  }
}

void AssignmentCoordinator::notify_new_decision_level() {
  if (!delivering_ && !consumer_.assignment_notifications_deferred() && pending_.empty()) {
    deliver(Kind::Decision, {});
    flush();
  } else {
    enqueue(Event{Kind::Decision, {}});
  }
}
void AssignmentCoordinator::notify_backtrack(std::size_t level) {
  if (!delivering_ && !consumer_.assignment_notifications_deferred() && pending_.empty()) {
    deliver(Kind::Backtrack, {}, level);
    flush();
  } else {
    enqueue(Event{Kind::Backtrack, {}, level});
  }
}

void AssignmentCoordinator::enqueue(Event event) {
  if (consumer_.assignment_notifications_deferred()) ++deferred_notifications_;
  pending_.push_back(std::move(event));
  flush();
}

void AssignmentCoordinator::deliver(Kind kind, const std::vector<int>& literals,
                                    std::size_t level) {
  delivering_ = true;
  try {
    switch (kind) {
      case Kind::Assignment:
        delta_.clear();
        for (const auto lit : literals) {
          if (trail_.assign(lit)) delta_.push_back(lit);
        }
        if (!delta_.empty()) consumer_.notify_assignment(delta_);
        break;
      case Kind::Decision:
        trail_.push_level();
        consumer_.notify_new_decision_level();
        break;
      case Kind::Backtrack:
        if (level != trail_.level()) {
          trail_.backtrack(level, delta_);
          consumer_.notify_backtrack_delta(level, delta_);
        }
        break;
    }
    delivering_ = false;
  } catch (...) {
    delivering_ = false;
    throw;
  }
}

void AssignmentCoordinator::flush() {
  if (delivering_ || consumer_.assignment_notifications_deferred()) return;
  while (!pending_.empty()) {
    Event event = std::move(pending_.front());
    pending_.pop_front();
    deliver(event.kind, event.literals, event.level);
    if (consumer_.assignment_notifications_deferred()) break;
  }
}

void AssignmentCoordinator::ready() {
  if (delivering_ || consumer_.assignment_notifications_deferred()) {
    throw std::logic_error("solver callback inside protected assignment delivery");
  }
  flush();
  if (consumer_.assignment_notifications_deferred()) {
    throw std::logic_error("assignment delivery opened a protected model scope");
  }
}

bool AssignmentCoordinator::cb_check_found_model(const std::vector<int>& model) {
  ready();
  for (const auto lit : model) {
    if (lit == 0 || lit == std::numeric_limits<int>::min()) {
      throw std::logic_error("invalid model literal");
    }
    const int assigned = trail_.literal(lit < 0 ? -lit : lit);
    if (assigned != 0 && assigned != lit) {
      throw std::logic_error("model contradicts notified assignment");
    }
  }
  return consumer_.cb_check_found_model(model);
}

int AssignmentCoordinator::cb_decide() {
  ready();
  return consumer_.cb_decide();
}
int AssignmentCoordinator::cb_propagate() {
  ready();
  return consumer_.cb_propagate();
}
int AssignmentCoordinator::cb_add_reason_clause_lit(int literal) {
  // Existing stored reasons may be needed while model-view notifications wait.
  if (!consumer_.assignment_notifications_deferred()) ready();
  return consumer_.cb_add_reason_clause_lit(literal);
}
bool AssignmentCoordinator::cb_has_external_clause(bool& forgettable) {
  ready();
  return consumer_.cb_has_external_clause(forgettable);
}
bool AssignmentCoordinator::cb_has_external_clause() {
  ready();
  return consumer_.cb_has_external_clause();
}
int AssignmentCoordinator::cb_add_external_clause_lit() {
  ready();
  return consumer_.cb_add_external_clause_lit();
}
}  // namespace primo::smt
