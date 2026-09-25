#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "sat/ipasir_up.hpp"
#include "sat/trail.hpp"

namespace primo::smt {

// Sole writer of one connection's observed assignment. See docs/dpllt.md.
// The backend borrows this object; it must disconnect before destruction.
class AssignmentCoordinator final : public sat::ExternalPropagator {
 public:
  explicit AssignmentCoordinator(sat::ExternalPropagator& consumer);
  ~AssignmentCoordinator() override;
  AssignmentCoordinator(const AssignmentCoordinator&) = delete;
  AssignmentCoordinator& operator=(const AssignmentCoordinator&) = delete;

  void notify_assignment(const std::vector<int>& lits) override;
  void notify_new_decision_level() override;
  void notify_backtrack(std::size_t level) override;
  bool cb_check_found_model(const std::vector<int>& model) override;
  int cb_decide() override;
  int cb_propagate() override;
  int cb_add_reason_clause_lit(int literal) override;
  bool cb_has_external_clause(bool& forgettable) override;
  bool cb_has_external_clause() override;
  int cb_add_external_clause_lit() override;
  void flush();
  [[nodiscard]] sat::TrailView view() const { return sat::TrailView(trail_); }

 private:
  enum class Kind { Assignment, Decision, Backtrack };
  struct Event {
    Kind kind;
    std::vector<int> literals;
    std::size_t level = 0;
  };
  void deliver(Kind kind, const std::vector<int>& literals, std::size_t level = 0);
  void enqueue(Event event);
  void ready();
  sat::ExternalPropagator& consumer_;
  sat::Trail trail_;
  std::deque<Event> pending_;
  std::vector<int> delta_;
  bool delivering_ = false;
};
}  // namespace primo::smt
