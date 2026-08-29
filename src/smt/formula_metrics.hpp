#pragma once

#include <cstddef>
#include <vector>

#include "core/ids.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::smt {

// Counts each term reachable from the roots once. The traversal follows the
// hash-consed core DAG and does not allocate or intern terms.
std::size_t reachable_term_count(const core::TermManager& terms,
                                 const std::vector<core::TermId>& roots);

}  // namespace primo::smt
