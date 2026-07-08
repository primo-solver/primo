#include "smtlib/lexer.hpp"

#include <cctype>

namespace primo::smtlib {

Lexer::Lexer(std::string_view input) : input_(input) {}

Token Lexer::next() {
  skip_space_and_comments();
  const std::size_t token_offset = offset_;
  if (at_end()) {
    return Token{TokenKind::End, "", token_offset};
  }

  const char current = advance();
  if (current == '(') {
    return Token{TokenKind::LParen, "(", token_offset};
  }
  if (current == ')') {
    return Token{TokenKind::RParen, ")", token_offset};
  }

  std::string text(1, current);
  while (!at_end()) {
    const char next_char = peek();
    if (std::isspace(static_cast<unsigned char>(next_char)) || next_char == '(' ||
        next_char == ')' || next_char == ';') {
      break;
    }
    text.push_back(advance());
  }
  return Token{TokenKind::Symbol, std::move(text), token_offset};
}

void Lexer::skip_space_and_comments() {
  while (!at_end()) {
    const char current = peek();
    if (std::isspace(static_cast<unsigned char>(current))) {
      advance();
      continue;
    }
    if (current == ';') {
      while (!at_end() && peek() != '\n') {
        advance();
      }
      continue;
    }
    break;
  }
}

bool Lexer::at_end() const { return offset_ >= input_.size(); }

char Lexer::peek() const { return input_[offset_]; }

char Lexer::advance() { return input_[offset_++]; }

}  // namespace primo::smtlib
