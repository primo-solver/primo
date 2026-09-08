#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include "core/model.hpp"
#include "core/term_manager.hpp"
#include "smt/model_reconstruction.hpp"
#include "theory/theory_component.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

// Uninterpreted-domain participants export every normalized variable so local
// domains can be identified by shared terms. Native authoritative owners do
// likewise; structural participants may omit native anchors. Check the export
// against the accepted projection (guided) or arrangement (complete).
void validate_component_fragment(const core::TermManager& terms,
                                 const theory::ComponentDescriptor& descriptor,
                                 std::span<const theory::ComponentInterfaceScope> scopes,
                                 const theory::TheoryModelToken& token,
                                 const theory::TheoryModelProjection* projection,
                                 std::span<const theory::InterfaceRelation> arrangement,
                                 const theory::ComponentModelFragment& fragment);

// Fragment-only construction: no component-private tableau or closure access.
// The old typed-model entry point remains unchanged for frozen solver routes.
std::shared_ptr<const core::Model> assemble_component_model(
    core::TermManager& terms, std::span<const core::TermId> assertions,
    std::span<const theory::ComponentModelFragment> fragments,
    const ModelConversionChain& conversions, std::size_t* completed_integers,
    util::Statistics* statistics);

}  // namespace primo::smt
