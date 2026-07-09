#include <cassert>

#include "sat/cadical_backend.hpp"

using primo::sat::CadicalBackend;
using primo::sat::Clause;
using primo::sat::Lit;
using primo::sat::SatResult;

namespace {

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

}  // namespace

int main() {
  backend_reports_sat_for_simple_clause();
  backend_reports_unsat_for_contradiction();
  backend_supports_assumptions();
  return 0;
}
