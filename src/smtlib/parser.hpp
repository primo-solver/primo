#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "smtlib/commands.hpp"

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
  std::vector<SExpr> parse_sexprs() const;
  Command parse_command(const SExpr& expr) const;
  std::string require_atom(const SExpr& expr, std::string_view context) const;

  std::string input_;
};

}  // namespace primo::smtlib
