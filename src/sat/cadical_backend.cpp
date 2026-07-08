#include "sat/cadical_backend.hpp"

namespace primo::sat {

void CadicalBackend::add_clause(const Clause&) {}

SatResult CadicalBackend::solve() {
  return SatResult::Unsupported;
}

}  // namespace primo::sat
