#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <unordered_map>

#include "core/ids.hpp"
#include "core/model.hpp"
#include "smt/model_reconstruction.hpp"
#include "util/rational.hpp"

namespace primo::smt {

// Inputs describe the final component problem, not a flattened reconstruction
// history. See docs/model-interpretation.md for the adapter/replay contract.
std::shared_ptr<const core::Model> build_typed_model(
    core::TermManager& terms, std::span<const core::TermId> assertions,
    std::span<const TheoryModelAssignment> assignments,
    const std::unordered_map<core::TermId, util::Rational>& numeric_values,
    const ModelConversionChain& conversions, std::size_t* completed_integers = nullptr);

}  // namespace primo::smt
