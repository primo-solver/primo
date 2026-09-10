#include "smtlib/lexer.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "smtlib/parser.hpp"

namespace primo::smtlib {
namespace {
bool digit(char ch) { return ch >= '0' && ch <= '9'; }
bool letter(char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); }
bool symbol_char(char ch) {
  return letter(ch) || digit(ch) ||
         std::string_view("~!@$%^&*_-+=<>.?/,").find(ch) != std::string_view::npos;
}
bool symbol_body(std::string_view text) {
  return !text.empty() && !digit(text.front()) && std::ranges::all_of(text, symbol_char);
}
}  // namespace

bool is_smt_space(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; }

bool is_reserved_word(std::string_view text) {
  // Section 3.1 plus Section 3.11 command vocabulary, shared with printing.
  static constexpr std::string_view words[] = {"!",
                                               "BINARY",
                                               "DECIMAL",
                                               "HEXADECIMAL",
                                               "NUMERAL",
                                               "STRING",
                                               "_",
                                               "as",
                                               "assert",
                                               "check-sat",
                                               "check-sat-assuming",
                                               "declare-const",
                                               "declare-datatype",
                                               "declare-datatypes",
                                               "declare-fun",
                                               "declare-sort",
                                               "declare-sort-parameter",
                                               "define-fun",
                                               "define-fun-rec",
                                               "define-funs-rec",
                                               "define-sort",
                                               "echo",
                                               "exists",
                                               "exit",
                                               "forall",
                                               "get-assertions",
                                               "get-assignment",
                                               "get-info",
                                               "get-model",
                                               "get-option",
                                               "get-proof",
                                               "get-unsat-assumptions",
                                               "get-unsat-core",
                                               "get-value",
                                               "lambda",
                                               "let",
                                               "match",
                                               "par",
                                               "pop",
                                               "push",
                                               "reset",
                                               "reset-assertions",
                                               "set-info",
                                               "set-logic",
                                               "set-option"};
  return std::ranges::binary_search(words, text);
}

bool is_simple_symbol(std::string_view text) {
  return symbol_body(text) && !is_reserved_word(text);
}

Lexer::Lexer(std::string_view input) : input_(input) {}

Token Lexer::next() {
  skip_space_and_comments();
  const std::size_t start = offset_;
  if (at_end()) return {TokenKind::End, "", start};
  const char current = advance();
  if (current == '(') return {TokenKind::LParen, "(", start};
  if (current == ')') return {TokenKind::RParen, ")", start};
  if (current == '|' || current == '"') {
    while (!at_end()) {
      if (advance() != current) continue;
      if (current == '"' && !at_end() && peek() == '"') {
        advance();
        continue;
      }
      // Selected compatibility policy: backslash/control bytes in quoted names
      // are literal; a backslash never escapes a closing pipe.
      if (current == '|')
        return {TokenKind::Symbol, std::string(input_.substr(start + 1, offset_ - start - 2)),
                start};
      return {TokenKind::String, std::string(input_.substr(start, offset_ - start)), start};
    }
    throw ParseError("unterminated quoted token", start);
  }
  while (!at_end()) {
    const char ch = peek();
    if (is_smt_space(ch) || ch == '(' || ch == ')' || ch == ';' || ch == '|' || ch == '"') break;
    advance();
  }
  std::string text(input_.substr(start, offset_ - start));
  const std::string_view view(text);
  TokenKind kind = TokenKind::Symbol;
  if (digit(current)) {
    const auto dot = view.find('.');
    const auto integer = view.substr(0, dot);
    if (!std::ranges::all_of(integer, digit) || (integer.size() > 1 && integer.front() == '0'))
      throw ParseError("invalid numeral (leading zeroes are not allowed)", start);
    kind = TokenKind::Numeral;
    if (dot != std::string_view::npos) {
      const auto fraction = view.substr(dot + 1);
      if (fraction.empty() || !std::ranges::all_of(fraction, digit))
        throw ParseError("invalid decimal", start);
      kind = TokenKind::Decimal;
    }
  } else if (current == '#') {
    if (view.size() < 3 || (view[1] != 'b' && view[1] != 'x'))
      throw ParseError("invalid binary or hexadecimal literal", start);
    const bool binary = view[1] == 'b';
    for (const char ch : view.substr(2)) {
      if (binary ? ch != '0' && ch != '1'
                 : !digit(ch) && !(ch >= 'a' && ch <= 'f') && !(ch >= 'A' && ch <= 'F'))
        throw ParseError("invalid binary or hexadecimal digit", start);
    }
    kind = binary ? TokenKind::Binary : TokenKind::Hexadecimal;
  } else if (current == ':') {
    if (!symbol_body(view.substr(1))) throw ParseError("invalid keyword", start);
    kind = TokenKind::Keyword;
  } else {
    if (!symbol_body(view)) throw ParseError("invalid symbol character", start);
    if (is_reserved_word(view)) kind = TokenKind::Reserved;
  }
  return {kind, std::move(text), start};
}

void Lexer::skip_space_and_comments() {
  while (!at_end()) {
    if (is_smt_space(peek())) {
      advance();
    } else if (peek() == ';') {
      while (!at_end() && peek() != '\n' && peek() != '\r') advance();
    } else {
      break;
    }
  }
}

bool Lexer::at_end() const { return offset_ >= input_.size(); }
char Lexer::peek() const { return input_[offset_]; }
char Lexer::advance() { return input_[offset_++]; }
}  // namespace primo::smtlib
