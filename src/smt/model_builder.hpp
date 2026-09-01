#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/term_manager.hpp"
#include "smt/model_reconstruction.hpp"
#include "util/rational.hpp"

namespace primo::smt {

struct QfUfModelBuildOptions {
  bool include_model_wrapper = true;
  bool skip_real_symbols = false;
  bool skip_real_constants = false;
  const std::unordered_map<core::TermId, util::Rational>* real_values = nullptr;
  const std::unordered_map<core::TermId, util::Rational>* int_values = nullptr;
};

std::string build_qf_uf_model(core::TermManager& terms,
                              const std::vector<TheoryModelAssignment>& theory_assignments,
                              QfUfModelBuildOptions options = {});

}  // namespace primo::smt
