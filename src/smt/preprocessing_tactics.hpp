#pragma once

#include "smt/tactic.hpp"

namespace primo::smt {

// Immutable settings for one equivalence/equisatisfiability-preserving
// preprocessing round. Strategy construction maps command-line options into
// this value once; the tactic itself does not inspect CLI state.
struct PreprocessingRoundOptions {
  bool produce_model = false;
  bool solved_constant_substitution = false;
  bool gaussian_elimination = false;
  bool use_eq_bridge = false;
  bool use_finite_domain_evaluation = false;
  bool use_ground_congruence_definitions = false;
  bool use_ground_unit_substitution = false;
  bool use_guarded_ground_unit_substitution = false;
};

Tactic make_preprocessing_round(PreprocessingRoundOptions options);

}  // namespace primo::smt
