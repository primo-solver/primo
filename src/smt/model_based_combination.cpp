#include "smt/model_based_combination.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace primo::smt {
namespace {

using theory::InterfacePair;
using theory::InterfaceRelation;
using theory::InterfaceRelationValue;
using theory::SharedModelPartition;
using theory::TheoryModelProjection;

ModelBasedCombinationResult result_with_status(ModelBasedCombinationStatus status) {
  ModelBasedCombinationResult result;
  result.status = status;
  return result;
}

std::uint64_t pair_key(core::TermId lhs, core::TermId rhs) {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return (static_cast<std::uint64_t>(lhs) << 32U) | static_cast<std::uint64_t>(rhs);
}

bool projection_equal(const TheoryModelProjection& projection, const InterfacePair& pair) {
  const auto partition = std::find_if(
      projection.partitions.begin(), projection.partitions.end(),
      [&pair](const SharedModelPartition& candidate) { return candidate.sort == pair.sort; });
  if (partition == projection.partitions.end()) {
    return false;
  }
  for (const theory::SharedModelClass& model_class : partition->equality_classes) {
    const bool has_lhs = std::ranges::find(model_class.terms, pair.lhs) != model_class.terms.end();
    const bool has_rhs = std::ranges::find(model_class.terms, pair.rhs) != model_class.terms.end();
    if (has_lhs || has_rhs) {
      return has_lhs && has_rhs;
    }
  }
  return false;
}

bool token_is_current(const TheoryModelProjection& projection, std::string_view component,
                      std::uint64_t assignment_epoch) {
  return projection.token.component == component &&
         projection.token.assignment_epoch == assignment_epoch;
}

bool projection_is_complete(const TheoryModelProjection& projection,
                            std::span<const InterfacePair> shared_pairs,
                            std::span<const core::SortId> shared_sorts) {
  std::unordered_set<core::SortId> seen_sorts;
  std::unordered_set<core::TermId> seen_terms;
  for (const SharedModelPartition& partition : projection.partitions) {
    if (std::ranges::find(shared_sorts, partition.sort) == shared_sorts.end() ||
        !seen_sorts.insert(partition.sort).second) {
      return false;
    }
    seen_terms.clear();
    for (const theory::SharedModelClass& model_class : partition.equality_classes) {
      if (model_class.terms.empty()) {
        return false;
      }
      for (const core::TermId term : model_class.terms) {
        if (!seen_terms.insert(term).second) {
          return false;
        }
      }
    }
    for (const InterfacePair& pair : shared_pairs) {
      if (pair.sort == partition.sort &&
          (!seen_terms.contains(pair.lhs) || !seen_terms.contains(pair.rhs))) {
        return false;
      }
    }
  }
  return std::ranges::all_of(shared_pairs, [&seen_sorts, shared_sorts](const InterfacePair& pair) {
    return std::ranges::find(shared_sorts, pair.sort) == shared_sorts.end() ||
           seen_sorts.contains(pair.sort);
  });
}

bool respects_relations(const TheoryModelProjection& projection,
                        std::span<const InterfaceRelation> arrangement,
                        std::span<const core::SortId> shared_sorts) {
  return std::ranges::all_of(arrangement, [&projection,
                                           shared_sorts](const InterfaceRelation& relation) {
    if (std::ranges::find(shared_sorts, relation.sort) == shared_sorts.end()) {
      return true;
    }
    const InterfacePair pair{relation.lhs, relation.rhs, relation.sort};
    return projection_equal(projection, pair) == (relation.value == InterfaceRelationValue::Equal);
  });
}

bool is_strict_refinement(const TheoryModelProjection& before, const TheoryModelProjection& after,
                          std::span<const InterfacePair> shared_pairs) {
  bool split = false;
  for (const InterfacePair& pair : shared_pairs) {
    const bool before_equal = projection_equal(before, pair);
    const bool after_equal = projection_equal(after, pair);
    if (!before_equal && after_equal) {
      return false;
    }
    split = split || (before_equal && !after_equal);
  }
  return split;
}

ModelBasedCombinationResult stop_result(theory::TheoryCheckStatus status, std::string diagnostic) {
  ModelBasedCombinationStatus mapped = ModelBasedCombinationStatus::Unknown;
  switch (status) {
    case theory::TheoryCheckStatus::ResourceLimit:
      mapped = ModelBasedCombinationStatus::ResourceLimit;
      break;
    case theory::TheoryCheckStatus::Unsupported:
      mapped = ModelBasedCombinationStatus::Unsupported;
      break;
    case theory::TheoryCheckStatus::Cancelled:
      mapped = ModelBasedCombinationStatus::Cancelled;
      break;
    case theory::TheoryCheckStatus::Unknown:
      mapped = ModelBasedCombinationStatus::Unknown;
      break;
    default:
      throw std::logic_error("non-stop theory status passed to stop_result");
  }
  ModelBasedCombinationResult result = result_with_status(mapped);
  result.diagnostic = std::move(diagnostic);
  return result;
}

ModelBasedCombinationResult stop_result(theory::TheoryMutationStatus status,
                                        std::string diagnostic) {
  ModelBasedCombinationStatus mapped = ModelBasedCombinationStatus::Unknown;
  switch (status) {
    case theory::TheoryMutationStatus::ResourceLimit:
      mapped = ModelBasedCombinationStatus::ResourceLimit;
      break;
    case theory::TheoryMutationStatus::Unsupported:
      mapped = ModelBasedCombinationStatus::Unsupported;
      break;
    case theory::TheoryMutationStatus::Cancelled:
      mapped = ModelBasedCombinationStatus::Cancelled;
      break;
    case theory::TheoryMutationStatus::Unknown:
      mapped = ModelBasedCombinationStatus::Unknown;
      break;
    default:
      throw std::logic_error("non-stop mutation status passed to stop_result");
  }
  ModelBasedCombinationResult result = result_with_status(mapped);
  result.diagnostic = std::move(diagnostic);
  return result;
}

}  // namespace

