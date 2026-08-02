#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <stdexcept>

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

}  // namespace

int main() {
  backend_reports_sat_for_simple_clause();
  backend_reports_unsat_for_contradiction();
  backend_supports_assumptions();
  backend_accepts_valid_cadical_options();
  backend_rejects_invalid_cadical_options();
  backend_declares_variables_safely_with_factor_enabled();
  return 0;
}
