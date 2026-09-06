#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "core/ids.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::smt {

struct TheoryModelAssignment {
  core::TermId term = core::kInvalidTermId;
  bool value = false;
  bool filtered = false;
  bool directed_substitution = false;

  bool operator==(const TheoryModelAssignment&) const = default;
};

struct MonotoneEliminatedVariable {
  core::TermId variable = core::kInvalidTermId;
  bool bounded_above = false;
  std::vector<std::pair<core::TermId, bool>> constraints;

  bool operator==(const MonotoneEliminatedVariable&) const = default;
};

struct BooleanTheoryAssignmentConversion {
  std::vector<TheoryModelAssignment> assignments;

  bool operator==(const BooleanTheoryAssignmentConversion&) const = default;
};

struct DirectedEqualitySubstitutionConversion {
  std::vector<std::pair<core::TermId, core::TermId>> substitutions;

  bool operator==(const DirectedEqualitySubstitutionConversion&) const = default;
};

struct LiaSubstitutionConversion {
  std::vector<std::pair<core::TermId, core::TermId>> substitutions;

  bool operator==(const LiaSubstitutionConversion&) const = default;
};

// The purification map, kept so that model construction can read a UF
// application's value off its label instead of searching for it.
//
// Griggio's thesis sect. 1.5.1: purification labels every alien subterm with a
// fresh variable and asserts the label, so in the purified formula every
// arithmetic-sorted UF application f(v_1..v_n) has a label v_f of its own, and
// all of those are interface variables the arithmetic solver assigns. The row
// is then (val(v_1)..val(v_n)) -> val(v_f). Discarding this map is what forced
// qf_uflia_int_values to rediscover the correspondence, and what
// bugs/qf-uflia-nested-application-model is caused by.
//
// Both the original application and its purified rewriting are recorded, since
// the arena holds both and the model builder walks all of them.
struct PurificationProxyConversion {
  std::vector<std::pair<core::TermId, core::TermId>> application_proxies;

  bool operator==(const PurificationProxyConversion&) const = default;
};

struct MonotoneEliminationConversion {
  std::vector<MonotoneEliminatedVariable> eliminated;

  bool operator==(const MonotoneEliminationConversion&) const = default;
};

using ModelConversionStep =
    std::variant<BooleanTheoryAssignmentConversion, DirectedEqualitySubstitutionConversion,
                 LiaSubstitutionConversion, MonotoneEliminationConversion,
                 PurificationProxyConversion>;

// Persistent execution-ordered postsolve log. Transformations append one typed
// step; model builders replay steps in reverse without regrouping them by kind.
class ModelConversionChain {
 public:
  ModelConversionChain appended(ModelConversionStep step) const;

  bool empty() const { return tail_ == nullptr; }
  std::size_t size() const { return tail_ == nullptr ? 0 : tail_->size; }

  template <typename Visitor>
  void replay_reverse(Visitor&& visitor) const {
    for (std::shared_ptr<const Node> node = tail_; node != nullptr; node = node->previous) {
      std::forward<Visitor>(visitor)(node->step);
    }
  }

  // Compatibility projection for the current string model builders. It keeps
  // execution order and materializes directed and LIA substitutions as
  // equality facts. Mixed models need both: a substituted Int constant can be
  // defined by an application whose numeric value is anchored through EUF.
  std::vector<TheoryModelAssignment> theory_assignments(core::TermManager& terms) const;

  bool operator==(const ModelConversionChain& other) const;

 private:
  struct Node {
    ModelConversionStep step;
    std::shared_ptr<const Node> previous;
    std::size_t size = 0;
  };

  std::shared_ptr<const Node> tail_;
};

// Forced facts are analysis input for the later finite-domain tactic, not model
// conversions, so they remain outside the ordered postsolve chain.
struct ModelReconstruction {
  ModelConversionChain conversions;
  std::vector<std::pair<core::TermId, core::TermId>> forced_equalities;
  std::vector<std::pair<core::TermId, core::TermId>> forced_disequalities;

  bool operator==(const ModelReconstruction&) const = default;
};

}  // namespace primo::smt
