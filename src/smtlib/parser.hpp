#pragma once

#include <istream>
#include <optional>
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
  explicit Parser(std::istream& input);
  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  // Single-pass; no token lookahead after the command's outer ')'.
  std::optional<Command> next_command();
  std::vector<Command> parse_commands();

 private:
  Command parse_command(const SExpr& expr) const;
  std::string require_atom(const SExpr& expr, std::string_view context) const;
  std::string require_symbol(const SExpr& expr, std::string_view context) const;

  std::string input_;
  Lexer lexer_;
};

}  // namespace primo::smtlib
