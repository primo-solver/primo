#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "smt/check_sat.hpp"

namespace {

void print_usage(const char* argv0) {
  std::cout << "usage: " << argv0 << " [--help] [--version] [file.smt2 ...]\n";
}

bool can_read_file(const std::string& path) {
  std::ifstream input(path);
  return input.good();
}

std::string read_file(const std::string& path) {
  std::ifstream input(path);
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> files;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    }
    if (arg == "--version") {
      std::cout << "primo scaffold 0.1.0\n";
      return 0;
    }
    files.push_back(arg);
  }

  if (files.empty()) {
    std::ostringstream contents;
    contents << std::cin.rdbuf();
    const primo::smt::CheckSatOutcome outcome = primo::smt::check_sat_smtlib(contents.str());
    std::cout << primo::smt::to_string(outcome.result) << "\n";
    if (!outcome.message.empty()) {
      std::cerr << outcome.message << "\n";
    }
    return outcome.result == primo::smt::CheckSatResult::Unsupported ? 2 : 0;
  }

  int exit_code = 0;
  for (const auto& file : files) {
    if (!can_read_file(file)) {
      std::cerr << "error: cannot read input file: " << file << "\n";
      return 1;
    }
    const primo::smt::CheckSatOutcome outcome = primo::smt::check_sat_smtlib(read_file(file));
    std::cout << primo::smt::to_string(outcome.result) << "\n";
    if (!outcome.message.empty()) {
      std::cerr << file << ": " << outcome.message << "\n";
    }
    if (outcome.result == primo::smt::CheckSatResult::Unsupported) {
      exit_code = 2;
    }
  }
  return exit_code;
}
