#include "sat/cadical_backend.hpp"

#include <cstdlib>
#include <stdexcept>

#if PRIMO_HAVE_IPASIR
#include <ipasir.h>
#endif

namespace primo::sat {

std::string_view cadical_signature() {
#if PRIMO_HAVE_IPASIR
  return ipasir_signature();
#else
  return "unavailable";
#endif
}

CadicalBackend::CadicalBackend() {
#if PRIMO_HAVE_IPASIR
  solver_ = ipasir_init();
  if (solver_ == nullptr) {
    throw std::runtime_error("ipasir_init returned null");
  }
#endif
}

CadicalBackend::~CadicalBackend() {
#if PRIMO_HAVE_IPASIR
  if (solver_ != nullptr) {
    ipasir_release(solver_);
  }
#endif
}

int CadicalBackend::new_var() { return ++max_var_; }

void CadicalBackend::add_clause(const Clause& clause) {
#if PRIMO_HAVE_IPASIR
  if (solver_ == nullptr) {
    throw std::runtime_error("IPASIR solver is not initialized");
  }
  for (const Lit lit : clause) {
    if (lit.value == 0) {
      throw std::invalid_argument("literal 0 is reserved as IPASIR clause terminator");
    }
    ipasir_add(solver_, lit.value);
  }
  ipasir_add(solver_, 0);
#else
  (void)clause;
#endif
}

void CadicalBackend::assume(Lit lit) {
#if PRIMO_HAVE_IPASIR
  if (lit.value == 0) {
    throw std::invalid_argument("assumption literal must be non-zero");
  }
  ipasir_assume(solver_, lit.value);
#else
  (void)lit;
#endif
}

SatResult CadicalBackend::solve() {
#if PRIMO_HAVE_IPASIR
  const int result = ipasir_solve(solver_);
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
  const int result = ipasir_val(solver_, var);
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
  return ipasir_failed(solver_, lit.value) != 0;
#else
  (void)lit;
  return false;
#endif
}

}  // namespace primo::sat
