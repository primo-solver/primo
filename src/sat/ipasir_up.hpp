#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "sat/clause.hpp"

namespace primo::sat {

enum class SatResult {
  Sat,
  Unsat,
  Unknown,
  Unsupported,
};

struct BackendStatistics {
  std::size_t variables = 0;
  std::size_t clauses = 0;
  std::size_t clause_literals = 0;
  std::size_t unit_clauses = 0;
  std::size_t binary_clauses = 0;
  std::size_t conflicts = 0;
  std::size_t decisions = 0;
  std::size_t propagations = 0;
};

struct IpasirUpCapabilities {
  bool external_propagation = false;
  bool external_clauses = false;
  bool external_decisions = false;
  bool observed_variables = false;
  bool fresh_observed_variables_during_solving = false;
};

class ExternalPropagator {
 public:
  virtual ~ExternalPropagator() = default;

  virtual void notify_assignment(const std::vector<int>& lits) = 0;
  virtual void notify_new_decision_level() = 0;
  virtual void notify_backtrack(std::size_t new_level) = 0;
  virtual bool cb_check_found_model(const std::vector<int>& model) = 0;

  virtual int cb_decide() { return 0; }
  virtual int cb_propagate() { return 0; }
  virtual int cb_add_reason_clause_lit(int propagated_lit) {
    (void)propagated_lit;
    return 0;
  }
  virtual bool cb_has_external_clause(bool& is_forgettable) {
    is_forgettable = true;
    return cb_has_external_clause();
  }
  virtual bool cb_has_external_clause() { return false; }
  virtual int cb_add_external_clause_lit() { return 0; }
};

class Terminator {
 public:
  virtual ~Terminator() = default;
  virtual bool terminate() = 0;
};

class IpasirUpBackend {
 public:
  virtual ~IpasirUpBackend() = default;
  virtual int new_var() = 0;
  virtual int new_observed_var() = 0;
  virtual void add_clause(const Clause& clause) = 0;
  virtual void assume(Lit lit) = 0;
  virtual SatResult solve() = 0;
  virtual std::optional<bool> value(int var) const = 0;
  virtual bool failed(Lit lit) const = 0;
  virtual BackendStatistics statistics() const = 0;
  virtual IpasirUpCapabilities capabilities() const = 0;
  virtual void phase(Lit lit) = 0;
  virtual void connect_external_propagator(ExternalPropagator* propagator) = 0;
  virtual void disconnect_external_propagator() = 0;
  virtual void add_observed_var(int var) = 0;
  virtual void connect_terminator(Terminator* terminator) = 0;
  virtual void disconnect_terminator() = 0;
};

}  // namespace primo::sat
