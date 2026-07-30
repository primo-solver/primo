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

struct QfUfModelBuildOptions {
  bool include_model_wrapper = true;
  bool skip_real_symbols = false;
};

std::string build_qf_uf_model(core::TermManager& terms,
                              const std::vector<TheoryModelAssignment>& theory_assignments,
                              QfUfModelBuildOptions options = {});

}  // namespace primo::smt
