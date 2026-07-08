#include "smt/check_sat.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "sat/cadical_backend.hpp"
#include "sat/clause.hpp"
#include "sat/ipasir_up.hpp"
#include "sat/lit.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/parser.hpp"
#include "theory/equality/euf_solver.hpp"

namespace primo::smt {

namespace {

class UnsupportedInput : public std::runtime_error {
 public:
  explicit UnsupportedInput(const std::string& message) : std::runtime_error(message) {}
};

class Frontend {
 public:
  core::TermId build_term(const smtlib::SExpr& expr) {
    if (expr.kind == smtlib::SExpr::Kind::Atom) {
      if (expr.atom == "true") {
        return terms_.mk_bool(true);
      }
      if (expr.atom == "false") {
        return terms_.mk_bool(false);
      }
      const auto bound = lookup_bound_term(expr.atom);
      if (bound.has_value()) {
        return bound.value();
      }
      const auto symbol = symbols_.lookup_symbol(expr.atom);
      if (!symbol.has_value()) {
        throw UnsupportedInput("unknown symbol: " + expr.atom);
      }
      return terms_.mk_constant(symbol.value());
    }

    if (expr.list.empty() || expr.list[0].kind != smtlib::SExpr::Kind::Atom) {
      throw UnsupportedInput("unsupported expression shape");
    }

    const std::string& head = expr.list[0].atom;
    if (head == "not") {
      require_arity(expr, 2, "not");
      return terms_.mk_not(build_term(expr.list[1]));
    }
    if (head == "and") {
      std::vector<core::TermId> children;
      for (std::size_t i = 1; i < expr.list.size(); ++i) {
        children.push_back(build_term(expr.list[i]));
      }
      return terms_.mk_and(std::move(children));
    }
    if (head == "or") {
      std::vector<core::TermId> children;
      for (std::size_t i = 1; i < expr.list.size(); ++i) {
        children.push_back(build_term(expr.list[i]));
      }
      return terms_.mk_or(std::move(children));
    }
    if (head == "=>") {
      require_arity(expr, 3, "=>");
      return terms_.mk_implies(build_term(expr.list[1]), build_term(expr.list[2]));
    }
    if (head == "let") {
      return build_let(expr);
    }
    if (head == "=") {
      require_arity(expr, 3, "=");
      return terms_.mk_eq(build_term(expr.list[1]), build_term(expr.list[2]));
    }
    if (head == "distinct") {
      return build_distinct(expr);
    }

    const auto symbol = symbols_.lookup_symbol(head);
    if (!symbol.has_value()) {
      throw UnsupportedInput("unknown function symbol: " + head);
    }
    std::vector<core::TermId> args;
    for (std::size_t i = 1; i < expr.list.size(); ++i) {
      args.push_back(build_term(expr.list[i]));
    }
    const core::TermId term = terms_.mk_app(symbol.value(), std::move(args));
    return term;
  }

  void process_command(const smtlib::Command& command, std::vector<core::TermId>& assertions) {
    switch (command.kind) {
      case smtlib::CommandKind::Ignored:
        return;
      case smtlib::CommandKind::SetLogic:
        if (command.logic != "QF_UF") {
          throw UnsupportedInput("unsupported logic: " + command.logic);
        }
        return;
      case smtlib::CommandKind::DeclareSort:
        symbols_.declare_sort(command.name);
        return;
      case smtlib::CommandKind::DeclareFun:
        declare_fun(command);
        return;
      case smtlib::CommandKind::Assert:
        assertions.push_back(build_term(command.assertion));
        return;
      case smtlib::CommandKind::CheckSat:
        saw_check_sat_ = true;
        return;
    }
  }

  bool saw_check_sat() const { return saw_check_sat_; }
  const core::SymbolTable& symbols() const { return symbols_; }
  const core::TermManager& terms() const { return terms_; }
  core::TermManager& terms() { return terms_; }

 private:
  std::optional<core::TermId> lookup_bound_term(const std::string& name) const {
    for (auto scope = let_scopes_.rbegin(); scope != let_scopes_.rend(); ++scope) {
      const auto found = scope->find(name);
      if (found != scope->end()) {
        return found->second;
      }
    }
    return std::nullopt;
  }

  static void require_arity(const smtlib::SExpr& expr, std::size_t arity, std::string_view op) {
    if (expr.list.size() != arity) {
      throw UnsupportedInput(std::string(op) + " has wrong arity");
    }
  }

