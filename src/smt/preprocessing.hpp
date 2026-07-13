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
                                                    const std::vector<core::TermId>& assertions,
                                                    std::size_t* consequences_added = nullptr);

struct BooleanUnitPropagationResult {
  std::vector<core::TermId> assertions;
  std::size_t units = 0;
  std::size_t rewrites = 0;
};

BooleanUnitPropagationResult propagate_boolean_units(core::TermManager& terms,
                                                     const std::vector<core::TermId>& assertions);

struct FiniteDomainAtMostOneResult {
  std::vector<core::TermId> clauses;
  std::size_t domains = 0;
};

class FiniteDomainAtMostOne {
 public:
  explicit FiniteDomainAtMostOne(core::TermManager& terms);

  FiniteDomainAtMostOneResult generate(const std::vector<core::TermId>& assertions);

 private:
  core::TermManager& terms_;
};

struct FiniteDomainEqualityDefinitionsResult {
  std::vector<core::TermId> clauses;
  std::size_t term_pairs = 0;
};

class FiniteDomainEqualityDefinitions {
 public:
  explicit FiniteDomainEqualityDefinitions(core::TermManager& terms);

  FiniteDomainEqualityDefinitionsResult generate(const std::vector<core::TermId>& assertions);

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
