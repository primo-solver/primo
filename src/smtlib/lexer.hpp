#pragma once

#include <cstddef>
#include <istream>
#include <streambuf>
#include <string>
#include <string_view>

namespace primo::smtlib {

enum class TokenKind {
  LParen,
  RParen,
  Symbol,
  Reserved,
  Numeral,
  Decimal,
  Binary,
  Hexadecimal,
  String,
  Keyword,
  End,
};

// SMT-LIB 2.7 Section 3.1, with the selected comma-in-symbol extension.
bool is_smt_space(char ch);
bool is_reserved_word(std::string_view text);
bool is_simple_symbol(std::string_view text);

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;
  std::size_t offset = 0;
};

class Lexer {
 public:
  explicit Lexer(std::string_view input);
  // The stream and its buffer must outlive the lexer. Construction performs no I/O.
  explicit Lexer(std::istream& input);

  Token next();

 private:
  void skip_space_and_comments();
  bool at_end();
  char peek();
  char advance();

  std::string_view input_;
  std::streambuf* buffer_ = nullptr;
  std::size_t offset_ = 0;
};

}  // namespace primo::smtlib