  core::TermId build_let(const smtlib::SExpr& expr) {
    require_arity(expr, 3, "let");
    if (expr.list[1].kind != smtlib::SExpr::Kind::List) {
      throw UnsupportedInput("let bindings must be a list");
    }

    std::unordered_map<std::string, core::TermId> scope;
    for (const smtlib::SExpr& binding : expr.list[1].list) {
      if (binding.kind != smtlib::SExpr::Kind::List || binding.list.size() != 2 ||
          binding.list[0].kind != smtlib::SExpr::Kind::Atom) {
        throw UnsupportedInput("malformed let binding");
      }
      const std::string& name = binding.list[0].atom;
      if (!scope.emplace(name, build_term(binding.list[1])).second) {
        throw UnsupportedInput("duplicate let binding: " + name);
      }
    }

    let_scopes_.push_back(std::move(scope));
    const core::TermId body = build_term(expr.list[2]);
    let_scopes_.pop_back();
    return body;
  }

  core::TermId build_distinct(const smtlib::SExpr& expr) {
    std::vector<core::TermId> terms;
    for (std::size_t i = 1; i < expr.list.size(); ++i) {
      terms.push_back(build_term(expr.list[i]));
    }
    std::vector<core::TermId> disequalities;
    for (std::size_t i = 0; i < terms.size(); ++i) {
      for (std::size_t j = i + 1; j < terms.size(); ++j) {
        disequalities.push_back(terms_.mk_not(terms_.mk_eq(terms[i], terms[j])));
      }
    }
    if (disequalities.empty()) {
      return terms_.mk_bool(true);
    }
    if (disequalities.size() == 1) {
      return disequalities.front();
    }
    return terms_.mk_and(std::move(disequalities));
  }

  core::SortId lookup_sort_name(const std::string& name) const {
    const auto sort = symbols_.lookup_sort(name);
    if (!sort.has_value()) {
      throw UnsupportedInput("unknown sort: " + name);
    }
    return sort.value();
  }

  void declare_fun(const smtlib::Command& command) {
    std::vector<core::SortId> arg_sorts;
    for (const std::string& name : command.arg_sorts) {
      arg_sorts.push_back(lookup_sort_name(name));
    }
    const core::SortId result_sort = lookup_sort_name(command.result_sort);
    if (arg_sorts.empty()) {
      symbols_.declare_constant(command.name, result_sort);
    } else if (result_sort == symbols_.bool_sort()) {
      symbols_.declare_predicate(command.name, std::move(arg_sorts));
    } else {
      symbols_.declare_function(command.name, std::move(arg_sorts), result_sort);
    }
  }

  core::SymbolTable symbols_;
  core::TermManager terms_{symbols_};
  std::vector<std::unordered_map<std::string, core::TermId>> let_scopes_;
  bool saw_check_sat_ = false;
};

class BooleanClausifier {
 public:
  struct TheoryAtom {
    sat::Lit lit;
    core::TermId term;
  };

  explicit BooleanClausifier(sat::IpasirUpBackend& backend, const core::TermManager& terms)
      : backend_(backend), terms_(terms) {}

  void assert_formula(core::TermId term) {
    if (terms_.get(term).sort != terms_.symbols().bool_sort()) {
      throw UnsupportedInput("assertion is not Boolean");
    }
    const sat::Lit lit = encode(term);
    backend_.add_clause(sat::Clause{lit});
  }

  const std::vector<TheoryAtom>& theory_atoms() const { return theory_atoms_; }

 private:
  sat::Lit encode(core::TermId term_id) {
    const auto existing = lit_by_term_.find(term_id);
    if (existing != lit_by_term_.end()) {
      return existing->second;
    }

    const core::Term& term = terms_.get(term_id);
    if (term.kind == core::TermKind::BoolValue) {
      return term.bool_value ? true_lit() : negate(true_lit());
    }

    if (term.kind == core::TermKind::Constant) {
      return atom_lit(term_id);
    }

    if (term.kind == core::TermKind::Not) {
      const sat::Lit lit = negate(encode(term.children.at(0)));
      lit_by_term_.emplace(term_id, lit);
      return lit;
    }

    const sat::Lit lit{backend_.new_var()};
    lit_by_term_.emplace(term_id, lit);

    switch (term.kind) {
      case core::TermKind::And:
        encode_and(lit, term.children);
        return lit;
      case core::TermKind::Or:
        encode_or(lit, term.children);
        return lit;
      case core::TermKind::Implies:
        encode_implies(lit, encode(term.children.at(0)), encode(term.children.at(1)));
        return lit;
      case core::TermKind::Application:
        if (term.sort != terms_.symbols().bool_sort()) {
          throw UnsupportedInput("non-Boolean function application in Boolean context");
        }
        theory_atoms_.push_back(TheoryAtom{lit, term_id});
        return lit;
      case core::TermKind::Equality:
        theory_atoms_.push_back(TheoryAtom{lit, term_id});
        return lit;
      case core::TermKind::BoolValue:
      case core::TermKind::Constant:
      case core::TermKind::Not:
        break;
    }
    throw UnsupportedInput("unsupported Boolean term");
  }

