#include "smtlib/parser.hpp"

#include <antlr4-runtime.h>

#include <sstream>
#include <utility>

#include "smtlib/antlr/SMTLIBv2Lexer.h"
#include "smtlib/antlr/SMTLIBv2Parser.h"

namespace primo::smtlib {

namespace {

class ThrowingErrorListener final : public antlr4::BaseErrorListener {
 public:
  void syntaxError(antlr4::Recognizer* /*recognizer*/, antlr4::Token* offendingSymbol,
                   std::size_t /*line*/, std::size_t charPositionInLine, const std::string& msg,
                   std::exception_ptr /*exception*/) override {
    const std::size_t offset =
        offendingSymbol == nullptr ? charPositionInLine : offendingSymbol->getStartIndex();
    throw ParseError(msg, offset);
  }
};

class TokenSExprParser {
 public:
  explicit TokenSExprParser(const std::vector<antlr4::Token*>& tokens) : tokens_(tokens) {}

  std::vector<SExpr> parse_all() {
    std::vector<SExpr> expressions;
    while (!at_end()) {
      expressions.push_back(parse_one());
    }
    return expressions;
  }

 private:
  bool at_end() const {
    return cursor_ >= tokens_.size() || tokens_[cursor_]->getType() == antlr4::Token::EOF;
  }

  const antlr4::Token& current() const {
    if (at_end()) {
      throw ParseError("unexpected end of input", 0);
    }
    return *tokens_[cursor_];
  }

  SExpr parse_one() {
    const antlr4::Token& token = current();
    if (token.getType() == SMTLIBv2Lexer::ParOpen) {
      ++cursor_;
      std::vector<SExpr> elements;
      while (!at_end() && current().getType() != SMTLIBv2Lexer::ParClose) {
        elements.push_back(parse_one());
      }
      if (at_end()) {
        throw ParseError("unexpected end of input in list", token.getStartIndex());
      }
      ++cursor_;
      return SExpr::make_list(std::move(elements));
    }
    if (token.getType() == SMTLIBv2Lexer::ParClose) {
      throw ParseError("unexpected closing parenthesis", token.getStartIndex());
    }
    ++cursor_;
    return SExpr::make_atom(token.getText());
  }

  const std::vector<antlr4::Token*>& tokens_;
  std::size_t cursor_ = 0;
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
  antlr4::ANTLRInputStream input(input_);
  SMTLIBv2Lexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  SMTLIBv2Parser parser(&tokens);
  ThrowingErrorListener errors;
  lexer.removeErrorListeners();
  parser.removeErrorListeners();
  lexer.addErrorListener(&errors);
  parser.addErrorListener(&errors);

  parser.start_();
  if (parser.getNumberOfSyntaxErrors() != 0) {
    throw ParseError("SMT-LIB parse failed", 0);
  }

  tokens.fill();
  const std::vector<antlr4::Token*> token_vector = tokens.getTokens();
  TokenSExprParser sexprs(token_vector);
  return sexprs.parse_all();
}

Command Parser::parse_command(const SExpr& expr) const {
  if (expr.kind != SExpr::Kind::List || expr.list.empty()) {
    throw ParseError("command must be a non-empty list", 0);
  }
  const std::string head = require_atom(expr.list[0], "command head");

  if (head == "set-info" || head == "set-option" || head == "exit") {
    Command command;
    command.kind = CommandKind::Ignored;
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
