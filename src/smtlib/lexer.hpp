#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace primo::smtlib {

enum class TokenKind {
  LParen,
  RParen,
  Symbol,
  End,
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;
  std::size_t offset = 0;
};

class Lexer {
 public:
  explicit Lexer(std::string_view input);

  Token next();

 private:
  void skip_space_and_comments();
  bool at_end() const;
  char peek() const;
  char advance();

  std::string_view input_;
  std::size_t offset_ = 0;
};

}  // namespace primo::smtlib