ModelBasedCombinationResult ModelBasedCombinationCoordinator::reconcile(
    std::uint64_t assignment_epoch, std::span<const ModelBasedComponentRound> components,
    std::span<const theory::InterfaceRelation> arrangement,
    std::span<const theory::InterfacePair> shared_pairs,
    const EqualityLiteralResolver& equality_literal) {
  std::vector<theory::TheoryModelProjection> projections;
  projections.reserve(components.size());

  for (const ModelBasedComponentRound& component : components) {
    if (component.solver == nullptr) {
      throw std::invalid_argument("model-based combination component is null");
    }
    ++statistics_.component_checks;
    theory::TheoryCheckResult check = component.solver->check(component.assignment, arrangement);
    switch (check.status) {
      case theory::TheoryCheckStatus::Consistent:
        break;
      case theory::TheoryCheckStatus::LearnedClause:
        ++statistics_.learned_clauses;
        {
          ModelBasedCombinationResult result =
              result_with_status(ModelBasedCombinationStatus::LearnClause);
          result.clause = std::move(check.clause);
          return result;
        }
      case theory::TheoryCheckStatus::EntailedLiteral: {
        sat::Clause clause;
        clause.reserve(check.reasons.size() + 1);
        for (const sat::Lit reason : check.reasons) {
          clause.push_back(sat::Lit{-reason.value});
        }
        clause.push_back(check.entailed_lit);
        ++statistics_.learned_clauses;
        ModelBasedCombinationResult result =
            result_with_status(ModelBasedCombinationStatus::LearnClause);
        result.clause = std::move(clause);
        return result;
      }
      case theory::TheoryCheckStatus::FreshDecision:
        ++statistics_.fresh_decisions;
        {
          ModelBasedCombinationResult result =
              result_with_status(ModelBasedCombinationStatus::FreshDecision);
          result.decision = std::move(check.decision);
          return result;
        }
      case theory::TheoryCheckStatus::ResourceLimit:
      case theory::TheoryCheckStatus::Unsupported:
      case theory::TheoryCheckStatus::Cancelled:
      case theory::TheoryCheckStatus::Unknown:
        return stop_result(check.status, std::move(check.diagnostic));
    }

    if (!component.solver->properties().produces_model_projection) {
      ModelBasedCombinationResult result =
          result_with_status(ModelBasedCombinationStatus::Unsupported);
      result.diagnostic = "component does not provide a shared model projection";
      return result;
    }
    if (!token_is_current(check.model, component.solver->properties().id, assignment_epoch)) {
      ++statistics_.stale_snapshots;
      ModelBasedCombinationResult result = result_with_status(ModelBasedCombinationStatus::Unknown);
      result.diagnostic = "stale theory model snapshot";
      return result;
    }
    if (!projection_is_complete(check.model, shared_pairs,
                                component.solver->properties().shared_sorts)) {
      ModelBasedCombinationResult result = result_with_status(ModelBasedCombinationStatus::Unknown);
      result.diagnostic = "incomplete or malformed theory model projection";
      return result;
    }
    if (!respects_relations(check.model, arrangement,
                            component.solver->properties().shared_sorts)) {
      ModelBasedCombinationResult result = result_with_status(ModelBasedCombinationStatus::Unknown);
      result.diagnostic = "theory model snapshot violates the SAT arrangement";
      return result;
    }
    ++statistics_.snapshots;
    projections.push_back(std::move(check.model));
  }

  if (projections.empty()) {
    return result_with_status(ModelBasedCombinationStatus::Accept);
  }

  std::unordered_set<std::uint64_t> care_pair_keys;
  for (std::size_t component_index = 0; component_index < components.size(); ++component_index) {
    for (const InterfacePair& pair :
         components[component_index].solver->care_pairs(projections[component_index].token)) {
      care_pair_keys.insert(pair_key(pair.lhs, pair.rhs));
    }
  }
  statistics_.care_pairs += care_pair_keys.size();
  std::vector<InterfacePair> ordered_pairs(shared_pairs.begin(), shared_pairs.end());
  std::stable_sort(ordered_pairs.begin(), ordered_pairs.end(),
                   [&care_pair_keys](const InterfacePair& lhs, const InterfacePair& rhs) {
                     return care_pair_keys.contains(pair_key(lhs.lhs, lhs.rhs)) >
                            care_pair_keys.contains(pair_key(rhs.lhs, rhs.rhs));
                   });

  for (std::size_t component_index = 0; component_index < components.size(); ++component_index) {
    std::vector<InterfaceRelation> required_relations(arrangement.begin(), arrangement.end());
    bool can_refine = false;
    for (const InterfacePair& pair : ordered_pairs) {
      const bool reference_equal = projection_equal(projections.front(), pair);
      const bool component_equal = projection_equal(projections[component_index], pair);
      if (component_equal && !reference_equal) {
        required_relations.push_back(InterfaceRelation{.lhs = pair.lhs,
                                                       .rhs = pair.rhs,
                                                       .sort = pair.sort,
                                                       .equality_lit = {},
                                                       .value = InterfaceRelationValue::Disequal});
        can_refine = true;
      }
    }
    if (!can_refine) {
      continue;
    }

    theory::TheoryMutationRequest request{.source = projections[component_index].token,
                                          .required_relations = std::move(required_relations)};
    ++statistics_.mutations;
    theory::TheoryMutationResult mutation = components[component_index].solver->mutate(request);
    if (mutation.status == theory::TheoryMutationStatus::Mutated) {
      if (!token_is_current(mutation.model, components[component_index].solver->properties().id,
                            assignment_epoch) ||
          mutation.model.token.revision <= projections[component_index].token.revision ||
          !projection_is_complete(mutation.model, ordered_pairs,
                                  components[component_index].solver->properties().shared_sorts) ||
          !is_strict_refinement(projections[component_index], mutation.model, ordered_pairs) ||
          !respects_relations(mutation.model, request.required_relations,
                              components[component_index].solver->properties().shared_sorts)) {
        ++statistics_.rejected_mutations;
        ModelBasedCombinationResult result =
            result_with_status(ModelBasedCombinationStatus::Unknown);
        result.diagnostic = "invalid theory model mutation";
        return result;
      }
      projections[component_index] = std::move(mutation.model);
      ++statistics_.snapshots;
      continue;
    }
    if (mutation.status == theory::TheoryMutationStatus::FixedPoint) {
      ++statistics_.fixed_points;
      continue;
    }
    return stop_result(mutation.status, std::move(mutation.diagnostic));
  }

  for (const InterfacePair& pair : ordered_pairs) {
    const bool first_value = projection_equal(projections.front(), pair);
    const bool agreement =
        std::ranges::all_of(projections, [&pair, first_value](const auto& model) {
          return projection_equal(model, pair) == first_value;
        });
    if (agreement) {
      continue;
    }
    ++statistics_.disagreements;
    const std::optional<sat::Lit> literal = equality_literal(pair);
    if (!literal.has_value()) {
      ModelBasedCombinationResult result =
          result_with_status(ModelBasedCombinationStatus::Unsupported);
      result.diagnostic = "cannot create SAT literal for shared model equality";
      return result;
    }
    ++statistics_.model_decisions;
    ModelBasedCombinationResult result =
        result_with_status(ModelBasedCombinationStatus::DecideEquality);
    result.decision_lit = literal.value();
    return result;
  }

  return result_with_status(ModelBasedCombinationStatus::Accept);
}

}  // namespace primo::smt
