#pragma once

#include <cstddef>
#include <utility>
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
                                                    const std::vector<core::TermId>& assertions,
                                                    std::size_t* consequences_added = nullptr);

struct GroundEqualitySubstitutionResult {
  std::vector<core::TermId> assertions;
  std::size_t units = 0;
  std::size_t rewrites = 0;
};

GroundEqualitySubstitutionResult substitute_ground_unit_equalities(
    core::TermManager& terms, const std::vector<core::TermId>& assertions);

struct RealEqualityLoweringResult {
  std::vector<core::TermId> assertions;
  std::size_t lowered = 0;
};

// Rewrites every Equality node whose operands are Real-sorted into
// `And(Le(a,b), Le(b,a))` (valid since Real is a total order). Must run
// after every other preprocessing pass that reasons about genuine
// Equality nodes (solve-eqs, eq-bridging, forced-atom/unit-equality
// propagation) but before Boolean clausification/theory dispatch, so
// NOT(a=b) still reduces to ordinary Boolean structure
// (NOT(AND(Le,Le))) that BooleanClausifier already handles -- this is
// what lets QF_LRA disequality avoid needing case-split machinery in
// LraTableau/OnlineLraPropagator. Non-Real equalities are left
// untouched.
RealEqualityLoweringResult lower_real_equalities(core::TermManager& terms,
                                                 const std::vector<core::TermId>& assertions);

struct SolvedConstantSubstitutionResult {
  std::vector<core::TermId> assertions;
  std::size_t candidates = 0;
  std::size_t accepted = 0;
  std::size_t rewrites = 0;
  std::size_t rejected_cycles = 0;
  std::size_t rejected_blowups = 0;
  std::size_t gaussian_candidates = 0;
};

// enable_gaussian_elimination additionally row-reduces (Gauss-Jordan,
// exact rationals) the system of asserted Real-sorted linear equalities
// before the ordinary bare-variable-equality candidate scan, catching
// definitions the latter can't (e.g. `2x = 5`, or a variable only solvable
// jointly from several equations). Opt-in (default false in
// CheckSatOptions) since it's materially new algorithmic machinery, not
// yet campaign-fuzzed.
SolvedConstantSubstitutionResult substitute_solved_constants(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    bool enable_gaussian_elimination = false);

struct BooleanUnitPropagationResult {
  std::vector<core::TermId> assertions;
  std::size_t units = 0;
  std::size_t rewrites = 0;
};

BooleanUnitPropagationResult propagate_boolean_units(core::TermManager& terms,
                                                     const std::vector<core::TermId>& assertions);

struct ForcedAtomPropagationResult {
  std::vector<core::TermId> assertions;
  std::vector<std::pair<core::TermId, core::TermId>> equality_pairs;
  std::vector<std::pair<core::TermId, core::TermId>> disequality_pairs;
  std::size_t units = 0;
  std::size_t disequalities = 0;
  std::size_t rewrites = 0;
};

ForcedAtomPropagationResult propagate_forced_atoms(core::TermManager& terms,
                                                   const std::vector<core::TermId>& assertions);

struct FiniteDomainAtMostOneResult {
  std::vector<core::TermId> clauses;
  std::size_t domains = 0;
  std::size_t choices = 0;
};

class FiniteDomainAtMostOne {
 public:
  explicit FiniteDomainAtMostOne(core::TermManager& terms);

  FiniteDomainAtMostOneResult generate(const std::vector<core::TermId>& assertions);
  FiniteDomainAtMostOneResult generate(
      const std::vector<core::TermId>& assertions,
      const std::vector<std::pair<core::TermId, core::TermId>>& additional_disequalities);
  FiniteDomainAtMostOneResult generate(
      const std::vector<core::TermId>& assertions,
      const std::vector<std::pair<core::TermId, core::TermId>>& additional_equalities,
      const std::vector<std::pair<core::TermId, core::TermId>>& additional_disequalities);

 private:
  core::TermManager& terms_;
};

struct FiniteDomainEqualityDefinitionsResult {
  std::vector<core::TermId> clauses;
  std::size_t candidate_term_pairs = 0;
  std::size_t term_pairs = 0;
  std::size_t predicted_clauses = 0;
  std::size_t rejected_domain_size = 0;
  std::size_t rejected_clause_limit = 0;
};

struct FiniteDomainEqualityDefinitionsOptions {
  std::size_t max_domain_size = 0;
  std::size_t max_clauses = 0;
};

class FiniteDomainEqualityDefinitions {
 public:
  explicit FiniteDomainEqualityDefinitions(core::TermManager& terms);

  FiniteDomainEqualityDefinitionsResult generate(
      const std::vector<core::TermId>& assertions,
      FiniteDomainEqualityDefinitionsOptions options = {});

 private:
  core::TermManager& terms_;
};

struct CongruenceDefinitionResult {
  std::vector<core::TermId> clauses;
  std::size_t groups = 0;
  std::size_t skipped_groups = 0;
};

class CongruenceDefinitionGenerator {
 public:
  explicit CongruenceDefinitionGenerator(core::TermManager& terms);

  CongruenceDefinitionResult generate(const std::vector<core::TermId>& assertions);

 private:
  core::TermManager& terms_;
};

struct FiniteDomainEvaluationResult {
  std::vector<core::TermId> assertions;
  std::size_t equalities_simplified = 0;
  std::size_t enumeration_cutoffs = 0;
};

class FiniteDomainEvaluator {
 public:
  explicit FiniteDomainEvaluator(core::TermManager& terms);

  FiniteDomainEvaluationResult simplify(const std::vector<core::TermId>& assertions);

 private:
  core::TermManager& terms_;
};

struct UfSymmetryBreakingResult {
  std::vector<core::TermId> constraints;
  std::size_t accepted_sets = 0;
  std::size_t values = 0;
  std::size_t terms = 0;
  std::size_t value_precedence_terms = 0;
  std::size_t value_precedence_constraints = 0;
  std::size_t rejected_noninvariant = 0;
};

struct UfSymmetryBreakingOptions {
  bool value_precedence = true;
};

class UfSymmetryBreaker {
 public:
  explicit UfSymmetryBreaker(core::TermManager& terms);

  UfSymmetryBreakingResult break_symmetries(const std::vector<core::TermId>& assertions,
                                            UfSymmetryBreakingOptions options = {});

 private:
  core::TermManager& terms_;
};

}  // namespace primo::smt
