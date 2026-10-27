#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <deque>
#include <optional>
#include <stdexcept>
#include <vector>

#include "sat/cadical_backend.hpp"
#include "smt/assignment_coordinator.hpp"

using primo::sat::CadicalBackend;
using primo::sat::Clause;
using primo::sat::ExternalPropagator;
using primo::sat::Lit;
using primo::sat::SatResult;

namespace {

class AssignmentRecorder final : public ExternalPropagator {
 public:
  std::vector<int> assignments;
  void notify_assignment(const std::vector<int>& lits) override {
    assignments.insert(assignments.end(), lits.begin(), lits.end());
  }
  void notify_new_decision_level() override {}
  void notify_backtrack(std::size_t) override {}
  bool cb_check_found_model(const std::vector<int>&) override { return true; }
  bool saw(int lit) const {
    return std::find(assignments.begin(), assignments.end(), lit) != assignments.end();
  }
};

void reconnect_replays_roots_and_drops_assumptions() {
  CadicalBackend backend({{"inprocessing", 0}});
  if (!backend.capabilities().external_propagation) return;
  const int root = backend.new_var();
  const int selector = backend.new_var();
  const int atom = backend.new_var();
  backend.add_clause({Lit{root}});
  backend.add_clause({Lit{-selector}, Lit{atom}});
  AssignmentRecorder first;
  backend.reconnect_external_propagator(first, {root, atom});
  backend.assume(Lit{selector});
  assert(backend.solve() == SatResult::Sat);
  assert(first.saw(root) && first.saw(atom));
  AssignmentRecorder second;
  backend.reconnect_external_propagator(second, {root, atom});
  backend.assume(Lit{-root});
  assert(backend.solve() == SatResult::Unsat);
  AssignmentRecorder third;
  backend.reconnect_external_propagator(third, {root, atom});
  backend.add_clause({Lit{-selector}});
  backend.assume(Lit{-atom});
  assert(backend.solve() == SatResult::Sat);
  assert(third.saw(root) && third.saw(-atom));
  const int fresh = backend.new_var();
  backend.add_clause({Lit{fresh}});
  AssignmentRecorder fourth;
  backend.reconnect_external_propagator(fourth, {root, atom, fresh});
  assert(backend.solve() == SatResult::Sat);
  assert(fourth.saw(root) && fourth.saw(fresh));
  backend.disconnect_external_propagator();
}

class FreshObservedModelCheckPropagator final : public ExternalPropagator {
 public:
  explicit FreshObservedModelCheckPropagator(CadicalBackend& backend) : backend_(backend) {}

  void notify_assignment(const std::vector<int>& lits) override {
    for (const int lit : lits) {
      assignments_.push_back(lit);
    }
  }

  void notify_new_decision_level() override { ++decision_levels_; }

  void notify_backtrack(std::size_t new_level) override { backtracked_to_ = new_level; }

  bool cb_check_found_model(const std::vector<int>& /*model*/) override {
    ++model_checks_;
    if (fresh_var_.has_value()) {
      return true;
    }
    fresh_var_ = backend_.new_observed_var();
    pending_.push_back(fresh_var_.value());
    return false;
  }

  int cb_propagate() override {
    if (pending_.empty()) {
      return 0;
    }
    const int lit = pending_.front();
    pending_.pop_front();
    active_reason_lit_ = lit;
    reason_started_ = false;
    return lit;
  }

  int cb_add_reason_clause_lit(int propagated_lit) override {
    if (propagated_lit != active_reason_lit_) {
      return 0;
    }
    if (!reason_started_) {
      reason_started_ = true;
      return propagated_lit;
    }
    active_reason_lit_ = 0;
    reason_started_ = false;
    return 0;
  }

  std::optional<int> fresh_var() const { return fresh_var_; }
  std::size_t model_checks() const { return model_checks_; }
  std::size_t assignment_count() const { return assignments_.size(); }
  std::size_t decision_levels() const { return decision_levels_; }
  std::optional<std::size_t> backtracked_to() const { return backtracked_to_; }

