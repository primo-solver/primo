#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <string>

#include "smtlib/ast.hpp"
#include "smtlib/commands.hpp"
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
    (set-info :source |
      generated benchmark
    |)
    (set-info :category "crafted")
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
  assert(commands.size() == 11);
  assert(commands[0].kind == CommandKind::Ignored);
  assert(commands[1].kind == CommandKind::Ignored);
  assert(commands[2].kind == CommandKind::Ignored);
  assert(commands[3].kind == CommandKind::SetOption);
  assert(commands[3].option == ":print-success");
  assert(commands[3].value == "false");
  assert(commands[6].kind == CommandKind::DeclareFun);
  assert(commands[6].name == "|+i|");
  assert(commands[8].kind == CommandKind::Assert);
  assert(commands[8].assertion.list[0].is_atom("let"));
  assert(commands[10].kind == CommandKind::Ignored);
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

void parses_model_commands() {
  Parser parser(R"(
    (set-option :produce-models true)
    (get-model)
  )");
  const auto commands = parser.parse_commands();
  assert(commands.size() == 2);
  assert(commands[0].kind == CommandKind::SetOption);
  assert(commands[0].option == ":produce-models");
  assert(commands[0].value == "true");
  assert(commands[1].kind == CommandKind::GetModel);
}

// SExpr parsing, copying, and destruction are all iterative (explicit
// stack), not recursive descent -- see ast.cpp/parser.cpp's class/function
// comments. Deeply nested input like this used to stack-overflow (found
// via fuzzing, since fuzzers routinely generate deep `let`/arithmetic
// chains); this is a regression guard well beyond any depth a native
// recursive implementation could tolerate.
void parses_and_copies_pathologically_deep_nesting() {
  constexpr int kDepth = 200000;
  std::string input = "(assert ";
  input.append(kDepth, '(');
  input += "a";
  input.append(kDepth, ')');
  input += ")";

  Parser parser(input);
  const auto commands = parser.parse_commands();
  assert(commands.size() == 1);

  // Force a real copy (not just a move) of the deeply nested tree, to
  // exercise SExpr's iterative copy constructor specifically.
  const SExpr copy = commands.front().assertion;
  assert(copy.kind == SExpr::Kind::List);

  // Both the original Command and this copy go out of scope at the end of
  // this function, exercising the iterative destructor on two separate
  // deep trees.
}

// SMT-LIB 2.6: (set-option <option>), <option> ::= <attribute>, and
// <attribute> ::= <keyword> | <keyword> <attribute_value>, where
// <attribute_value> ::= <spec_constant> | <symbol> | ( <s_expr>* ).
// So a bare keyword is legal and the value may be a list. Parsing set-option
// as strictly "keyword plus atom" rejected conforming scripts such as
// (set-option :verbosity (- 1)), which z3 accepts. Only :produce-models is
// acted on; the rest are ignored, so a list value is recorded as empty.
void parses_standard_set_option_forms() {
  Parser parser(R"(
    (set-option :some-flag)
    (set-option :verbosity (- 1))
    (set-option :opts (a (b c) d))
    (set-option :random-seed 5)
  )");
  const auto commands = parser.parse_commands();
  assert(commands.size() == 4);
  for (const auto& command : commands) {
    assert(command.kind == CommandKind::SetOption);
  }
  assert(commands[0].option == ":some-flag");
  assert(commands[0].value.empty());
  assert(commands[1].option == ":verbosity");
  assert(commands[1].value.empty());  // list value, not acted on
  assert(commands[2].option == ":opts");
  assert(commands[3].option == ":random-seed");
  assert(commands[3].value == "5");
}

void rejects_malformed_or_unsupported_input() {
  // Two options in one command is not legal SMT-LIB -- <option> is a single
  // <attribute>. z3 tolerates it; being stricter here is the standard.
  expect_parse_error([] { Parser("(set-option :a 1 :b 2)").parse_commands(); });
  expect_parse_error([] { Parser("(set-option)").parse_commands(); });
  expect_parse_error([] { Parser("(check-sat extra)").parse_commands(); });
  expect_parse_error([] { Parser("(get-model extra)").parse_commands(); });
  expect_parse_error([] { Parser("(declare-sort A 1)").parse_commands(); });
  expect_parse_error([] { Parser("(define-fun f (x A) Bool true)").parse_commands(); });
  expect_parse_error([] { Parser("(push 1)").parse_commands(); });
  expect_parse_error([] { Parser("(assert (= a b)").parse_commands(); });
  expect_parse_error([] { Parser("(set-info :source |unterminated)").parse_commands(); });
  expect_parse_error([] { Parser("(set-info :source \"unterminated)").parse_commands(); });
}

}  // namespace

int main() {
  parses_qf_uf_smoke_shape();
  parses_nested_boolean_assertion();
  parses_benchmark_metadata_and_quoted_symbols();
  parses_define_fun_macros();
  parses_and_copies_pathologically_deep_nesting();
  parses_model_commands();
  parses_standard_set_option_forms();
  rejects_malformed_or_unsupported_input();
  return 0;
}
