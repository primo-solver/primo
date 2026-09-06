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
  // Counts Int terms the builder had to invent a value for, because neither
  // the component model nor their congruence class supplied one.
  //
  // Inventing is correct for a genuinely unconstrained term and unsound for a
  // constrained one, and the builder cannot tell the two apart -- it never
  // sees the assertions. It is therefore the mechanism behind
  // bugs/qf-uflia-nested-application-model: an invented number becomes both a
  // table key and a result, and the printed table then contradicts the
  // constraint that pinned the term. Nothing used to record that this had
  // happened, so the only way to find out was an external model validator.
  std::size_t* invented_int_values = nullptr;
};

std::string build_qf_uf_model(core::TermManager& terms,
                              const std::vector<TheoryModelAssignment>& theory_assignments,
                              QfUfModelBuildOptions options = {});

}  // namespace primo::smt