 private:
  CadicalBackend& backend_;
  std::deque<int> pending_;
  std::vector<int> assignments_;
  std::optional<int> fresh_var_;
  std::optional<std::size_t> backtracked_to_;
  int active_reason_lit_ = 0;
  std::size_t model_checks_ = 0;
  std::size_t decision_levels_ = 0;
  bool reason_started_ = false;
};

class FreshObservedPropagationCallbackPropagator final : public ExternalPropagator {
 public:
  explicit FreshObservedPropagationCallbackPropagator(CadicalBackend& backend)
      : backend_(backend) {}

  void notify_assignment(const std::vector<int>& lits) override {
    for (const int lit : lits) {
      assignments_.push_back(lit);
    }
  }

  void notify_new_decision_level() override { ++decision_levels_; }

  void notify_backtrack(std::size_t new_level) override { backtracked_to_ = new_level; }

  bool cb_check_found_model(const std::vector<int>& /*model*/) override { return true; }

  int cb_propagate() override {
    if (propagated_) {
      return 0;
    }
    fresh_var_ = backend_.new_observed_var();
    propagated_ = true;
    active_reason_lit_ = fresh_var_.value();
    reason_started_ = false;
    return active_reason_lit_;
  }

  int cb_add_reason_clause_lit(int propagated_lit) override {
    if (propagated_lit != active_reason_lit_) {
      return 0;
    }
    if (!reason_started_) {
      reason_started_ = true;
      return propagated_lit;
    }
    active_reason_lit_ = 0;
    reason_started_ = false;
    return 0;
  }

  std::optional<int> fresh_var() const { return fresh_var_; }
  std::size_t assignment_count() const { return assignments_.size(); }
  std::size_t decision_levels() const { return decision_levels_; }
  std::optional<std::size_t> backtracked_to() const { return backtracked_to_; }

