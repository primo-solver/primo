#include "sat/cadical_backend.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if PRIMO_HAVE_IPASIR
#include <cadical.hpp>
#endif

namespace primo::sat {

std::string_view cadical_signature() {
#if PRIMO_HAVE_IPASIR
  return CaDiCaL::Solver::signature();
#else
  return "unavailable";
#endif
}

CadicalBackend::CadicalBackend() : CadicalBackend(std::vector<std::pair<std::string, int>>{}) {}

CadicalBackend::CadicalBackend(const std::vector<std::pair<std::string, int>>& options) {
#if PRIMO_HAVE_IPASIR
  std::unique_ptr<CaDiCaL::Solver> solver(new CaDiCaL::Solver());
  if (solver == nullptr) {
    throw std::runtime_error("CaDiCaL solver allocation failed");
  }
  for (const auto& [name, value] : options) {
    if (!solver->set(name.c_str(), value)) {
      throw std::invalid_argument("invalid CaDiCaL option: " + name);
    }
  }
  solver_ = solver.release();
#else
  (void)options;
#endif
}

CadicalBackend::~CadicalBackend() {
#if PRIMO_HAVE_IPASIR
  if (solver_ != nullptr) {
    delete solver_;
  }
#endif
}

int CadicalBackend::new_var() {
  ++max_var_;
  statistics_.variables = static_cast<std::size_t>(max_var_);
#if PRIMO_HAVE_IPASIR
#if PRIMO_CADICAL_HAS_RESIZE
  solver_->resize(max_var_);
#elif PRIMO_CADICAL_HAS_RESERVE
  solver_->reserve(max_var_);
#endif
#endif
  return max_var_;
}

void CadicalBackend::add_clause(const Clause& clause) {
  ++statistics_.clauses;
  statistics_.clause_literals += clause.size();
  if (clause.size() == 1) {
    ++statistics_.unit_clauses;
  } else if (clause.size() == 2) {
    ++statistics_.binary_clauses;
  }
#if PRIMO_HAVE_IPASIR
  if (solver_ == nullptr) {
    throw std::runtime_error("IPASIR solver is not initialized");
  }
  for (const Lit lit : clause) {
    if (lit.value == 0) {
      throw std::invalid_argument("literal 0 is reserved as IPASIR clause terminator");
    }
    solver_->add(lit.value);
  }
  solver_->add(0);
#else
  (void)clause;
#endif
}

void CadicalBackend::assume(Lit lit) {
#if PRIMO_HAVE_IPASIR
  if (lit.value == 0) {
    throw std::invalid_argument("assumption literal must be non-zero");
  }
  solver_->assume(lit.value);
#else
  (void)lit;
#endif
}

SatResult CadicalBackend::solve() {
#if PRIMO_HAVE_IPASIR
  const int result = solver_->solve();
  if (result == 10) {
    last_result_ = SatResult::Sat;
  } else if (result == 20) {
    last_result_ = SatResult::Unsat;
  } else {
    last_result_ = SatResult::Unknown;
  }
  return last_result_;
#else
  last_result_ = SatResult::Unsupported;
  return last_result_;
#endif
}

std::optional<bool> CadicalBackend::value(int var) const {
#if PRIMO_HAVE_IPASIR
  if (last_result_ != SatResult::Sat) {
    return std::nullopt;
  }
  if (var <= 0) {
    throw std::invalid_argument("SAT variable must be positive");
  }
  const int result = solver_->val(var);
  if (result == var) {
    return true;
  }
  if (result == -var) {
    return false;
  }
  return std::nullopt;
#else
  (void)var;
  return std::nullopt;
#endif
}

bool CadicalBackend::failed(Lit lit) const {
#if PRIMO_HAVE_IPASIR
  if (last_result_ != SatResult::Unsat) {
    return false;
  }
  if (lit.value == 0) {
    throw std::invalid_argument("failed literal must be non-zero");
  }
  return solver_->failed(lit.value);
#else
  (void)lit;
  return false;
#endif
}

BackendStatistics CadicalBackend::statistics() const { return statistics_; }

void CadicalBackend::phase(Lit lit) {
#if PRIMO_HAVE_IPASIR
  if (lit.value == 0) {
    throw std::invalid_argument("phase literal must be non-zero");
  }
  solver_->phase(lit.value);
#else
  (void)lit;
#endif
}

void CadicalBackend::connect_external_propagator(CaDiCaL::ExternalPropagator* propagator) {
#if PRIMO_HAVE_IPASIR
  solver_->connect_external_propagator(propagator);
#else
  (void)propagator;
#endif
}

void CadicalBackend::disconnect_external_propagator() {
#if PRIMO_HAVE_IPASIR
  solver_->disconnect_external_propagator();
#endif
}

void CadicalBackend::connect_terminator(CaDiCaL::Terminator* terminator) {
#if PRIMO_HAVE_IPASIR
  solver_->connect_terminator(terminator);
#else
  (void)terminator;
#endif
}

void CadicalBackend::disconnect_terminator() {
#if PRIMO_HAVE_IPASIR
  solver_->disconnect_terminator();
#endif
}

void CadicalBackend::add_observed_var(int var) {
#if PRIMO_HAVE_IPASIR
  solver_->add_observed_var(var);
#else
  (void)var;
#endif
}

}  // namespace primo::sat
