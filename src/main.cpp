#include <CLI/CLI.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "sat/cadical_backend.hpp"
#include "smt/check_sat.hpp"

#ifndef PRIMO_GIT_REVISION
#define PRIMO_GIT_REVISION "unknown"
#endif

namespace {

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

std::string version_text() {
  std::ostringstream version;
  version << "primo 0.1.0\n";
  version << "git: " << PRIMO_GIT_REVISION << "\n";
  version << "cadical: " << primo::sat::cadical_signature();
  return version.str();
}

void print_startup_banner() {
  std::cerr << "primo git " << PRIMO_GIT_REVISION << "\n";
  std::cerr << "cadical " << primo::sat::cadical_signature() << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> files;
  bool quiet = false;
  primo::smt::CheckSatOptions solver_options;

  CLI::App app{"Primo SMT solver scaffold"};
  app.add_flag("-q,--quiet", quiet, "Suppress startup version banner");
  app.add_flag("--uf-symmetry-breaking", solver_options.uf_symmetry_breaking,
               "Enable experimental QF_UF symmetry breaking over invariant finite-domain values");
  app.add_option("files", files, "SMT-LIB input files. Reads stdin when omitted")
      ->type_name("FILE");
  app.set_version_flag("--version", version_text());

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }

  if (!quiet) {
    print_startup_banner();
  }

  if (files.empty()) {
    std::ostringstream contents;
    contents << std::cin.rdbuf();
    const primo::smt::CheckSatOutcome outcome =
        primo::smt::check_sat_smtlib(contents.str(), solver_options);
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
    const primo::smt::CheckSatOutcome outcome =
        primo::smt::check_sat_smtlib(read_file(file), solver_options);
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
