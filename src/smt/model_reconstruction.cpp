#include "smt/model_reconstruction.hpp"

#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "core/term_manager.hpp"

namespace primo::smt {

ModelConversionChain ModelConversionChain::appended(ModelConversionStep step) const {
  ModelConversionChain result;
  result.tail_ = std::make_shared<const Node>(
      Node{.step = std::move(step), .previous = tail_, .size = size() + 1});
  return result;
}

bool ModelConversionChain::operator==(const ModelConversionChain& other) const {
  if (tail_ == other.tail_) {
    return true;
  }
  std::shared_ptr<const Node> lhs = tail_;
  std::shared_ptr<const Node> rhs = other.tail_;
  while (lhs != nullptr && rhs != nullptr) {
    if (lhs->step != rhs->step) {
      return false;
    }
    lhs = lhs->previous;
    rhs = rhs->previous;
  }
  return lhs == nullptr && rhs == nullptr;
}

std::vector<TheoryModelAssignment> ModelConversionChain::theory_assignments(
    core::TermManager& terms) const {
  std::vector<TheoryModelAssignment> result;
  std::vector<const ModelConversionStep*> execution_order;
  execution_order.reserve(size());
  replay_reverse(
      [&execution_order](const ModelConversionStep& step) { execution_order.push_back(&step); });
  for (auto step = execution_order.rbegin(); step != execution_order.rend(); ++step) {
    std::visit(
        [&result, &terms](const auto& conversion) {
          using Conversion = std::decay_t<decltype(conversion)>;
          if constexpr (std::is_same_v<Conversion, BooleanTheoryAssignmentConversion>) {
            result.insert(result.end(), conversion.assignments.begin(),
                          conversion.assignments.end());
          } else if constexpr (std::is_same_v<Conversion, DirectedEqualitySubstitutionConversion>) {
            result.reserve(result.size() + conversion.substitutions.size());
            for (const auto& [lhs, rhs] : conversion.substitutions) {
              result.push_back(TheoryModelAssignment{.term = terms.mk_eq(lhs, rhs),
                                                     .value = true,
                                                     .filtered = false,
                                                     .directed_substitution = true});
            }
          }
        },
        **step);
  }
  return result;
}

}  // namespace primo::smt
