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
  [[nodiscard]] bool has_pending() const { return !pending_.empty(); }
  [[nodiscard]] std::size_t deferred_notifications() const { return deferred_notifications_; }
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
  std::size_t deferred_notifications_ = 0;
};
// Lifetime boundary: settle the backend's reason callbacks before the view and
// consumer can be destroyed. Re-observe root variables on retained sessions.
class AssignmentConnection {
 public:
  AssignmentConnection(sat::IpasirUpBackend& backend, sat::ExternalPropagator& consumer,
                       const std::vector<int>& observed = {})
      : backend_(backend), coordinator_(consumer) {
    backend_.reconnect_external_propagator(coordinator_, observed);
  }
  ~AssignmentConnection() { backend_.disconnect_external_propagator(); }
  AssignmentConnection(const AssignmentConnection&) = delete;
  AssignmentConnection& operator=(const AssignmentConnection&) = delete;
  void flush() { coordinator_.flush(); }
  [[nodiscard]] bool has_pending() const { return coordinator_.has_pending(); }
  [[nodiscard]] std::size_t deferred_notifications() const {
    return coordinator_.deferred_notifications();
  }
  [[nodiscard]] sat::TrailView view() const { return coordinator_.view(); }

 private:
  sat::IpasirUpBackend& backend_;
  AssignmentCoordinator coordinator_;
};
}  // namespace primo::smt
