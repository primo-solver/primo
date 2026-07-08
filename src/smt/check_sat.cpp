#include "smt/check_sat.hpp"

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
    if (head == "=") {
      require_arity(expr, 3, "=");
      return terms_.mk_eq(build_term(expr.list[1]), build_term(expr.list[2]));
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
    if (terms_.get(term).sort == symbols_.bool_sort()) {
      throw UnsupportedInput("predicate applications require Boolean abstraction");
    }
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

 private:
  static void require_arity(const smtlib::SExpr& expr, std::size_t arity, std::string_view op) {
    if (expr.list.size() != arity) {
      throw UnsupportedInput(std::string(op) + " has wrong arity");
    }
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
  bool saw_check_sat_ = false;
};

class BooleanClausifier {
 public:
  explicit BooleanClausifier(sat::IpasirUpBackend& backend, const core::TermManager& terms)
      : backend_(backend), terms_(terms) {}

  void assert_formula(core::TermId term) {
    const sat::Lit lit = encode(term);
    backend_.add_clause(sat::Clause{lit});
  }

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
      case core::TermKind::Equality:
        throw UnsupportedInput("theory atom reached Boolean-only clausifier");
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
  for (const core::TermId assertion : assertions) {
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

    bool has_equality = false;
    for (const core::TermId assertion : assertions) {
      has_equality = has_equality || contains_equality(frontend.terms(), assertion);
    }
    if (has_equality) {
      return solve_euf_conjunction(frontend.terms(), assertions);
    }

    return solve_boolean_conjunction(frontend.terms(), assertions);
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
