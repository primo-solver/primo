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

struct UnitEqualityPropagationStats {
  std::size_t consequences_added = 0;
  std::size_t query_nodes = 0;
  std::size_t query_memo_hits = 0;
  std::size_t add_term_requests = 0;
  std::size_t add_term_seen_hits = 0;
};

std::vector<core::TermId> propagate_unit_equalities(core::TermManager& terms,
                                                    const std::vector<core::TermId>& assertions,
                                                    UnitEqualityPropagationStats* stats = nullptr);

struct GroundEqualitySubstitutionResult {
  std::vector<core::TermId> assertions;
  std::vector<std::pair<core::TermId, core::TermId>> substitutions;
  std::size_t units = 0;
  std::size_t rewrites = 0;
};

GroundEqualitySubstitutionResult substitute_ground_unit_equalities(
    core::TermManager& terms, const std::vector<core::TermId>& assertions);

struct RealEqualityLoweringResult {
  std::vector<core::TermId> assertions;
  std::size_t lowered = 0;
  std::size_t guarded_distributed = 0;
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
RealEqualityLoweringResult lower_real_equalities(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    bool enable_guarded_real_equality_distribution = false);

struct SolvedConstantSubstitutionResult {
  std::vector<core::TermId> assertions;
  std::vector<std::pair<core::TermId, core::TermId>> substitutions;
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

// One real variable removed by monotone (one-sided) elimination, together
// with everything needed to give it a value again when a model is printed.
struct MonotoneEliminatedVariable {
  core::TermId variable = core::kInvalidTermId;
  // True when every discharged constraint bounds the variable from above,
  // so an arbitrarily small value satisfies all of them; false for the
  // mirror case.
  bool bounded_above = false;
  // The atoms discharged for this variable, each paired with the truth
  // value that occurrence has to take. Model reconstruction re-derives a
  // satisfying value from exactly these.
  std::vector<std::pair<core::TermId, bool>> constraints;
};

struct MonotoneEliminationResult {
  std::vector<core::TermId> assertions;
  // Sorted by TermId for reproducibility. No dependency order is needed:
  // whichever eliminated variable of a discharged atom is valued last sees
  // every other leaf already valued, and repairs that atom.
  std::vector<MonotoneEliminatedVariable> eliminated;
  std::size_t candidates = 0;
  std::size_t atoms_folded = 0;
};

// Eliminates real variables that occur *one-sidedly*: if, after accounting
// for Boolean polarity, every atom mentioning `x` pushes it the same
// direction, then some extreme value of `x` satisfies all of them at once,
// and each such atom can be replaced by the constant its own occurrence
// needs (true at positive polarity, false at negative).
//
// This is Fourier-Motzkin elimination of a single-sign variable: with no
// opposite bounds to pair against, the pairwise combination step is empty
// and every constraint mentioning the variable is simply discharged. The
// saving is mostly Boolean -- the atom leaves the CNF and the clauses it
// satisfies disappear -- and only secondarily a smaller tableau.
//
// Deliberately conservative. A variable is disqualified unless it is a
// 0-ary Real `Constant` whose every occurrence is a linear leaf of a
// Le/Lt atom: appearing under an uninterpreted application, inside a Real
// equality, or at mixed polarity all force it two-sided. An atom carrying
// any leaf that model reconstruction could not evaluate disqualifies
// every variable in it, so a discharged constraint can always be repaired
// when the model is built.
MonotoneEliminationResult eliminate_monotone_variables(core::TermManager& terms,
                                                       const std::vector<core::TermId>& assertions);

struct BooleanUnitPropagationResult {
  std::vector<core::TermId> assertions;
  std::vector<std::pair<core::TermId, bool>> assignments;
  std::size_t units = 0;
  std::size_t rewrites = 0;
  std::size_t memo_hits = 0;
  std::size_t memo_misses = 0;
};

BooleanUnitPropagationResult propagate_boolean_units(core::TermManager& terms,
                                                     const std::vector<core::TermId>& assertions);

struct ForcedAtomPropagationResult {
  std::vector<core::TermId> assertions;
  std::vector<std::pair<core::TermId, bool>> assignments;
  std::vector<std::pair<core::TermId, core::TermId>> equality_pairs;
  std::vector<std::pair<core::TermId, core::TermId>> disequality_pairs;
  std::size_t units = 0;
  std::size_t disequalities = 0;
  std::size_t rewrites = 0;
  std::size_t memo_hits = 0;
  std::size_t memo_misses = 0;
  std::size_t query_nodes = 0;
  std::size_t query_memo_hits = 0;
  std::size_t add_term_requests = 0;
  std::size_t add_term_seen_hits = 0;
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
