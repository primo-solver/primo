#include <CLI/CLI.hpp>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "sat/cadical_backend.hpp"
#include "smt/check_sat.hpp"
#include "util/statistics.hpp"

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

void print_outcome(const primo::smt::CheckSatOutcome& outcome, bool print_stats) {
  std::cout << primo::smt::to_string(outcome.result) << "\n";
  if (print_stats) {
    primo::util::print_statistics(std::cerr, outcome.statistics);
  }
}

std::optional<primo::smt::AutoBudget> parse_auto_budget(const std::string& text) {
  primo::smt::AutoBudget budget;
  if (text == "auto") {
    budget.kind = primo::smt::AutoBudget::Kind::Auto;
    return budget;
  }
  if (text == "unlimited") {
    budget.kind = primo::smt::AutoBudget::Kind::Unlimited;
    return budget;
  }
  try {
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed);
    if (consumed != text.size()) {
      return std::nullopt;
    }
    if (value > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    budget.kind = primo::smt::AutoBudget::Kind::Fixed;
    budget.value = static_cast<std::size_t>(value);
    return budget;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> files;
  bool quiet = false;
  bool print_stats = false;
  std::string qf_uf_preprocess = "strong";
  std::string qf_uf_profile = "default";
  std::string euf_engine = "incremental";
  std::string euf_prop = "off";
  std::string euf_prop_budget = "auto";
  std::string euf_prop_delivery = "auto";
  primo::smt::CheckSatOptions solver_options;

  CLI::App app{"Primo SMT solver scaffold"};
  app.add_flag("-q,--quiet", quiet, "Suppress startup version banner");
  app.add_flag("--stats", print_stats, "Print solver statistics to stderr");
  app.add_flag("--uf-symmetry-breaking", solver_options.uf_symmetry_breaking,
               "Enable experimental QF_UF symmetry breaking over invariant finite-domain values");
  app.add_flag("--eq-bridge", solver_options.eq_bridge,
               "Add equalities implied by every branch of selected QF_UF disjunctions");
  app.add_flag("--ground-congruence", solver_options.ground_congruence,
               "Enable bounded SAT-visible ground congruence clauses");
  app.add_option("--qf-uf-preprocess", qf_uf_preprocess,
                 "QF_UF preprocessing preset: off, basic, or strong")
      ->check(CLI::IsMember({"off", "basic", "strong"}));
  CLI::Option* profile_option = app.add_option("--qf-uf-profile", qf_uf_profile,
                                               "QF_UF profile: default or tuned-interesting")
                                    ->check(CLI::IsMember({"default", "tuned-interesting"}));
  app.add_option("--euf-prop", euf_prop, "EUF propagation mode: off, model, or online")
      ->check(CLI::IsMember({"off", "model", "online"}));
  app.add_option("--euf-engine", euf_engine, "EUF engine: incremental or lazy")
      ->check(CLI::IsMember({"lazy", "incremental"}));
  app.add_option("--euf-prop-budget", euf_prop_budget,
                 "EUF propagation candidate budget: auto, unlimited, or a nonnegative integer");
  app.add_option("--euf-prop-delivery,--prop-delivery-budget", euf_prop_delivery,
                 "EUF propagation delivery budget: auto, unlimited, or a nonnegative integer; 0 "
                 "means unlimited");
  CLI::Option* preprocess_passes_option =
      app.add_option("--preprocessing-passes,--preprocess-passes",
                     solver_options.preprocessing_passes,
                     "Number of QF_UF preprocessing passes to run")
          ->check(CLI::Range(0, 100));
  CLI::Option* prop_interval_option =
      app.add_option("--prop-interval", solver_options.euf_propagation_interval,
                     "Online EUF propagation discovery interval")
          ->check(CLI::Range(1, 1000000));
  CLI::Option* prop_assign_threshold_option =
      app.add_option("--prop-assign-threshold", solver_options.euf_propagation_assign_threshold,
                     "Skip online EUF propagation discovery above this assigned-atom fraction")
          ->check(CLI::Range(0.0, 1.0));
  app.add_option("files", files, "SMT-LIB input files. Reads stdin when omitted")
      ->type_name("FILE");
  app.set_version_flag("--version", version_text());

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }

  if (qf_uf_preprocess == "off") {
    solver_options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Off;
  } else if (qf_uf_preprocess == "strong") {
    solver_options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Strong;
  } else {
    solver_options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Basic;
  }
  if (profile_option->count() > 0 && qf_uf_profile == "tuned-interesting") {
    solver_options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Strong;
    solver_options.uf_symmetry_breaking = true;
    solver_options.eq_bridge = true;
    if (preprocess_passes_option->count() == 0) {
      solver_options.preprocessing_passes = 4;
    }
    if (prop_interval_option->count() == 0) {
      solver_options.euf_propagation_interval = 32;
    }
    if (prop_assign_threshold_option->count() == 0) {
      solver_options.euf_propagation_assign_threshold = 0.25;
    }
    if (euf_prop_delivery == "auto") {
      euf_prop_delivery = "1000";
    }
  }
  if (euf_prop == "model") {
    solver_options.euf_propagation = primo::smt::EufPropagationMode::Model;
  } else if (euf_prop == "online") {
    solver_options.euf_propagation = primo::smt::EufPropagationMode::Online;
  } else {
    solver_options.euf_propagation = primo::smt::EufPropagationMode::Off;
  }
  if (euf_engine == "incremental") {
    solver_options.euf_engine = primo::smt::EufEngine::Incremental;
  } else {
    solver_options.euf_engine = primo::smt::EufEngine::Lazy;
  }
  const std::optional<primo::smt::AutoBudget> parsed_prop_budget =
      parse_auto_budget(euf_prop_budget);
  const std::optional<primo::smt::AutoBudget> parsed_prop_delivery =
      parse_auto_budget(euf_prop_delivery);
  if (!parsed_prop_budget.has_value()) {
    std::cerr << "error: invalid --euf-prop-budget value: " << euf_prop_budget << "\n";
    return 1;
  }
  if (!parsed_prop_delivery.has_value()) {
    std::cerr << "error: invalid --euf-prop-delivery value: " << euf_prop_delivery << "\n";
    return 1;
  }
  solver_options.euf_propagation_budget = parsed_prop_budget.value();
  solver_options.euf_propagation_delivery = parsed_prop_delivery.value();

  if (!quiet) {
    print_startup_banner();
  }

  if (files.empty()) {
    std::ostringstream contents;
    contents << std::cin.rdbuf();
    const primo::smt::CheckSatOutcome outcome =
        primo::smt::check_sat_smtlib(contents.str(), solver_options);
    print_outcome(outcome, print_stats);
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
    print_outcome(outcome, print_stats);
    if (!outcome.message.empty()) {
      std::cerr << file << ": " << outcome.message << "\n";
    }
    if (outcome.result == primo::smt::CheckSatResult::Unsupported) {
      exit_code = 2;
    }
  }
  return exit_code;
}
