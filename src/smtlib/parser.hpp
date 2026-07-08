#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "smtlib/commands.hpp"
#include "smtlib/lexer.hpp"

namespace primo::smtlib {

class ParseError : public std::runtime_error {
 public:
  ParseError(std::string message, std::size_t offset);

  std::size_t offset() const;

 private:
  std::size_t offset_ = 0;
};

class Parser {
 public:
  explicit Parser(std::string_view input);

  std::vector<Command> parse_commands();

 private:
  SExpr parse_sexpr();
  Command parse_command(const SExpr& expr) const;
  std::string require_atom(const SExpr& expr, std::string_view context) const;
  void advance();
  bool match(TokenKind kind);
  void expect(TokenKind kind, std::string_view context);
  [[noreturn]] void fail(std::string message) const;

  Lexer lexer_;
  Token current_;
};

}  // namespace primo::smtlib
