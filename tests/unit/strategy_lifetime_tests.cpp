// A Strategy is documented as an immutable value with a canonical form and a
// stable fingerprint, which is what makes it worth caching, comparing, and
// reusing as the tactic layer grows more theories and logics. That is only
// true if a strategy node carries no reference to the session that built it.
// These tests pin the preprocessing strategy to that contract: it must read
// its term manager and statistics from the TacticContext it is executed
// under, not from whatever was in scope at construction time.

#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smt/preprocessing_strategy.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"
#include "util/statistics.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

primo::smt::CheckSatOptions looping_options() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.preprocessing_passes = 1;
  options.preprocessing_adaptive_extra_passes = 2;
  return options;
}

// A goal with enough structure that the preprocessing round reports at least
// one completed pass through the repeat observer.
std::vector<primo::core::TermId> looping_assertions(primo::core::TermManager& terms) {
  const primo::core::TermId zero = terms.mk_rational(0);
  const primo::core::TermId one = terms.mk_rational(1);
  const primo::core::TermId bound = terms.mk_le(zero, one);
  const primo::core::TermId negated = terms.mk_not(terms.mk_not(bound));
  return {bound, negated};
}

primo::smt::Strategy build_preprocessing_strategy(primo::smt::Frontend& frontend,
                                                  const primo::smt::CheckSatOptions& options,
                                                  primo::util::Statistics& build_statistics) {
  const std::vector<primo::core::TermId> assertions = looping_assertions(frontend.terms());
  const primo::smt::PreprocessingProfile profile =
      primo::smt::analyze_preprocessing_profile(frontend.terms(), assertions, options, false);
  // Retained as the "wrong" sink the counters must not reach.
  static_cast<void>(build_statistics);
  return primo::smt::make_preprocessing_strategy(frontend, options, profile, assertions);
}

// The counters must follow the execution, not the construction. A cached
// strategy re-executed under a second session would otherwise report its work
// into the first session's statistics.
void preprocessing_strategy_records_passes_into_the_executing_context() {
  const primo::smt::CheckSatOptions options = looping_options();
  primo::smt::Frontend frontend;
  primo::util::Statistics build_statistics;
  const primo::smt::Strategy strategy =
      build_preprocessing_strategy(frontend, options, build_statistics);

  primo::util::Statistics execution_statistics;
  primo::smt::TacticContext context(frontend.terms(), execution_statistics);
  const primo::smt::Tactic pipeline = compile(strategy);
  const std::vector<primo::core::TermId> assertions = looping_assertions(frontend.terms());
  static_cast<void>(pipeline(context, primo::smt::AnyOf::single(primo::smt::Goal{assertions})));

  check(execution_statistics.get(":tactic.preprocessing-pass.invocations") > 0);
  check(build_statistics.get(":tactic.preprocessing-pass.invocations") == 0);
}

}  // namespace

int main() {
  preprocessing_strategy_records_passes_into_the_executing_context();
  return 0;
}
