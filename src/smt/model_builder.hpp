#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/term_manager.hpp"
#include "util/rational.hpp"

namespace primo::smt {

struct TheoryModelAssignment {
  core::TermId term = core::kInvalidTermId;
  bool value = false;
  bool filtered = false;
  bool directed_substitution = false;
};

struct QfUfModelBuildOptions {
  bool include_model_wrapper = true;
  bool skip_real_symbols = false;
  bool skip_real_constants = false;
  const std::unordered_map<core::TermId, util::Rational>* real_values = nullptr;
};

std::string build_qf_uf_model(core::TermManager& terms,
                              const std::vector<TheoryModelAssignment>& theory_assignments,
                              QfUfModelBuildOptions options = {});

}  // namespace primo::smt
