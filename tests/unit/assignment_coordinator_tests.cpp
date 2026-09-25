#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "smt/assignment_coordinator.hpp"

namespace {
using primo::smt::AssignmentCoordinator;
void require(bool condition) {
  if (!condition) throw std::runtime_error("assignment protocol assertion failed");
}
template <class Action>
void rejects(Action action) {
  try {
    action();
  } catch (const std::logic_error&) {
    return;
  }
  throw std::runtime_error("invalid assignment protocol event accepted");
}
struct Consumer final : primo::sat::ExternalPropagator {
  bool protected_model = false;
  int assignments = 0;
  int backtracks = 0;
  std::vector<int> removed;
  std::function<void()> during_assignment;
  bool assignment_notifications_deferred() const override { return protected_model; }
  void notify_assignment(const std::vector<int>& lits) override {
    ++assignments;
    for (const int lit : lits) require(assignment_view().literal(lit < 0 ? -lit : lit) == lit);
    if (during_assignment) during_assignment();
  }
  void notify_new_decision_level() override { require(assignment_view().level() > 0); }
  void notify_backtrack(std::size_t) override { throw std::logic_error("missing removal delta"); }
  void notify_backtrack_delta(std::size_t level, const std::vector<int>& delta) override {
    ++backtracks;
    require(assignment_view().level() == level);
    removed = delta;
    for (const int lit : delta) require(assignment_view().literal(lit < 0 ? -lit : lit) == 0);
  }
  bool cb_check_found_model(const std::vector<int>&) override { return true; }
  int cb_add_reason_clause_lit(int lit) override { return lit; }
};
void boundaries() {
  Consumer consumer;
  {
    AssignmentCoordinator owner(consumer);
    owner.notify_assignment({1, 1, -2});
    require(consumer.assignments == 1 && owner.view().size() == 2);
    const auto epoch = owner.view().epoch();
    owner.notify_assignment({-2});
    owner.notify_backtrack(0);
    require(owner.view().epoch() == epoch && consumer.backtracks == 0);
    owner.notify_new_decision_level();
    owner.notify_assignment({3});
    owner.notify_new_decision_level();
    owner.notify_new_decision_level();
    owner.notify_assignment({-4, 5});
    owner.notify_backtrack(1);
    require(consumer.removed == std::vector<int>({5, -4}));
    require(owner.view().literal(1) == 1 && owner.view().literal(2) == -2);
    require(owner.view().literal(3) == 3);
    owner.notify_assignment({4});
    owner.notify_backtrack(0);
    require(consumer.removed == std::vector<int>({4, 3}));
    require(owner.view().size() == 2);
    rejects([&] { owner.notify_assignment({-1}); });
    rejects([&] { owner.notify_assignment({0}); });
    rejects([&] { owner.notify_assignment({std::numeric_limits<int>::min()}); });
    rejects([&] { owner.notify_backtrack(1); });
    rejects([&] { owner.cb_check_found_model({-1, -2}); });
    require(owner.cb_check_found_model({1, -2, 8}));
  }
  require(!consumer.assignment_view().attached());
}
void protected_and_reentrant_delivery() {
  Consumer consumer;
  AssignmentCoordinator owner(consumer);
  owner.notify_assignment({1});
  const auto epoch = owner.view().epoch();
  consumer.protected_model = true;
  owner.notify_new_decision_level();
  owner.notify_assignment({2});
  owner.notify_new_decision_level();
  owner.notify_assignment({-3});
  owner.notify_backtrack(1);
  owner.notify_assignment({3});
  require(owner.view().epoch() == epoch && owner.view().size() == 1);
  require(consumer.assignments == 1 && consumer.backtracks == 0);
  require(owner.cb_add_reason_clause_lit(7) == 7);
  rejects([&] { owner.cb_propagate(); });
  consumer.protected_model = false;
  owner.flush();
  require(owner.view().level() == 1 && owner.view().literal(3) == 3);
  require(consumer.assignments == 4 && consumer.backtracks == 1);
  bool reentered = false;
  consumer.during_assignment = [&] {
    if (reentered) return;
    reentered = true;
    owner.notify_backtrack(0);
    owner.notify_assignment({-2});
    require(owner.view().literal(2) == 2);
  };
  owner.notify_assignment({4});
  require(owner.view().level() == 0 && owner.view().literal(2) == -2);
  require(owner.view().literal(3) == 0 && owner.view().literal(4) == 0);
}
void protection_can_begin_during_replay() {
  Consumer consumer;
  AssignmentCoordinator owner(consumer);
  consumer.protected_model = true;
  owner.notify_assignment({1});
  owner.notify_assignment({2});
  consumer.protected_model = false;
  consumer.during_assignment = [&] { consumer.protected_model = true; };
  rejects([&] { owner.cb_propagate(); });
  require(owner.view().literal(1) == 1 && owner.view().literal(2) == 0);
  consumer.during_assignment = {};
  consumer.protected_model = false;
  owner.flush();
  require(owner.view().literal(2) == 2);
}
void snapshot_and_growth() {
  primo::sat::Trail model;
  const primo::sat::TrailView view(model);
  const std::vector first{1, -2};
  model.load_model(first);
  const auto epoch = view.epoch();
  model.assign(100000);
  require(view.literal(1) == 1 && view.position(100000) == 2);
  const std::vector second{-1, 2};
  model.load_model(second);
  require(view.literal(1) == -1 && view.literal(100000) == 0 && view.epoch() > epoch);
}
}  // namespace
int main() {
  try {
    boundaries();
    protected_and_reentrant_delivery();
    snapshot_and_growth();
    protection_can_begin_during_replay();
    std::cout << "PASS: shared assignment protocol\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
