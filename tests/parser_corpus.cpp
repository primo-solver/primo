#include <algorithm>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "smtlib/commands.hpp"
#include "smtlib/parser.hpp"

// Test-only command traversal using the production streaming Parser. No term
// translation or solving: see docs/incremental-smt.md, native archive checks.
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input");
    primo::smtlib::Parser parser(input);
    std::size_t commands = 0, assertions = 0, declarations = 0;
    std::size_t pushes = 0, pops = 0, depth = 0, max_depth = 0, unsupported = 0;
    std::vector<std::streamoff> query_ends;
    while (auto command = parser.next_command()) {
      ++commands;
      using Kind = primo::smtlib::CommandKind;
      switch (command->kind) {
        case Kind::Assert:
          ++assertions;
          break;
        case Kind::DeclareSort:
        case Kind::DeclareFun:
        case Kind::DefineFun:
          ++declarations;
          break;
        case Kind::Push:
          if (command->count > std::numeric_limits<std::size_t>::max() - depth)
            throw std::runtime_error("scope depth overflow");
          depth += command->count;
          max_depth = std::max(depth, max_depth);
          ++pushes;
          break;
        case Kind::Pop:
          if (command->count > depth) throw std::runtime_error("pop underflow");
          depth -= command->count;
          ++pops;
          break;
        case Kind::Reset:
        case Kind::ResetAssertions:
          depth = 0;
          break;
        case Kind::CheckSat: {
          // next_command consumes exactly through ')', with no token lookahead.
          // The seekable file position therefore identifies an exact raw prefix.
          const auto position = input.tellg();
          if (position < std::streampos{0}) throw std::runtime_error("unavailable byte offset");
          query_ends.push_back(static_cast<std::streamoff>(position));
          break;
        }
        case Kind::Unsupported:
          ++unsupported;
          break;
        default:
          break;
      }
    }
    if (input.bad()) throw std::runtime_error("input I/O error");
    std::cout << "{\"commands\":" << commands << ",\"assertions\":" << assertions
              << ",\"declarations\":" << declarations << ",\"pushes\":" << pushes
              << ",\"pops\":" << pops << ",\"max_depth\":" << max_depth
              << ",\"final_depth\":" << depth << ",\"unsupported\":" << unsupported
              << ",\"query_ends\":[";
    for (std::size_t i = 0; i < query_ends.size(); ++i) {
      if (i != 0) std::cout << ',';
      std::cout << query_ends[i];
    }
    std::cout << "]}\n";
    return unsupported == 0 ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
