#include "smtlib/parser.hpp"

#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "smtlib/ast.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/lexer.hpp"

namespace primo::smtlib {

namespace {

class TokenSExprParser {
 public:
  explicit TokenSExprParser(std::string_view input) : lexer_(input) { advance(); }

  std::vector<SExpr> parse_all() {
    std::vector<SExpr> expressions;
    while (!at_end()) {
      expressions.push_back(parse_one());
    }
    return expressions;
  }

 private:
  bool at_end() const { return current_.kind == TokenKind::End; }

  void advance() { current_ = lexer_.next(); }

  const Token& current() const {
    if (at_end()) {
      throw ParseError("unexpected end of input", current_.offset);
    }
    return current_;
  }

  SExpr parse_one() {
    const Token token = current();
    if (token.kind == TokenKind::LParen) {
      advance();
      std::vector<SExpr> elements;
      while (!at_end() && current().kind != TokenKind::RParen) {
        elements.push_back(parse_one());
      }
      if (at_end()) {
        throw ParseError("unexpected end of input in list", token.offset);
      }
      advance();
      return SExpr::make_list(std::move(elements));
    }
    if (token.kind == TokenKind::RParen) {
      throw ParseError("unexpected closing parenthesis", token.offset);
    }
    if ((token.text.starts_with('|') && !token.text.ends_with('|')) ||
        (token.text.starts_with('"') && !token.text.ends_with('"'))) {
      throw ParseError("unterminated quoted token", token.offset);
    }
    advance();
    return SExpr::make_atom(token.text);
  }

  Lexer lexer_;
  Token current_;
};

}  // namespace

ParseError::ParseError(std::string message, std::size_t offset)
    : std::runtime_error(std::move(message)), offset_(offset) {}

std::size_t ParseError::offset() const { return offset_; }

Parser::Parser(std::string_view input) : input_(input) {}

std::vector<Command> Parser::parse_commands() {
  std::vector<Command> commands;
  for (const SExpr& expr : parse_sexprs()) {
    commands.push_back(parse_command(expr));
  }
  return commands;
}

std::vector<SExpr> Parser::parse_sexprs() const {
  TokenSExprParser sexprs(input_);
  return sexprs.parse_all();
}

Command Parser::parse_command(const SExpr& expr) const {
  if (expr.kind != SExpr::Kind::List || expr.list.empty()) {
    throw ParseError("command must be a non-empty list", 0);
  }
  const std::string head = require_atom(expr.list[0], "command head");

  if (head == "set-info" || head == "exit") {
    Command command;
    command.kind = CommandKind::Ignored;
    return command;
  }

  if (head == "set-option") {
    if (expr.list.size() != 3) {
      throw ParseError("set-option expects a keyword and a value", 0);
    }
    Command command;
    command.kind = CommandKind::SetOption;
    command.option = require_atom(expr.list[1], "option keyword");
    command.value = require_atom(expr.list[2], "option value");
    return command;
  }

  if (head == "set-logic") {
    if (expr.list.size() != 2) {
      throw ParseError("set-logic expects one argument", 0);
    }
    Command command;
    command.kind = CommandKind::SetLogic;
    command.logic = require_atom(expr.list[1], "logic");
    return command;
  }

  if (head == "declare-sort") {
    if (expr.list.size() != 3 || !expr.list[2].is_atom("0")) {
      throw ParseError("declare-sort expects a name and arity 0", 0);
    }
    Command command;
    command.kind = CommandKind::DeclareSort;
    command.name = require_atom(expr.list[1], "sort name");
    return command;
  }

  if (head == "declare-const") {
    if (expr.list.size() != 3) {
      throw ParseError("declare-const expects name and result sort", 0);
    }
    Command command;
    command.kind = CommandKind::DeclareFun;
    command.name = require_atom(expr.list[1], "constant name");
    command.result_sort = require_atom(expr.list[2], "result sort");
    return command;
  }

  if (head == "declare-fun") {
    if (expr.list.size() != 4 || expr.list[2].kind != SExpr::Kind::List) {
      throw ParseError("declare-fun expects name, argument sort list, and result sort", 0);
    }
    Command command;
    command.kind = CommandKind::DeclareFun;
    command.name = require_atom(expr.list[1], "function name");
    for (const SExpr& arg_sort : expr.list[2].list) {
      command.arg_sorts.push_back(require_atom(arg_sort, "argument sort"));
    }
    command.result_sort = require_atom(expr.list[3], "result sort");
    return command;
  }

  if (head == "define-fun") {
    if (expr.list.size() != 5 || expr.list[2].kind != SExpr::Kind::List) {
      throw ParseError("define-fun expects name, parameter list, result sort, and body", 0);
    }
    Command command;
    command.kind = CommandKind::DefineFun;
    command.name = require_atom(expr.list[1], "function name");
    for (const SExpr& parameter : expr.list[2].list) {
      if (parameter.kind != SExpr::Kind::List || parameter.list.size() != 2) {
        throw ParseError("define-fun parameter must be a sorted variable", 0);
      }
      command.parameters.push_back(SortedVar{require_atom(parameter.list[0], "parameter name"),
                                             require_atom(parameter.list[1], "parameter sort")});
    }
    command.result_sort = require_atom(expr.list[3], "result sort");
    command.body = expr.list[4];
    return command;
  }

  if (head == "assert") {
    if (expr.list.size() != 2) {
      throw ParseError("assert expects one expression", 0);
    }
    Command command;
    command.kind = CommandKind::Assert;
    command.assertion = expr.list[1];
    return command;
  }

  if (head == "check-sat") {
    if (expr.list.size() != 1) {
      throw ParseError("check-sat expects no arguments", 0);
    }
    Command command;
    command.kind = CommandKind::CheckSat;
    return command;
  }

  if (head == "get-model") {
    if (expr.list.size() != 1) {
      throw ParseError("get-model expects no arguments", 0);
    }
    Command command;
    command.kind = CommandKind::GetModel;
    return command;
  }

  throw ParseError("unsupported command: " + head, 0);
}

std::string Parser::require_atom(const SExpr& expr, std::string_view context) const {
  if (expr.kind != SExpr::Kind::Atom) {
    std::ostringstream out;
    out << context << " must be an atom";
    throw ParseError(out.str(), 0);
  }
  return expr.atom;
}

}  // namespace primo::smtlib
