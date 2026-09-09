#pragma once

#include <istream>
#include <optional>
#include <string>

namespace primo::smtlib {

// Frames commands only; Parser remains the grammar/type-independent AST parser.
// The borrowed stream must outlive this reader. next() never reads past ')'.
class CommandReader {
 public:
  explicit CommandReader(std::istream& input) : input_(input) {}
  std::optional<std::string> next();

 private:
  std::istream& input_;
};

}  // namespace primo::smtlib
