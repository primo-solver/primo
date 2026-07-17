#include <unistd.h>

#include <CLI/CLI.hpp>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "sat/cadical_backend.hpp"
#include "smt/check_sat.hpp"
#include "util/statistics.hpp"

#ifndef PRIMO_GIT_REVISION
#define PRIMO_GIT_REVISION "unknown"
#endif

namespace {

primo::util::Statistics g_live_statistics;
bool g_print_timeout_statistics = false;

void handle_sigterm(int) {
  [[maybe_unused]] const ssize_t written = write(STDOUT_FILENO, "unknown\n", 8);
  if (g_print_timeout_statistics) {
    primo::util::print_statistics(std::cerr, g_live_statistics);
  }
  std::exit(0);
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

std::optional<std::pair<std::string, int>> parse_cadical_option(const std::string& text) {
  const std::size_t separator = text.find('=');
  if (separator == std::string::npos || separator == 0 || separator + 1 == text.size()) {
    return std::nullopt;
  }
  try {
    std::size_t consumed = 0;
    const long value = std::stol(text.substr(separator + 1), &consumed);
    if (consumed != text.size() - separator - 1) {
      return std::nullopt;
    }
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
      return std::nullopt;
    }
    return std::pair{text.substr(0, separator), static_cast<int>(value)};
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGTERM, handle_sigterm);
  std::vector<std::string> files;
  bool quiet = false;
  bool print_stats = false;
  bool no_uf_symmetry_breaking = false;
  bool no_finite_domain_value_precedence = false;
  bool finite_domain_equality_definitions = false;
  bool no_finite_domain_equality_definitions = false;
  bool no_eq_bridge = false;
  bool no_solved_constant_substitution = false;
  std::string qf_uf_preprocess = "auto";
  std::string qf_uf_profile = "default";
  std::string euf_engine = "incremental";
  std::string euf_prop = "auto";
  std::string pure_theory_filter = "auto";
  std::string euf_prop_budget = "auto";
  std::string euf_prop_delivery = "1000";
  std::size_t max_theory_conflicts = 0;
  std::vector<std::string> cadical_options;
  primo::smt::CheckSatOptions solver_options;

  CLI::App app{"Primo SMT solver scaffold"};
  app.add_flag("-q,--quiet", quiet, "Suppress startup version banner");
  app.add_flag("--stats", print_stats, "Print solver statistics to stderr");
  app.add_flag("--uf-symmetry-breaking", solver_options.uf_symmetry_breaking,
               "Enable default-on QF_UF symmetry breaking over invariant finite-domain values");
  app.add_flag("--no-uf-symmetry-breaking", no_uf_symmetry_breaking,
               "Disable QF_UF finite-domain symmetry breaking");
  app.add_flag("--finite-domain-value-precedence", solver_options.finite_domain_value_precedence,
               "Enable fixed-term value precedence inside finite-domain symmetry breaking");
  app.add_flag("--no-finite-domain-value-precedence", no_finite_domain_value_precedence,
               "Disable fixed-term value precedence inside finite-domain symmetry breaking");
  app.add_flag("--finite-domain-equality-definitions", finite_domain_equality_definitions,
               "Enable guarded finite-domain equality definitions in auto preprocessing");
  app.add_flag("--no-finite-domain-equality-definitions", no_finite_domain_equality_definitions,
               "Disable finite-domain SAT-visible equality definition clauses");
  app.add_flag("--eq-bridge", solver_options.eq_bridge,
               "Add equalities implied by every branch of selected QF_UF disjunctions");
  app.add_flag("--no-eq-bridge", no_eq_bridge,
               "Disable equality bridging unless required by the preprocessing preset");
  app.add_flag("--ground-congruence", solver_options.ground_congruence,
               "Enable bounded SAT-visible ground congruence clauses");
  app.add_flag("--ground-unit-substitution", solver_options.ground_unit_substitution,
               "Enable experimental ground substitution from visible unit equalities");
  app.add_flag("--guarded-ground-unit-substitution",
               solver_options.guarded_ground_unit_substitution,
               "Enable guarded ground substitution only when preprocessing metrics predict a win");
  app.add_flag("--no-solved-constant-substitution", no_solved_constant_substitution,
               "Disable default QF_UF solved-constant substitution")
      ->default_val(false);
  app.add_flag("--preprocess-only", solver_options.preprocess_only,
               "Run parsing and preprocessing, print unknown, and skip SAT/theory solving");
  app.add_option("--qf-uf-preprocess", qf_uf_preprocess,
                 "QF_UF preprocessing preset: off, basic, strong, or auto")
      ->check(CLI::IsMember({"off", "basic", "strong", "auto"}));
  CLI::Option* profile_option = app.add_option("--qf-uf-profile", qf_uf_profile,
                                               "QF_UF profile: default or tuned-interesting")
                                    ->check(CLI::IsMember({"default", "tuned-interesting"}));
  app.add_option("--euf-prop", euf_prop,
                 "EUF propagation mode: auto, off, conflict, model, or online")
      ->check(CLI::IsMember({"auto", "off", "conflict", "model", "online"}));
  app.add_option("--euf-engine", euf_engine, "EUF engine: incremental or lazy")
      ->check(CLI::IsMember({"lazy", "incremental"}));
  app.add_option("--pure-theory-filter", pure_theory_filter,
                 "T-pure theory literal filtering mode: auto, on, or off")
      ->check(CLI::IsMember({"auto", "on", "off"}));
  app.add_option("--euf-prop-budget", euf_prop_budget,
                 "EUF propagation candidate budget: auto, unlimited, or a nonnegative integer");
  app.add_option("--euf-prop-delivery,--prop-delivery-budget", euf_prop_delivery,
                 "EUF propagation delivery budget: auto, unlimited, or a nonnegative integer; 0 "
                 "means unlimited");
  CLI::Option* max_theory_conflicts_option =
      app.add_option("--max-theory-conflicts", max_theory_conflicts,
                     "Return unknown after at least this many learned theory conflicts")
          ->check(CLI::Range(1, 1000000000));
  app.add_option_function<std::string>(
         "--cadical-option",
         [&cadical_options](const std::string& option) { cadical_options.push_back(option); },
         "Set a CaDiCaL integer option as name=value; may be repeated")
      ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
  CLI::Option* preprocess_passes_option =
      app.add_option("--preprocessing-passes,--preprocess-passes",
                     solver_options.preprocessing_passes,
                     "Number of QF_UF preprocessing passes to run")
          ->check(CLI::Range(0, 100));
  app.add_option("--preprocessing-adaptive-extra-passes",
                 solver_options.preprocessing_adaptive_extra_passes,
                 "Additional adaptive QF_UF preprocessing passes after the configured pass cap")
      ->check(CLI::Range(0, 100));
  app.add_option("--preprocessing-adaptive-shrink-percent",
                 solver_options.preprocessing_adaptive_shrink_percent,
                 "Minimum reachable-term shrink percentage required for another adaptive pass")
      ->check(CLI::Range(0, 100));
  app.add_option(
         "--finite-domain-eqdefs-auto-domain-limit",
         solver_options.finite_domain_eqdefs_auto_domain_limit,
         "Maximum finite-domain value count for auto equality definitions; 0 means unlimited")
      ->check(CLI::Range(0, 1000000));
  app.add_option("--finite-domain-eqdefs-auto-clause-limit",
                 solver_options.finite_domain_eqdefs_auto_clause_limit,
                 "Maximum generated equality-definition clauses per check-sat in auto; 0 "
                 "means unlimited")
      ->check(CLI::Range(0, 100000000));
  app.add_option("--finite-domain-eqdefs-auto-term-limit",
                 solver_options.finite_domain_eqdefs_auto_term_limit,
                 "Maximum initial reachable-term count for auto equality definitions on "
                 "single-assertion formulas; 0 means unlimited")
      ->check(CLI::Range(0, 100000000));
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
  } else if (qf_uf_preprocess == "auto") {
    solver_options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Auto;
  } else {
    solver_options.qf_uf_preprocess = primo::smt::QfUfPreprocessMode::Basic;
  }
  if (profile_option->count() > 0 && qf_uf_profile == "tuned-interesting") {
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
  if (no_uf_symmetry_breaking) {
    solver_options.uf_symmetry_breaking = false;
  }
  if (no_finite_domain_value_precedence) {
    solver_options.finite_domain_value_precedence = false;
  }
  if (finite_domain_equality_definitions) {
    solver_options.auto_finite_domain_equality_definitions = true;
    solver_options.finite_domain_equality_definitions = true;
  }
  if (no_finite_domain_equality_definitions) {
    solver_options.finite_domain_equality_definitions = false;
    solver_options.auto_finite_domain_equality_definitions = false;
  }
  if (no_eq_bridge) {
    solver_options.eq_bridge = false;
  }
  if (no_solved_constant_substitution) {
    solver_options.solved_constant_substitution = false;
  }
  if (euf_prop == "auto") {
    solver_options.euf_propagation = primo::smt::EufPropagationMode::Auto;
  } else if (euf_prop == "conflict") {
    solver_options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  } else if (euf_prop == "model") {
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
  if (pure_theory_filter == "off") {
    solver_options.pure_theory_filter = primo::smt::PureTheoryFilterMode::Off;
  } else if (pure_theory_filter == "on") {
    solver_options.pure_theory_filter = primo::smt::PureTheoryFilterMode::On;
  } else {
    solver_options.pure_theory_filter = primo::smt::PureTheoryFilterMode::Auto;
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
  if (max_theory_conflicts_option->count() > 0) {
    solver_options.max_theory_conflicts = max_theory_conflicts;
  }
  for (const std::string& option : cadical_options) {
    const std::optional<std::pair<std::string, int>> parsed = parse_cadical_option(option);
    if (!parsed.has_value()) {
      std::cerr << "error: invalid --cadical-option value: " << option << "\n";
      return 1;
    }
    solver_options.cadical_options.push_back(parsed.value());
  }

  if (!quiet) {
    print_startup_banner();
  }
  g_print_timeout_statistics = print_stats;
  if (print_stats) {
    solver_options.live_statistics = &g_live_statistics;
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
