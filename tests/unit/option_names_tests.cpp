#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <set>
#include <string_view>

#include "cli/option_names.hpp"

namespace {

using primo::cli::enum_name;
using primo::cli::EnumName;
using primo::cli::parse_enum;

// A table for an enum with `enumerators` values numbered from zero: names are
// unique and parse to their entry, every enumerator has a canonical name that
// parses back to it, and unknown names are rejected.
template <typename Enum, std::size_t N>
void check_table(const std::array<EnumName<Enum>, N>& names, std::size_t enumerators) {
  std::set<std::string_view> seen;
  for (const auto& entry : names) {
    assert(seen.insert(entry.name).second);
    assert(static_cast<std::size_t>(entry.value) < enumerators);
    assert(parse_enum(names, entry.name) == entry.value);
  }
  for (std::size_t index = 0; index < enumerators; ++index) {
    const auto value = static_cast<Enum>(index);
    assert(parse_enum(names, enum_name(names, value)) == value);
  }
  assert(!parse_enum(names, "no-such-name").has_value());
}

}  // namespace

int main() {
  using namespace primo::cli;
  check_table(kIncrementalModeNames, 2);
  check_table(kPreprocessingModeNames, 4);
  check_table(kAutoPreprocessSelectorNames, 2);
  check_table(kBooleanImpliesRewriteNames, 3);
  check_table(kQfUfPortfolioNames, 2);
  check_table(kEufPropagationNames, 5);
  check_table(kEufEngineNames, 2);
  check_table(kEufBooleanDomainNames, 2);
  check_table(kEufExplainSearchNames, 5);
  check_table(kLraPivotingRuleNames, 4);
  check_table(kLraSoiMinimizationNames, 3);
  check_table(kLraSoiMinimizationOrderNames, 3);
  check_table(kMixedDispatchNames, 2);
  check_table(kQfUflraLraEqualityProducerNames, 3);
  check_table(kModelEqualityMutationScopeNames, 2);
  check_table(kNelsonOppenPropagationNames, 3);
  check_table(kModelEqualityBranchPolicyNames, 4);
  check_table(kPureTheoryFilterNames, 3);
  check_table(kTheoryPhaseNames, 5);
  check_table(kTupleOrderNames, 6);
  // An alias parses to its value; the canonical name comes first.
  assert(parse_enum(kNelsonOppenPropagationNames, "off") ==
         primo::smt::NelsonOppenPropagationMode::Exact);
  assert(enum_name(kNelsonOppenPropagationNames, primo::smt::NelsonOppenPropagationMode::Exact) ==
         "exact");
  return 0;
}
