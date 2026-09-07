#include "smt/model_reconstruction.hpp"

#include <memory>
#include <utility>
#include <variant>

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

}  // namespace primo::smt
