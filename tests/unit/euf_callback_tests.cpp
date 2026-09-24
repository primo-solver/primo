#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "sat/clause.hpp"
#include "sat/lit.hpp"
#include "smt/check_sat.hpp"
#include "smt/solver_engines.hpp"
#include "theory/theory_component.hpp"
#include "theory/theory_solver.hpp"
#include "util/statistics.hpp"

namespace {
using namespace primo;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

// Drive the production EUF callback through its existing component adapter.
// No SAT branching heuristic or private-state inspection determines this trace.
// IPASIR-UP Section 2.2: backtrack(k) retains assignments at levels <= k.
struct Fixture {
  enum Atom { AB = 1, BC, CD, AD, DE };
  core::SymbolTable symbols;
  core::TermManager terms{symbols};
  smt::CheckSatOptions options;
  util::Statistics statistics;
  std::vector<theory::ComponentAtom> atoms;
  std::unique_ptr<theory::TheoryComponent> component;
  std::array<int, 6> assignment{};
  static constexpr std::array<std::pair<std::size_t, std::size_t>, 5> endpoints{
      {{0, 1}, {1, 2}, {2, 3}, {0, 3}, {3, 4}}};

  Fixture() {
    const auto sort = symbols.declare_sort("U");
    std::array<core::TermId, 5> constants{};
    for (std::size_t i = 0; i < constants.size(); ++i) {
      constants[i] = terms.mk_constant(symbols.declare_constant("u" + std::to_string(i), sort));
    }
    for (const auto& [lhs, rhs] : endpoints) {
      atoms.push_back({terms.mk_eq(constants[lhs], constants[rhs]),
                       sat::Lit{static_cast<int>(atoms.size()) + 1}, true, true, true});
    }
    options.euf_propagation = smt::EufPropagationMode::Online;
    options.pure_theory_filter = smt::PureTheoryFilterMode::Off;
    options.euf_propagation_interval = 1;
    options.euf_propagation_assign_threshold = 1.0;
    options.qf_uflia_model_combination = false;
    component = smt::make_uf_euf_component(terms, options, statistics, true);
    require(component != nullptr, "EUF callback tests require the IPASIR backend");
    component->register_interface({});
    component->register_atoms(atoms);
  }

  void assign(Atom atom, bool value = true) {
    const auto index = static_cast<std::size_t>(atom);
    const std::array delta{
        theory::AssignedTheoryLiteral{atoms[index - 1].term, atoms[index - 1].lit, value}};
    component->assign(delta);
    assignment[index] = value ? 1 : -1;
  }

  // Independent finite oracle: all maps of five constants into five values
  // cover every equality partition. No Primo equality engine is used here.
  bool valid_clause(const sat::Clause& clause) const {
    for (std::size_t encoding = 0; encoding < 3125; ++encoding) {
      auto rest = encoding;
      std::array<std::size_t, 5> values{};
      for (auto& value : values) {
        value = rest % 5;
        rest /= 5;
      }
      bool satisfied = false;
      for (const auto literal : clause) {
        const auto var =
            static_cast<std::size_t>(literal.value < 0 ? -literal.value : literal.value);
        require(var > 0 && var <= endpoints.size(), "explanation contains an unknown atom");
        const auto [lhs, rhs] = endpoints[var - 1];
        satisfied |= (values[lhs] == values[rhs]) == (literal.value > 0);
      }
      if (!satisfied) return false;
    }
    return true;
  }

  void expect_ad_propagation() {
    const auto result = component->propagate();
    require(result.has_value(), "lost equality propagation after backtrack");
    require(result->entailed_lit.value == AD, "unexpected or already assigned consequence");
    require(!result->reasons.empty(), "transitive equality needs antecedents");
    sat::Clause clause{result->entailed_lit};
    for (const auto reason : result->reasons) {
      const auto var = static_cast<std::size_t>(reason.value < 0 ? -reason.value : reason.value);
      require(var > 0 && var < assignment.size(), "reason names an unknown atom");
      require(assignment[var] == (reason.value > 0 ? 1 : -1), "reason is not currently true");
      clause.push_back(sat::Lit{-reason.value});
    }
    require(valid_clause(clause), "propagation explanation is not equality-valid");
  }

