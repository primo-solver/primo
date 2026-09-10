#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <sstream>
#include <string>
#include <utility>

#include "cli/response_printer.hpp"
#include "smt/check_sat.hpp"
#include "smt/smt_session.hpp"
#include "smtlib/parser.hpp"

using primo::smt::CheckSatResult;
using primo::smt::ResponseKind;
using primo::smt::SmtSession;

namespace {
void accept(SmtSession& session, const std::string& text) {
  const auto response = session.execute(text);
  assert(response.kind == ResponseKind::Success);
}

void expect(SmtSession& session, CheckSatResult result) {
  const auto response = session.execute("(check-sat)");
  assert(response.kind == ResponseKind::CheckSat);
  assert(response.outcome->result == result);
}

void scopes_and_unsafe_simplification() {
  for (const std::string logic : {"QF_LIA", "QF_LRA", "QF_UFLIA", "QF_UFLRA"}) {
    primo::smt::CheckSatOptions options;
    options.check_model = true;
    SmtSession session(options);
    accept(session, "(set-option :produce-models true)");
    accept(session, "(set-logic " + logic + ")");
    const std::string sort = logic.ends_with("LIA") ? "Int" : "Real";
    accept(session, "(declare-const x " + sort + ")");
    accept(session, "(assert (>= x 0))");
    expect(session, CheckSatResult::Sat);
    const auto saved = session.execute("(get-model)").outcome;
    assert(saved && saved->typed_model);
    accept(session, "(push 2)");
    accept(session, "(assert (<= x (- 1)))");
    expect(session, CheckSatResult::Unsat);
    accept(session, "(pop 1)");
    expect(session, CheckSatResult::Sat);
    accept(session, "(declare-const y " + sort + ")");
    accept(session, "(assert (= y (+ x 1)))");
    expect(session, CheckSatResult::Sat);
    accept(session, "(assert (<= y x))");
    expect(session, CheckSatResult::Unsat);
    accept(session, "(pop 1)");
    expect(session, CheckSatResult::Sat);
    // Old models own their signature even after both translation generations die.
    assert(saved->model.find("x") != std::string::npos);
    assert(saved->model.find("y") == std::string::npos);
  }
}

void declarations_definitions_and_side_constraints() {
  SmtSession session;
  accept(session, "(set-logic QF_UF)");
  accept(session, "(declare-const b Bool)");
  accept(session, "(push 1)");
  accept(session, "(declare-sort U 0)");
  accept(session, "(declare-fun f (Bool) Bool)");
  accept(session, "(define-fun d () Bool (f (not b)))");
  accept(session, "(assert d)");
  expect(session, CheckSatResult::Sat);
  accept(session, "(pop 1)");
  accept(session, "(push 1)");
  accept(session, "(declare-sort U 0)");
  accept(session, "(declare-fun f (Bool) Bool)");
  accept(session, "(define-fun d () Bool (f (not b)))");
  accept(session, "(assert (not d))");
  accept(session, "(assert (f (not b)))");
  expect(session, CheckSatResult::Unsat);
  accept(session, "(pop 1)");
  accept(session, "(declare-const d Bool)");
  expect(session, CheckSatResult::Sat);
  accept(session, "(reset-assertions)");
  accept(session, "(declare-sort U 0)");
  accept(session, "(declare-const b U)");
  expect(session, CheckSatResult::Sat);
  accept(session, "(reset)");
  accept(session, "(set-logic QF_LIA)");
  accept(session, "(declare-const b Int)");
  expect(session, CheckSatResult::Sat);
}

void protocol_errors() {
  for (const std::string command :
       {"(pop 1)", "(push -1)", "(push 01)", "(pop)", "(push 184467440737095516160)", "(get-model)",
        "(reset 1)", "(exit 2)", "(set-logic QF_LIA)", "(set-option :produce-models true)"}) {
    SmtSession session;
    accept(session, "(set-logic QF_UF)");
    assert(session.execute(command).kind == ResponseKind::Error);
    assert(session.terminated());
  }
  for (const std::string command : {"(declare-const d Bool)", "(define-fun d () Bool false)"}) {
    SmtSession session;
    accept(session, "(set-logic QF_UF)");
    accept(session, "(define-fun d () Bool true)");
    assert(session.execute(command).kind == ResponseKind::Error);
  }
  SmtSession session;
  assert(session.execute("(set-option :global-declarations true)").kind ==
         ResponseKind::Unsupported);
  accept(session, "(set-option :global-declarations false)");
  accept(session, "(set-option :produce-models true)");
  accept(session, "(set-logic QF_UF)");
  expect(session, CheckSatResult::Sat);
  assert(session.execute("(get-model)").kind == ResponseKind::Model);
  accept(session, "(push 0)");
  accept(session, "(pop 0)");
  assert(session.execute("(get-model)").kind == ResponseKind::Error);
}

void quoting_and_responses() {
  std::istringstream input(R"(; outside
    (set-info :source "a ""quoted"" ); text")
    (set-logic QF_UF)(declare-const |p(;)| Bool)
    (assert |p(;)|)(check-sat)(exit) garbage ignored
  )");
  primo::smtlib::Parser parser(input);
  SmtSession session;
  std::ostringstream output;
  while (!session.terminated()) {
    auto command = parser.next_command();
    assert(command);
    auto response = session.execute(std::move(*command));
    assert(response.kind != ResponseKind::Error);
    primo::cli::print_response(output, response);
  }
  assert(output.str() == "sat\n");
  SmtSession success;
  auto response = success.execute("(set-option :print-success true)");
  assert(response.print_success);
  response = success.execute("(set-option :print-success false)");
  assert(!response.print_success);
}

void one_shot_rejects_multi_query() {
  const auto outcome =
      primo::smt::check_sat_smtlib("(set-logic QF_UF)(check-sat)(assert false)(check-sat)");
  assert(outcome.result == CheckSatResult::Unsupported);
}

void definition_validation_and_portfolio() {
  for (const std::string definition :
       {"(define-fun f ((x Bool)) Bool missing)", "(define-fun f ((x Bool)) Int x)",
        "(define-fun f ((x Bool) (x Bool)) Bool x)"}) {
    SmtSession session;
    accept(session, "(set-logic QF_UFLIA)");
    assert(session.execute(definition).kind == ResponseKind::Error);
  }
  primo::smt::CheckSatOptions options;
  options.experimental_split_bools = 2;
  options.check_model = true;
  options.qf_uf_portfolio = primo::smt::QfUfPortfolioMode::BasicPolarity;
  SmtSession session(options);
  accept(session, "(set-option :produce-models true)");
  accept(session, "(set-logic QF_UF)");
  accept(session, "(declare-const a Bool)");
  accept(session, "(declare-const b Bool)");
  accept(session, "(define-fun f ((x Bool)) Bool (not x))");
  accept(session, "(assert (or a b))");
  expect(session, CheckSatResult::Sat);
  accept(session, "(push 1)");
  accept(session, "(assert (and (f a) (f b)))");
  expect(session, CheckSatResult::Unsat);
  accept(session, "(pop 1)");
  expect(session, CheckSatResult::Sat);
}

void definitions_are_lexical_not_dynamic() {
  SmtSession session;
  accept(session, "(set-logic QF_UF)");
  accept(session, "(declare-const a Bool)");
  accept(session, "(define-fun f ((v Bool)) Bool a)");
  accept(session, "(define-fun g ((a Bool)) Bool (f a))");
  accept(session, "(assert (not a))");
  accept(session, "(push 1)");
  accept(session, "(assert (let ((a true)) (g a)))");
  expect(session, CheckSatResult::Unsat);
  accept(session, "(pop 1)");
  accept(session, "(define-fun h ((a Bool)) Bool (let ((z (f a))) (and (not z) a)))");
  accept(session, "(assert (h true))");
  expect(session, CheckSatResult::Sat);
}
void canonical_names_and_categories() {
  for (const bool reversed : {false, true}) {
    SmtSession session;
    accept(session, "(set-logic QF_UF)");
    const std::string global = reversed ? "|gate|" : "gate";
    const std::string local = reversed ? "gate" : "|gate|";
    accept(session, "(declare-const " + global + " Bool)");
    accept(session, "(assert " + global + ")");
    accept(session, "(assert (let ((" + local + " false)) " + global + "))");
    expect(session, CheckSatResult::Unsat);
  }
  SmtSession session;
  accept(session, "(set-logic QF_UF)");
  accept(session, "(declare-sort |U| 0)");
  accept(session, "(declare-const || U)");
  accept(session, "(declare-fun |let| (U) Bool)");
  accept(session, "(define-fun |same| ((|17| U)) Bool (|let| |17|))");
  accept(session, "(assert (same ||))");
  accept(session, "(push 1)");
  accept(session, "(declare-const |x| Bool)");
  accept(session, "(assert x)");
  expect(session, CheckSatResult::Sat);
  accept(session, "(pop 1)");
  accept(session, "(declare-const x Bool)");
  accept(session, "(assert (not |x|))");
  expect(session, CheckSatResult::Sat);
  accept(session, "(assert (not (|let| ||)))");
  expect(session, CheckSatResult::Unsat);

  SmtSession parallel;
  accept(parallel, "(set-logic QF_UF)");
  accept(parallel, "(declare-const x Bool)");
  accept(parallel, "(assert x)");
  accept(parallel, "(assert (let ((|x| false) (x x)) |x|))");
  expect(parallel, CheckSatResult::Sat);
  accept(parallel, "(assert (let ((x true) (|x| false)) x))");
  expect(parallel, CheckSatResult::Unsat);

  for (const std::string input : {"(declare-const |x| Bool)", "(define-fun |x| () Bool false)"}) {
    SmtSession duplicates;
    accept(duplicates, "(set-logic QF_UF)");
    accept(duplicates, "(declare-const x Bool)");
    assert(duplicates.execute(input).kind == ResponseKind::Error);
  }
  for (const std::string input :
       {"(assert \"false\")", "(assert let)", "(assert (let ((17 true)) true))",
        "(assert (\"and\" true false))"}) {
    SmtSession invalid;
    accept(invalid, "(set-logic QF_UF)");
    assert(invalid.execute(input).kind == ResponseKind::Error);
  }
}

}  // namespace

int main() {
  canonical_names_and_categories();
  scopes_and_unsafe_simplification();
  declarations_definitions_and_side_constraints();
  protocol_errors();
  quoting_and_responses();
  one_shot_rejects_multi_query();
  definition_validation_and_portfolio();
  definitions_are_lexical_not_dynamic();
}
