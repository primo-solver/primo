#pragma once

#include <cstddef>
#include <vector>

#include "core/ids.hpp"
#include "core/term_manager.hpp"

namespace primo::smt {

class Preprocessing {};

struct EqualityBridgeResult {
  std::vector<core::TermId> equalities;
  std::size_t skipped_or_nodes = 0;
};

class EqualityBridge {
 public:
  explicit EqualityBridge(core::TermManager& terms);

  EqualityBridgeResult bridge(const std::vector<core::TermId>& assertions);

 private:
  core::TermManager& terms_;
};

std::vector<core::TermId> propagate_unit_equalities(core::TermManager& terms,
                                                    const std::vector<core::TermId>& assertions);

struct UfSymmetryBreakingResult {
  std::vector<core::TermId> constraints;
  std::size_t accepted_sets = 0;
  std::size_t values = 0;
  std::size_t terms = 0;
  std::size_t rejected_noninvariant = 0;
};

class UfSymmetryBreaker {
 public:
  explicit UfSymmetryBreaker(core::TermManager& terms);

  UfSymmetryBreakingResult break_symmetries(const std::vector<core::TermId>& assertions);

 private:
  core::TermManager& terms_;
};

}  // namespace primo::smt