 private:
  CadicalBackend& backend_;
  std::vector<int> assignments_;
  std::optional<int> fresh_var_;
  std::optional<std::size_t> backtracked_to_;
  int active_reason_lit_ = 0;
  std::size_t decision_levels_ = 0;
  bool propagated_ = false;
  bool reason_started_ = false;
};

// A reason produced by the retired callback must be settled before its owner
// dies, and its permanent unit consequence must be replayed to the next owner.
void reconnect_after_external_reason() {
  CadicalBackend backend({}, true);
  if (!backend.capabilities().external_propagation) return;
  const int x = backend.new_var();
  backend.add_clause({Lit{x}});
  int inferred = 0;
  {
    FreshObservedPropagationCallbackPropagator first(backend);
    const primo::smt::AssignmentConnection connection(backend, first, {x});
    assert(backend.solve() == SatResult::Sat);
    inferred = *first.fresh_var();
    assert(connection.view().literal(inferred) == inferred);
  }
  AssignmentRecorder second;
  const primo::smt::AssignmentConnection connection(backend, second, {x, inferred});
  assert(backend.solve() == SatResult::Sat);
  assert(second.saw(x) && second.saw(inferred));
  backend.assume(Lit{-inferred});
  assert(backend.solve() == SatResult::Unsat);
}

void reconnect_after_cancellation() {
  class Stop final : public primo::sat::Terminator {
   public:
    bool terminate() override { return true; }
  } stop;
  CadicalBackend backend({}, true);
  if (!backend.capabilities().external_propagation) return;
  std::vector<int> variables;
  for (int i = 0; i < 100; ++i) variables.push_back(backend.new_var());
  backend.add_clause({Lit{variables.front()}});
  AssignmentRecorder first;
  backend.reconnect_external_propagator(first, variables);
  backend.connect_terminator(&stop);
  assert(backend.solve() == SatResult::Unknown);
  backend.disconnect_terminator();
  AssignmentRecorder second;
  backend.reconnect_external_propagator(second, variables);
  assert(backend.solve() == SatResult::Sat);
  assert(second.saw(variables.front()));
  backend.disconnect_external_propagator();
}

void backend_reports_sat_for_simple_clause() {
  CadicalBackend backend;
  const int x = backend.new_var();
  backend.add_clause(Clause{Lit{x}});

  const SatResult result = backend.solve();
  if (result == SatResult::Unsupported) {
    return;
  }

  assert(result == SatResult::Sat);
  assert(backend.value(x).has_value());
  assert(backend.value(x).value());
}

void backend_reports_unsat_for_contradiction() {
  CadicalBackend backend;
  const int x = backend.new_var();
  backend.add_clause(Clause{Lit{x}});
  backend.add_clause(Clause{Lit{-x}});

  const SatResult result = backend.solve();
  if (result == SatResult::Unsupported) {
    return;
  }

  assert(result == SatResult::Unsat);
}

void backend_supports_assumptions() {
  CadicalBackend backend;
  const int x = backend.new_var();
  backend.add_clause(Clause{Lit{x}});
  backend.assume(Lit{-x});

  const SatResult result = backend.solve();
  if (result == SatResult::Unsupported) {
    return;
  }

  assert(result == SatResult::Unsat);
  assert(backend.failed(Lit{-x}));
}

void backend_accepts_valid_cadical_options() {
  CadicalBackend backend({{"verbose", 0}});
  const int x = backend.new_var();
  backend.add_clause(Clause{Lit{x}});
  const SatResult result = backend.solve();
  if (result == SatResult::Unsupported) {
    return;
  }
  assert(result == SatResult::Sat);
}

void backend_rejects_invalid_cadical_options() {
  if (primo::sat::cadical_signature() == "unavailable") {
    return;
  }
  try {
    CadicalBackend backend({{"definitely_not_a_cadical_option", 1}});
  } catch (const std::invalid_argument&) {
    return;
  }
  assert(false && "expected invalid CaDiCaL option to be rejected");
}

void backend_declares_variables_safely_with_factor_enabled() {
  if (primo::sat::cadical_signature() == "unavailable") {
    return;
  }

  CadicalBackend backend({{"factor", 1}});
  const int root = backend.new_var();
  backend.add_clause(Clause{Lit{root}});
  assert(backend.solve() == SatResult::Sat);

  for (int i = 0; i < 32; ++i) {
    const int next = backend.new_var();
    backend.add_clause(Clause{Lit{-root}, Lit{next}});
    assert(backend.solve() == SatResult::Sat);
    assert(backend.value(next).has_value());
    assert(backend.value(next).value());
  }
}

void backend_observes_fresh_variable_from_model_callback() {
  if (primo::sat::cadical_signature() == "unavailable") {
    return;
  }

  CadicalBackend backend;
  const auto capabilities = backend.capabilities();
  assert(capabilities.observed_variables);
  assert(capabilities.external_propagation);
  assert(capabilities.fresh_observed_variables_during_solving);

  FreshObservedModelCheckPropagator propagator(backend);
  backend.connect_external_propagator(&propagator);
  const SatResult result = backend.solve();
  backend.disconnect_external_propagator();

  assert(result == SatResult::Sat);
  assert(propagator.model_checks() >= 2);
  assert(propagator.fresh_var().has_value());
  assert(backend.value(propagator.fresh_var().value()).has_value());
  assert(backend.value(propagator.fresh_var().value()).value());
  assert(propagator.assignment_count() >= 1);
}

void backend_observes_fresh_variable_from_propagation_callback() {
  if (primo::sat::cadical_signature() == "unavailable") {
    return;
  }

  CadicalBackend backend;
  const auto capabilities = backend.capabilities();
  assert(capabilities.observed_variables);
  assert(capabilities.external_propagation);
  assert(capabilities.fresh_observed_variables_during_solving);

  FreshObservedPropagationCallbackPropagator propagator(backend);
  backend.connect_external_propagator(&propagator);
  const SatResult result = backend.solve();
  backend.disconnect_external_propagator();

  assert(result == SatResult::Sat);
  assert(propagator.fresh_var().has_value());
  assert(backend.value(propagator.fresh_var().value()).has_value());
  assert(backend.value(propagator.fresh_var().value()).value());
  assert(propagator.assignment_count() >= 1);
}

}  // namespace

int main() {
  reconnect_replays_roots_and_drops_assumptions();
  reconnect_after_external_reason();
  reconnect_after_cancellation();
  backend_reports_sat_for_simple_clause();
  backend_reports_unsat_for_contradiction();
  backend_supports_assumptions();
  backend_accepts_valid_cadical_options();
  backend_rejects_invalid_cadical_options();
  backend_declares_variables_safely_with_factor_enabled();
  backend_observes_fresh_variable_from_model_callback();
  backend_observes_fresh_variable_from_propagation_callback();
  return 0;
}
