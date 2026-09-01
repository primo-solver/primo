#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/ids.hpp"
#include "sat/clause.hpp"
#include "sat/lit.hpp"

namespace primo::theory {

// Properties needed by an SMT-owned combination coordinator.  They are
// declarations about the implemented fragment, not runtime search state.
enum class StableInfiniteness {
  No,
  Yes,
};

enum class Convexity {
  Convex,
  NonConvex,
};

struct TheoryProperties {
  std::string_view id;
  std::vector<core::SortId> shared_sorts;
  StableInfiniteness stably_infinite = StableInfiniteness::No;
  Convexity convexity = Convexity::NonConvex;
  bool produces_model_projection = false;
  bool propagates_equalities = false;
  bool supplies_care_pairs = false;
};

// One SAT assignment projected onto an atom owned by a component theory.
// `lit` is the positive literal naming `atom`; `value` is its assigned truth
// value.  Explanations returned by a component are clauses over these literals.
struct AssignedTheoryLiteral {
  core::TermId atom = core::kInvalidTermId;
  sat::Lit lit;
  bool value = false;
};

enum class InterfaceRelationValue {
  Equal,
  Disequal,
};

// A complete-arrangement policy supplies one relation for every same-sort
// shared pair.  Convex policies may supply only the relations already fixed by
// SAT or explained theory propagation.
struct InterfaceRelation {
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;
  core::SortId sort = core::kInvalidSortId;
  sat::Lit equality_lit;
  InterfaceRelationValue value = InterfaceRelationValue::Equal;
};

struct SharedModelClass {
  std::vector<core::TermId> terms;
};

// Opaque freshness stamp for a theory-private model.  The SMT coordinator may
// compare and validate this stamp but never dereferences the component model.
struct TheoryModelToken {
  std::string component;
  std::uint64_t assignment_epoch = 0;
  std::uint64_t revision = 0;

  bool operator==(const TheoryModelToken&) const = default;
};

struct SharedModelPartition {
  core::SortId sort = core::kInvalidSortId;
  std::vector<SharedModelClass> equality_classes;
};

struct TheoryModelProjection {
  TheoryModelToken token;
  std::vector<SharedModelPartition> partitions;
};

struct InterfacePair {
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;
  core::SortId sort = core::kInvalidSortId;

  bool operator==(const InterfacePair&) const = default;
};

struct TheoryDecisionRequest {
  InterfacePair pair;
  InterfaceRelationValue preferred_value = InterfaceRelationValue::Equal;
  std::vector<sat::Lit> reasons;
};

enum class TheoryCheckStatus {
  Consistent,
  LearnedClause,
  EntailedLiteral,
  FreshDecision,
  ResourceLimit,
  Unsupported,
  Cancelled,
  Unknown,
};

// A learned clause includes both conflicts and propagations.  Keeping a
// general SAT clause here is what permits non-convex components to return a
// disjunction without pretending that one equality is individually entailed.
struct TheoryCheckResult {
  TheoryCheckStatus status = TheoryCheckStatus::Unknown;
  sat::Clause clause;
  TheoryModelProjection model;
  sat::Lit entailed_lit;
  std::vector<sat::Lit> reasons;
  TheoryDecisionRequest decision;
  std::string diagnostic;
};

struct TheoryMutationRequest {
  TheoryModelToken source;
  std::vector<InterfaceRelation> required_relations;
};

enum class TheoryMutationStatus {
  Mutated,
  FixedPoint,
  ResourceLimit,
  Unsupported,
  Cancelled,
  Unknown,
};

struct TheoryMutationResult {
  TheoryMutationStatus status = TheoryMutationStatus::FixedPoint;
  TheoryModelProjection model;
  std::string diagnostic;
};

// Solve-scoped component interface.  The SMT layer owns purification, SAT,
// interface arrangements and scheduling; an implementation owns only its
// theory-local prepared state and receives no SAT backend handle.
class TheorySolver {
 public:
  virtual ~TheorySolver() = default;

  [[nodiscard]] virtual const TheoryProperties& properties() const = 0;
  [[nodiscard]] virtual TheoryCheckResult check(std::span<const AssignedTheoryLiteral> assignment,
                                                std::span<const InterfaceRelation> arrangement) = 0;
  [[nodiscard]] virtual TheoryMutationResult mutate(const TheoryMutationRequest&) {
    return TheoryMutationResult{};
  }
  [[nodiscard]] virtual std::vector<InterfacePair> care_pairs(const TheoryModelToken&) const {
    return {};
  }
};

}  // namespace primo::theory
