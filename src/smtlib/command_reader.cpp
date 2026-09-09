#include "smtlib/command_reader.hpp"

#include <cctype>
#include <cstddef>
#include <optional>
#include <streambuf>
#include <string>

#include "smtlib/parser.hpp"

namespace primo::smtlib {

std::optional<std::string> CommandReader::next() {
  std::string text;
  std::size_t depth = 0;
  bool comment = false;
  char quote = 0;
  auto& buffer = *input_.rdbuf();
  for (;;) {
    const auto value = buffer.sbumpc();
    if (std::char_traits<char>::eq_int_type(value, std::char_traits<char>::eof())) {
      if (depth != 0) throw ParseError("unexpected end of input in command", text.size());
      return std::nullopt;
    }
    const char c = std::char_traits<char>::to_char_type(value);
    if (depth != 0) text.push_back(c);
    if (comment) {
      if (c == '\n' || c == '\r') comment = false;
      continue;
    }
    if (quote != 0) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == ';') {
      comment = true;
      continue;
    }
    if (depth == 0) {
      if (std::isspace(static_cast<unsigned char>(c))) continue;
      if (c != '(') throw ParseError("command must start with '('", 0);
      text.push_back(c);
    }
    // Adjacent doubled string quotes leave the scanner inside the string;
    // no lookahead is necessary, so a pipe never blocks after a full command.
    if (c == '"' || c == '|')
      quote = c;
    else if (c == '(')
      ++depth;
    else if (c == ')' && --depth == 0)
      return text;
  }
}

}  // namespace primo::smtlib
