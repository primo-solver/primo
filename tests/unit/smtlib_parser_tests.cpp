#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <string>

#include "smtlib/parser.hpp"

using primo::smtlib::CommandKind;
using primo::smtlib::ParseError;
using primo::smtlib::Parser;
using primo::smtlib::SExpr;

namespace {

template <typename Fn>
void expect_parse_error(Fn fn) {
  try {
    fn();
  } catch (const ParseError&) {
    return;
  }
  assert(false && "expected ParseError");
}

void parses_qf_uf_smoke_shape() {
  const std::string input = R"(
    ; comment before commands
    (set-logic QF_UF)
    (declare-sort A 0)
    (declare-fun f (A A) A)
    (declare-fun a () A)
    (assert (= (f a a) a))
    (check-sat)
  )";

  Parser parser(input);
  const auto commands = parser.parse_commands();
  assert(commands.size() == 6);

  assert(commands[0].kind == CommandKind::SetLogic);
  assert(commands[0].logic == "QF_UF");

  assert(commands[1].kind == CommandKind::DeclareSort);
  assert(commands[1].name == "A");

  assert(commands[2].kind == CommandKind::DeclareFun);
  assert(commands[2].name == "f");
  assert(commands[2].arg_sorts.size() == 2);
  assert(commands[2].result_sort == "A");

  assert(commands[3].kind == CommandKind::DeclareFun);
  assert(commands[3].arg_sorts.empty());

  assert(commands[4].kind == CommandKind::Assert);
  assert(commands[4].assertion.kind == SExpr::Kind::List);
  assert(commands[4].assertion.list[0].is_atom("="));

  assert(commands[5].kind == CommandKind::CheckSat);
}

void parses_nested_boolean_assertion() {
  Parser parser("(assert (and true (not false) (=> true true)))");
  const auto commands = parser.parse_commands();
  assert(commands.size() == 1);
  const auto& assertion = commands[0].assertion;
  assert(assertion.kind == SExpr::Kind::List);
  assert(assertion.list.size() == 4);
  assert(assertion.list[0].is_atom("and"));
}

void parses_benchmark_metadata_and_quoted_symbols() {
  Parser parser(R"(
    (set-info :smt-lib-version 2.6)
    (set-info :source "generated benchmark")
    (set-option :print-success false)
    (set-logic QF_UF)
    (declare-sort U 0)
    (declare-const |+i| U)
    (declare-fun |<=i| (U U) Bool)
    (assert (let ((?x |+i|)) (|<=i| ?x ?x)))
    (check-sat)
    (exit)
  )");
  const auto commands = parser.parse_commands();
  assert(commands.size() == 10);
  assert(commands[0].kind == CommandKind::Ignored);
  assert(commands[1].kind == CommandKind::Ignored);
  assert(commands[2].kind == CommandKind::Ignored);
  assert(commands[5].kind == CommandKind::DeclareFun);
  assert(commands[5].name == "|+i|");
  assert(commands[7].kind == CommandKind::Assert);
  assert(commands[7].assertion.list[0].is_atom("let"));
  assert(commands[9].kind == CommandKind::Ignored);
}

void parses_define_fun_macros() {
  Parser parser(R"(
    (define-fun same ((x A) (y A)) Bool (= x y))
    (define-fun c () A a)
  )");
  const auto commands = parser.parse_commands();
  assert(commands.size() == 2);
  assert(commands[0].kind == CommandKind::DefineFun);
  assert(commands[0].name == "same");
  assert(commands[0].parameters.size() == 2);
  assert(commands[0].parameters[0].name == "x");
  assert(commands[0].parameters[0].sort == "A");
  assert(commands[0].parameters[1].name == "y");
  assert(commands[0].parameters[1].sort == "A");
  assert(commands[0].result_sort == "Bool");
  assert(commands[0].body.kind == SExpr::Kind::List);
  assert(commands[0].body.list[0].is_atom("="));

  assert(commands[1].kind == CommandKind::DefineFun);
  assert(commands[1].name == "c");
  assert(commands[1].parameters.empty());
  assert(commands[1].result_sort == "A");
}

void rejects_malformed_or_unsupported_input() {
  expect_parse_error([] { Parser("(check-sat extra)").parse_commands(); });
  expect_parse_error([] { Parser("(declare-sort A 1)").parse_commands(); });
  expect_parse_error([] { Parser("(define-fun f (x A) Bool true)").parse_commands(); });
  expect_parse_error([] { Parser("(push 1)").parse_commands(); });
  expect_parse_error([] { Parser("(assert (= a b)").parse_commands(); });
}

}  // namespace

int main() {
  parses_qf_uf_smoke_shape();
  parses_nested_boolean_assertion();
  parses_benchmark_metadata_and_quoted_symbols();
  parses_define_fun_macros();
  rejects_malformed_or_unsupported_input();
  return 0;
}
