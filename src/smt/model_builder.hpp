#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/term_manager.hpp"

namespace primo::smt {

struct TheoryModelAssignment {
  core::TermId term = core::kInvalidTermId;
  bool value = false;
  bool filtered = false;
};

std::string build_qf_uf_model(core::TermManager& terms,
                              const std::vector<TheoryModelAssignment>& theory_assignments);

}  // namespace primo::smt
