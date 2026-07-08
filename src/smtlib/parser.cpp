#include "smtlib/parser.hpp"

#include <sstream>
#include <utility>

namespace primo::smtlib {

ParseError::ParseError(std::string message, std::size_t offset)
    : std::runtime_error(std::move(message)), offset_(offset) {}

std::size_t ParseError::offset() const { return offset_; }

Parser::Parser(std::string_view input) : lexer_(input) { advance(); }

std::vector<Command> Parser::parse_commands() {
  std::vector<Command> commands;
  while (current_.kind != TokenKind::End) {
    commands.push_back(parse_command(parse_sexpr()));
  }
  return commands;
}

SExpr Parser::parse_sexpr() {
  if (current_.kind == TokenKind::Symbol) {
    std::string atom = current_.text;
    advance();
    return SExpr::make_atom(std::move(atom));
  }

  expect(TokenKind::LParen, "S-expression");
  std::vector<SExpr> elements;
  while (current_.kind != TokenKind::RParen) {
    if (current_.kind == TokenKind::End) {
      fail("unexpected end of input in list");
    }
    elements.push_back(parse_sexpr());
  }
  expect(TokenKind::RParen, "list");
  return SExpr::make_list(std::move(elements));
}

Command Parser::parse_command(const SExpr& expr) const {
  if (expr.kind != SExpr::Kind::List || expr.list.empty()) {
    throw ParseError("command must be a non-empty list", 0);
  }
  const std::string head = require_atom(expr.list[0], "command head");

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
    return Command{CommandKind::CheckSat};
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

void Parser::advance() { current_ = lexer_.next(); }

bool Parser::match(TokenKind kind) {
  if (current_.kind != kind) {
    return false;
  }
  advance();
  return true;
}

void Parser::expect(TokenKind kind, std::string_view context) {
  if (!match(kind)) {
    std::ostringstream out;
    out << "expected token while parsing " << context;
    fail(out.str());
  }
}

[[noreturn]] void Parser::fail(std::string message) const {
  throw ParseError(std::move(message), current_.offset);
}

}  // namespace primo::smtlib
