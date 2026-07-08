#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage(const char* argv0) {
  std::cout << "usage: " << argv0 << " [--help] [--version] [file.smt2 ...]\n";
}

bool can_read_file(const std::string& path) {
  std::ifstream input(path);
  return input.good();
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

  for (const auto& file : files) {
    if (!can_read_file(file)) {
      std::cerr << "error: cannot read input file: " << file << "\n";
      return 1;
    }
  }

  std::cout << "unsupported\n";
  std::cerr << "Primo scaffold: SMT solving is not implemented yet.\n";
  std::cerr << "Next step: implement M0 typed sorts, symbols, and terms.\n";
  return 2;
}
