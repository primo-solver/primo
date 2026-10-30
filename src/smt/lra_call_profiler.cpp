#include "smt/lra_call_profiler.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "util/rational.hpp"

namespace primo::smt {
namespace {

std::string numeral(const util::Rational& value) {
  std::string text = value.to_string();
  const bool negative = !text.empty() && text.front() == '-';
  if (negative) text.erase(0, 1);
  const auto slash = text.find('/');
  if (slash != std::string::npos) {
    // QF_LRA concrete coefficients use (/ c n), with the sign on integer c.
    // See SMT-LIB's QF_LRA :extensions, not an outer negation of division.
    const std::string numerator =
        negative ? "(- " + text.substr(0, slash) + ")" : text.substr(0, slash);
    return "(/ " + numerator + " " + text.substr(slash + 1) + ")";
  }
  return negative ? "(- " + text + ")" : text;
}

}  // namespace

LraCallProfiler::LraCallProfiler(const std::string& path, std::string export_directory,
                                 std::size_t export_min_pivots, std::size_t export_limit)
    : export_directory_(std::move(export_directory)),
      export_min_pivots_(export_min_pivots),
      export_limit_(export_limit) {
  out_.exceptions(std::ios::failbit | std::ios::badbit);
  out_.open(path);
  if (!export_directory_.empty()) {
    const std::filesystem::path directory(export_directory_);
    if (std::filesystem::exists(directory) && !std::filesystem::is_empty(directory)) {
      throw std::invalid_argument("LRA call export directory must be empty");
    }
    std::filesystem::create_directories(directory);
  }
  out_ << "{\"event\":\"begin\",\"schema\":1}\n";
  out_.flush();
}

LraCallProfiler::~LraCallProfiler() {
  // An output failure must not throw from a destructor during stack unwinding.
  out_.exceptions(std::ios::goodbit);
  out_ << "{\"event\":\"end\",\"checkers\":" << checkers_ << ",\"calls\":" << calls_
       << ",\"exported\":" << exported_ << "}\n";
  out_.flush();
}

LraCallProfiler::Checker LraCallProfiler::register_checker(const Tableau& base) {
  Checker checker{++checkers_, base.variable_count(), 0, ""};
  std::size_t rows = 0;
  std::size_t nonzeros = 0;
  std::ostringstream definitions;
  for (std::size_t var = 0; var < checker.variables; ++var) {
    if (!export_directory_.empty()) definitions << "(declare-fun v" << var << " () Real)\n";
  }
  for (std::size_t var = 0; var < checker.variables; ++var) {
    if (!base.is_basic(var)) continue;
    ++rows;
    const auto& row = base.row(var);
    nonzeros += row.size();
    if (export_directory_.empty()) continue;
    definitions << "(assert (= v" << var << " ";
    if (row.empty()) {
      definitions << '0';
    } else {
      if (row.size() > 1) definitions << "(+ ";
      for (const auto& entry : row) {
        definitions << "(* " << numeral(entry.coeff) << " v" << entry.var << ") ";
      }
      if (row.size() > 1) definitions << ')';
    }
    definitions << "))\n";
  }
  checker.definitions = definitions.str();
  out_ << "{\"event\":\"checker\",\"checker\":" << checker.id
       << ",\"variables\":" << checker.variables << ",\"rows\":" << rows
       << ",\"nonzeros\":" << nonzeros << "}\n";
  out_.flush();
  return checker;
}

std::string LraCallProfiler::export_cube(const Checker& checker, const Tableau& tableau,
                                         bool conflict) {
  if (tableau.variable_count() != checker.variables) return "skip-variable-count";
  std::ostringstream body;
  for (std::size_t var = 0; var < checker.variables; ++var) {
    for (bool upper : {false, true}) {
      const auto bound = upper ? tableau.upper_bound(var) : tableau.lower_bound(var);
      if (!bound) continue;
      const int delta = bound->value.infinitesimal.sign();
      if ((upper && delta > 0) || (!upper && delta < 0)) return "skip-delta-direction";
      const char* relation = upper ? (delta < 0 ? "<" : "<=") : (delta > 0 ? ">" : ">=");
      body << "(assert (" << relation << " v" << var << ' ' << numeral(bound->value.rational)
           << "))\n";
    }
  }
  const std::string name =
      "checker-" + std::to_string(checker.id) + "-call-" + std::to_string(checker.calls) + ".smt2";
  std::ofstream cube;
  cube.exceptions(std::ios::failbit | std::ios::badbit);
  cube.open(std::filesystem::path(export_directory_) / name);
  cube << "; Numeric feasibility conjunction; tableau definitions and active bounds only.\n"
       << "; Original Boolean context, disequality splitting and warm basis are not replayed.\n"
       << "(set-logic QF_LRA)\n(set-info :status " << (conflict ? "unsat" : "sat") << ")\n"
       << checker.definitions << body.str() << "(check-sat)\n";
  cube.close();
  ++exported_;
  return name;
}

void LraCallProfiler::record(Checker& checker, const Tableau& tableau, std::size_t pivots,
                             std::uint64_t nanoseconds, std::size_t row_updates,
                             std::size_t term_ops, bool conflict) {
  ++checker.calls;
  ++calls_;
  std::string exported;
  if (!export_directory_.empty() && exported_ < export_limit_ && pivots >= export_min_pivots_) {
    exported = export_cube(checker, tableau, conflict);
  }
  out_ << "{\"event\":\"check\",\"checker\":" << checker.id << ",\"call\":" << checker.calls
       << ",\"pivots\":" << pivots << ",\"nanoseconds\":" << nanoseconds
       << ",\"row_updates\":" << row_updates << ",\"term_ops\":" << term_ops
       << ",\"conflict\":" << (conflict ? "true" : "false") << ",\"export\":\"" << exported
       << "\"}\n";
  if (calls_ % 1024 == 0) out_.flush();
}

}  // namespace primo::smt
