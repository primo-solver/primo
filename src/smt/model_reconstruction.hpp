#pragma once

#include <utility>
#include <vector>

#include "core/ids.hpp"

namespace primo::smt {

// A Boolean or equality assignment removed by preprocessing and required when
// extending a model of the transformed goal to the original vocabulary.
struct TheoryModelAssignment {
  core::TermId term = core::kInvalidTermId;
  bool value = false;
  bool filtered = false;
  bool directed_substitution = false;

  bool operator==(const TheoryModelAssignment&) const = default;
};

// One real variable removed by monotone (one-sided) elimination, together
// with the constraints used to choose a value when reconstructing a model.
struct MonotoneEliminatedVariable {
  core::TermId variable = core::kInvalidTermId;
  bool bounded_above = false;
  std::vector<std::pair<core::TermId, bool>> constraints;

  bool operator==(const MonotoneEliminatedVariable&) const = default;
};

// Goal-local records produced by equisatisfiable preprocessing. A tactic
// constructs a new value and installs it with Goal::with_reconstruction; a
// goal never exposes mutable access to the value it owns.
struct ModelReconstruction {
  std::vector<TheoryModelAssignment> model_assignments;
  std::vector<MonotoneEliminatedVariable> monotone_eliminated;
  std::vector<std::pair<core::TermId, core::TermId>> lia_substitutions;
  std::vector<std::pair<core::TermId, core::TermId>> forced_equalities;
  std::vector<std::pair<core::TermId, core::TermId>> forced_disequalities;

  bool operator==(const ModelReconstruction&) const = default;
};

}  // namespace primo::smt