  sat::Lit atom_lit(core::TermId term_id) {
    const auto existing = lit_by_term_.find(term_id);
    if (existing != lit_by_term_.end()) {
      return existing->second;
    }
    const sat::Lit lit{backend_.new_var()};
    lit_by_term_.emplace(term_id, lit);
    return lit;
  }

  sat::Lit true_lit() {
    if (true_lit_.value == 0) {
      true_lit_ = sat::Lit{backend_.new_var()};
      backend_.add_clause(sat::Clause{true_lit_});
    }
    return true_lit_;
  }

  static sat::Lit negate(sat::Lit lit) { return sat::Lit{-lit.value}; }

  void encode_and(sat::Lit output, const std::vector<core::TermId>& children) {
    sat::Clause reverse_clause{output};
    for (const core::TermId child : children) {
      const sat::Lit child_lit = encode(child);
      backend_.add_clause(sat::Clause{negate(output), child_lit});
      reverse_clause.push_back(negate(child_lit));
    }
    backend_.add_clause(reverse_clause);
  }

  void encode_or(sat::Lit output, const std::vector<core::TermId>& children) {
    sat::Clause forward_clause{negate(output)};
    for (const core::TermId child : children) {
      const sat::Lit child_lit = encode(child);
      backend_.add_clause(sat::Clause{negate(child_lit), output});
      forward_clause.push_back(child_lit);
    }
    backend_.add_clause(forward_clause);
  }

  void encode_implies(sat::Lit output, sat::Lit lhs, sat::Lit rhs) {
    backend_.add_clause(sat::Clause{lhs, output});
    backend_.add_clause(sat::Clause{negate(rhs), output});
    backend_.add_clause(sat::Clause{negate(output), negate(lhs), rhs});
  }

  sat::IpasirUpBackend& backend_;
  const core::TermManager& terms_;
  std::unordered_map<core::TermId, sat::Lit> lit_by_term_;
  std::vector<TheoryAtom> theory_atoms_;
  sat::Lit true_lit_{0};
};

CheckSatResult map_sat_result(sat::SatResult result) {
  switch (result) {
    case sat::SatResult::Sat:
      return CheckSatResult::Sat;
    case sat::SatResult::Unsat:
      return CheckSatResult::Unsat;
    case sat::SatResult::Unknown:
      return CheckSatResult::Unknown;
    case sat::SatResult::Unsupported:
      return CheckSatResult::Unsupported;
  }
  return CheckSatResult::Unknown;
}

bool is_euf_literal(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::BoolValue) {
    return true;
  }
  if (term.kind == core::TermKind::Equality) {
    return true;
  }
  if (term.kind == core::TermKind::Not) {
    return terms.get(term.children.at(0)).kind == core::TermKind::Equality;
  }
  return false;
}

void collect_conjuncts(const core::TermManager& terms, core::TermId term_id,
                       std::vector<core::TermId>& conjuncts) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::And) {
    for (const core::TermId child : term.children) {
      collect_conjuncts(terms, child, conjuncts);
    }
    return;
  }
  conjuncts.push_back(term_id);
}

bool contains_equality(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Equality) {
    return true;
  }
  for (const core::TermId child : term.children) {
    if (contains_equality(terms, child)) {
      return true;
    }
  }
  return false;
}

CheckSatOutcome solve_euf_conjunction(const core::TermManager& terms,
                                      const std::vector<core::TermId>& assertions) {
  theory::equality::EufSolver solver(terms);
  std::vector<core::TermId> conjuncts;
  for (const core::TermId assertion : assertions) {
    collect_conjuncts(terms, assertion, conjuncts);
  }

  for (const core::TermId assertion : conjuncts) {
    if (!is_euf_literal(terms, assertion)) {
      return CheckSatOutcome{CheckSatResult::Unsupported,
                             "Boolean abstraction for theory atoms is not implemented yet"};
    }

    const core::Term& term = terms.get(assertion);
    if (term.kind == core::TermKind::BoolValue) {
      if (!term.bool_value) {
        return CheckSatOutcome{CheckSatResult::Unsat, ""};
      }
      continue;
    }
    if (term.kind == core::TermKind::Equality) {
      solver.assert_equal(term.children.at(0), term.children.at(1));
      continue;
    }

    const core::Term& equality = terms.get(term.children.at(0));
    solver.assert_distinct(equality.children.at(0), equality.children.at(1));
  }

  return CheckSatOutcome{solver.is_satisfiable() ? CheckSatResult::Sat : CheckSatResult::Unsat, ""};
}