  void expect_conflict(bool conflict) {
    const auto result = component->check(theory::TheoryCheckEffort::Partial, 1, {});
    require(result.status == (conflict ? theory::TheoryCheckStatus::LearnedClause
                                       : theory::TheoryCheckStatus::Consistent),
            "wrong conflict result after backtrack");
    if (conflict) {
      require(valid_clause(result.clause), "conflict explanation is not equality-valid");
      for (const auto literal : result.clause) {
        const auto var =
            static_cast<std::size_t>(literal.value < 0 ? -literal.value : literal.value);
        require(assignment[var] == (literal.value > 0 ? -1 : 1),
                "conflict clause is not falsified");
      }
    }
  }
};

void root_assignments_survive() {
  Fixture f;
  f.assign(Fixture::AB);
  f.assign(Fixture::BC);
  f.component->push_level();
  f.assign(Fixture::DE);
  f.component->pop_to_level(0);
  f.assignment[Fixture::DE] = 0;
  f.component->push_level();
  f.assign(Fixture::CD);
  f.expect_ad_propagation();
}

void retained_level_assignments_survive() {
  Fixture f;
  f.assign(Fixture::AB);
  f.component->push_level();
  f.assign(Fixture::BC);
  f.component->push_level();
  f.assign(Fixture::DE);
  f.component->pop_to_level(1);
  f.assignment[Fixture::DE] = 0;
  f.assign(Fixture::CD);
  f.expect_ad_propagation();
}

void empty_levels_and_repeated_backtracks() {
  Fixture f;
  f.assign(Fixture::AB);
  f.component->push_level();
  f.assign(Fixture::BC);
  f.component->push_level();  // SAT decision on an unobserved variable.
  f.component->push_level();
  f.assign(Fixture::DE);
  f.component->pop_to_level(2);
  f.assignment[Fixture::DE] = 0;
  f.component->pop_to_level(1);
  f.component->push_level();
  f.assign(Fixture::CD);
  f.expect_ad_propagation();
}

void closure_preserves_root_and_retained_facts() {
  for (const bool root : {false, true}) {
    Fixture f;
    f.assign(Fixture::AB);
    if (!root) f.component->push_level();
    f.assign(Fixture::BC);
    f.component->push_level();
    f.assign(Fixture::DE);
    f.component->pop_to_level(root ? 0 : 1);
    f.assignment[Fixture::DE] = 0;
    f.assign(Fixture::CD);
    f.assign(Fixture::AD, false);
    f.expect_conflict(true);
  }
}

void discarded_assignments_can_change_polarity() {
  Fixture f;
  f.assign(Fixture::AB);
  f.component->push_level();
  f.assign(Fixture::BC);
  f.assign(Fixture::CD);
  f.assign(Fixture::AD, false);
  f.expect_conflict(true);
  f.component->pop_to_level(0);
  f.assignment = {};
  f.assignment[Fixture::AB] = 1;
  f.component->push_level();
  f.assign(Fixture::BC, false);
  f.assign(Fixture::CD);
  f.assign(Fixture::AD, false);
  f.assign(Fixture::DE);
  f.expect_conflict(false);
  const auto checked = f.component->check(theory::TheoryCheckEffort::FullModel, 2, {});
  require(checked.status == theory::TheoryCheckStatus::Consistent,
          "full model retained old conflict");
  const auto model = f.component->export_model(checked.model.token);
  require(model.facts.size() == f.atoms.size(), "model omitted a retained assignment");
  for (std::size_t i = 0; i < model.facts.size(); ++i) {
    require(
        model.facts[i].atom == f.atoms[i].term && model.facts[i].value == (f.assignment[i + 1] > 0),
        "model exported a stale assignment");
  }
}
}  // namespace

int main() {
  const std::array tests{
      std::pair{"root assignments", &root_assignments_survive},
      std::pair{"retained level", &retained_level_assignments_survive},
      std::pair{"empty levels and repeated backtracks", &empty_levels_and_repeated_backtracks},
      std::pair{"closure facts and conflict explanations",
                &closure_preserves_root_and_retained_facts},
      std::pair{"reassignment and model facts", &discarded_assignments_can_change_polarity}};
  int failures = 0;
  for (const auto& [name, test] : tests) {
    try {
      test();
      std::cout << "PASS: " << name << '\n';
    } catch (const std::exception& error) {
      std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
      ++failures;
    }
  }
  return failures == 0 ? 0 : 1;
}
