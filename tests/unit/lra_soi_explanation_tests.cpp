#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

namespace {
using primo::sat::Lit;
using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::LraTableau;
using primo::theory::arithmetic::LraVarId;
using primo::util::Rational;

// Original equations and assertions are saved before any pivot. Each literal
// names its input formula, or the original antecedents of a derived bound.
// Exported conflicts contain ONLY those formulas selected by the explanation.
struct Fixture {
  LraTableau tableau{LraTableau::PivotingRule::Soi};
  std::vector<std::string> equations;
  std::map<int, std::string> premises;

  LraVarId variable() { return tableau.add_variable(); }
  void row(LraVarId basic, const std::vector<std::pair<LraVarId, Rational>>& terms) {
    std::string expression = "(+ 0";
    for (const auto& [var, coefficient] : terms) {
      // Fixture coefficients are integers; serialize negative numerals using
      // SMT-LIB unary minus, independently of the solver's printer.
      const auto text = coefficient.to_string();
      const auto numeral = coefficient.sign() < 0 ? "(- " + text.substr(1) + ")" : text;
      expression += " (* " + numeral + " v" + std::to_string(var) + ")";
    }
    equations.push_back("(assert (= v" + std::to_string(basic) + " " + expression + ")))");
    tableau.add_row(basic, terms);
  }
  void bound(LraVarId var, bool upper, long value, int reason, bool strict = false) {
    const auto sign = upper ? -1 : 1;
    const auto delta = DeltaRational{Rational(value), Rational(strict ? sign : 0)};
    const auto conflict = upper ? tableau.assert_upper(var, delta, Lit{reason})
                                : tableau.assert_lower(var, delta, Lit{reason});
    assert(!conflict);
    const std::string relation = upper ? (strict ? "<" : "<=") : (strict ? ">" : ">=");
    const auto numeral = value < 0 ? "(- " + std::to_string(-value) + ")" : std::to_string(value);
    premises.emplace(reason,
                     "(assert (" + relation + " v" + std::to_string(var) + " " + numeral + "))");
  }
  void export_conflict(const std::filesystem::path& directory, const std::string& name) {
    const auto conflict = tableau.check();
    assert(conflict && !conflict->empty());
    std::ofstream out(directory / (name + ".smt2"));
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << "(set-logic QF_LRA)\n";
    for (LraVarId var = 0; var < tableau.variable_count(); ++var)
      out << "(declare-fun v" << var << " () Real)\n";
    for (const auto& equation : equations) out << equation << '\n';
    for (const auto reason : *conflict) out << premises.at(reason.value) << '\n';
    out << "(check-sat)\n";
  }
};

void cases(const std::filesystem::path& directory, LraTableau::SoiMinimization arm,
           const std::string& suffix, bool paper, bool cache = false) {
  const auto configure = [arm, paper, cache](Fixture& fixture) {
    auto tuning = fixture.tableau.tuning();
    tuning.soi_minimization = arm;
    tuning.soi_cached_conflicts = cache;
    tuning.soi_linear_maintenance = true;
    tuning.soi_selection =
        paper ? LraTableau::SoiSelectionRule::Paper : LraTableau::SoiSelectionRule::Historical;
    fixture.tableau.set_tuning(tuning);
  };
  {
    Fixture f;
    configure(f);
    const auto x = f.variable();
    const auto b = f.variable();
    f.row(b, {{x, Rational(1)}});
    f.bound(x, true, 0, 1);
    f.bound(b, false, 0, 2, true);
    f.export_conflict(directory, "strict-" + suffix);
  }
  {
    Fixture f;
    configure(f);
    const auto x = f.variable();
    const auto y = f.variable();
    const auto b = f.variable();
    const auto c = f.variable();
    f.row(b, {{x, Rational(1)}, {y, Rational(-1)}});
    f.row(c, {{x, Rational(-1)}, {y, Rational(1)}});
    f.bound(b, false, 1, 1);
    f.bound(c, false, 0, 2, true);
    f.export_conflict(directory, "multi-row-cancellation-" + suffix);
  }
  {
    Fixture f;
    configure(f);
    const auto x = f.variable();
    const auto b = f.variable();
    f.row(b, {{x, Rational(2)}});
    f.bound(x, true, 1, 1);
    f.bound(b, false, 6, 2);
    f.export_conflict(directory, "past-entering-bound-" + suffix);
    if (paper && cache) assert(f.tableau.soi_work().conflict_preferences == 1);
  }
  {
    // P({b}) holds, P({b,c}) fails, P({b,c,d}) holds. The minimizer's
    // certificate predicate is non-monotone, even though SAT is monotone.
    Fixture f;
    configure(f);
    const auto x = f.variable();
    const auto b = f.variable();
    const auto c = f.variable();
    const auto d = f.variable();
    f.row(b, {{x, Rational(-1)}});
    f.row(c, {{x, Rational(2)}});
    f.row(d, {{x, Rational(-2)}});
    f.bound(x, false, 0, 1);
    f.bound(b, false, 1, 2);
    f.bound(c, false, 1, 3);
    f.bound(d, false, 1, 4);
    f.export_conflict(directory, "non-monotone-" + suffix);
  }
  {
    Fixture f;
    configure(f);
    const auto x = f.variable();
    const auto y = f.variable();
    const auto sum = f.variable();
    const auto b = f.variable();
    f.row(sum, {{x, Rational(1)}, {y, Rational(1)}});
    f.row(b, {{x, Rational(1)}});
    f.bound(sum, true, 0, 1);
    f.bound(y, false, 0, 2);
    // Row propagation implies x <= 0. Expand its reason into the two
    // original premises; never include the propagated formula in the cube.
    f.bound(x, true, 0, 3);
    f.premises[3] = f.premises.at(1) + "\n" + f.premises.at(2);
    f.bound(b, false, 0, 4, true);
    f.export_conflict(directory, "expanded-propagation-" + suffix);
  }
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::filesystem::path directory(argv[1]);
  std::filesystem::create_directories(directory);
  for (const bool paper : {false, true}) {
    for (const bool cache : {false, true}) {
      if (!paper && cache) continue;
      const std::string prefix = paper ? (cache ? "cached-" : "paper-") : "historical-";
      cases(directory, LraTableau::SoiMinimization::QuickXplain, prefix + "recursive", paper,
            cache);
      cases(directory, LraTableau::SoiMinimization::RowGrowing, prefix + "row-growing", paper,
            cache);
      cases(directory, LraTableau::SoiMinimization::Deletion, prefix + "deletion", paper, cache);
      cases(directory, LraTableau::SoiMinimization::None, prefix + "none", paper, cache);
    }
  }
}