CheckSatOutcome solve_boolean_conjunction(const core::TermManager& terms,
                                          const std::vector<core::TermId>& assertions) {
  sat::CadicalBackend backend;
  BooleanClausifier clausifier(backend, terms);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }

  return CheckSatOutcome{map_sat_result(backend.solve()), ""};
}

void assert_theory_atom(theory::equality::EufSolver& solver, const core::TermManager& terms,
                        core::TermId atom, bool value, core::TermId true_term,
                        core::TermId false_term) {
  const core::Term& term = terms.get(atom);
  if (term.kind == core::TermKind::Equality) {
    if (value) {
      solver.assert_equal(term.children.at(0), term.children.at(1));
    } else {
      solver.assert_distinct(term.children.at(0), term.children.at(1));
    }
    return;
  }
  if (term.kind == core::TermKind::Application && term.sort == terms.symbols().bool_sort()) {
    solver.assert_equal(atom, value ? true_term : false_term);
    return;
  }
  throw UnsupportedInput("unsupported theory atom");
}

CheckSatOutcome solve_qf_uf(core::TermManager& terms, const std::vector<core::TermId>& assertions) {
  sat::CadicalBackend backend;
  BooleanClausifier clausifier(backend, terms);
  for (const core::TermId assertion : assertions) {
    clausifier.assert_formula(assertion);
  }

  const core::TermId true_term = terms.mk_bool(true);
  const core::TermId false_term = terms.mk_bool(false);

  while (true) {
    const sat::SatResult sat_result = backend.solve();
    if (sat_result != sat::SatResult::Sat) {
      return CheckSatOutcome{map_sat_result(sat_result), ""};
    }

    theory::equality::EufSolver solver(terms);
    solver.assert_distinct(true_term, false_term);
    sat::Clause conflict_clause;
    for (const BooleanClausifier::TheoryAtom& atom : clausifier.theory_atoms()) {
      const int var = atom.lit.value > 0 ? atom.lit.value : -atom.lit.value;
      const std::optional<bool> assignment = backend.value(var);
      if (!assignment.has_value()) {
        return CheckSatOutcome{CheckSatResult::Unknown, "SAT backend did not assign theory atom"};
      }
      assert_theory_atom(solver, terms, atom.term, assignment.value(), true_term, false_term);
      conflict_clause.push_back(assignment.value() ? sat::Lit{-var} : sat::Lit{var});
    }

    if (solver.is_satisfiable()) {
      return CheckSatOutcome{CheckSatResult::Sat, ""};
    }
    if (conflict_clause.empty()) {
      return CheckSatOutcome{CheckSatResult::Unsat, ""};
    }
    backend.add_clause(conflict_clause);
  }
}

}  // namespace

CheckSatOutcome check_sat_smtlib(std::string_view input) {
  try {
    smtlib::Parser parser(input);
    const std::vector<smtlib::Command> commands = parser.parse_commands();

    Frontend frontend;
    std::vector<core::TermId> assertions;
    for (const smtlib::Command& command : commands) {
      frontend.process_command(command, assertions);
    }
    if (!frontend.saw_check_sat()) {
      return CheckSatOutcome{CheckSatResult::Unsupported, "missing check-sat command"};
    }

    const bool has_equality = [&] {
      for (const core::TermId assertion : assertions) {
        if (contains_equality(frontend.terms(), assertion)) {
          return true;
        }
      }
      return false;
    }();
    if (has_equality) {
      return solve_qf_uf(frontend.terms(), assertions);
    }
    try {
      return solve_boolean_conjunction(frontend.terms(), assertions);
    } catch (const UnsupportedInput&) {
      return solve_qf_uf(frontend.terms(), assertions);
    }
  } catch (const UnsupportedInput& error) {
    return CheckSatOutcome{CheckSatResult::Unsupported, error.what()};
  } catch (const smtlib::ParseError& error) {
    return CheckSatOutcome{CheckSatResult::Unsupported, error.what()};
  } catch (const std::exception& error) {
    return CheckSatOutcome{CheckSatResult::Unsupported, error.what()};
  }
}

std::string_view to_string(CheckSatResult result) {
  switch (result) {
    case CheckSatResult::Sat:
      return "sat";
    case CheckSatResult::Unsat:
      return "unsat";
    case CheckSatResult::Unknown:
      return "unknown";
    case CheckSatResult::Unsupported:
      return "unsupported";
  }
  return "unknown";
}

}  // namespace primo::smt
