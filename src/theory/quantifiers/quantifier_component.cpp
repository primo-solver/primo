#include "theory/quantifiers/quantifier_component.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "core/binders.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "util/statistics.hpp"

namespace primo::theory {

namespace {

bool is_quantifier(const core::Term& node) {
  return node.kind == core::TermKind::Forall || node.kind == core::TermKind::Exists;
}

}  // namespace

QuantifierComponent::QuantifierComponent(core::TermManager& terms,
                                         std::span<const core::TermId> ground_roots,
                                         FreshConstantFactory fresh_constant,
                                         QuantifierInstantiationOptions options,
                                         util::Statistics& statistics)
    : terms_(terms),
      fresh_constant_(std::move(fresh_constant)),
      options_(options),
      statistics_(statistics),
      simplifier_(terms) {
  descriptor_.id = "quantifiers";
  descriptor_.fragment = "enumerative instantiation of quantified formulas";
  descriptor_.convexity = Convexity::Convex;
  descriptor_.capabilities.incremental = true;
  descriptor_.capabilities.check_on_full_model = true;
  // Bool's domain is exactly {true, false}; no other Bool term is needed.
  auto& booleans = pools_[terms_.symbols().bool_sort()];
  booleans.push_back(terms_.mk_bool(false));
  booleans.push_back(terms_.mk_bool(true));
  for (const core::TermId root : ground_roots) {
    add_pool_terms(root);
  }
}

bool QuantifierComponent::owns_atom(core::TermId atom) const {
  return is_quantifier(terms_.get(atom));
}

std::vector<core::TermId> QuantifierComponent::interface_variables(core::TermId) const {
  return {};
}

void QuantifierComponent::register_interface(std::span<const ComponentInterfaceScope> scopes) {
  if (!scopes.empty()) {
    throw std::logic_error("the quantifier component shares no interface terms");
  }
}

void QuantifierComponent::register_atoms(std::span<const ComponentAtom> atoms) {
  // Growth happens at a detached boundary; the next dispatcher replays the
  // assignment from scratch, as for every other component.
  reset();
  registered_atoms_ += atoms.size();
}

core::TermId QuantifierComponent::encode_equality(const InterfacePair&) {
  throw std::logic_error("the quantifier component shares no interface terms");
}

void QuantifierComponent::reset() {
  trail_.clear();
  levels_.clear();
}

void QuantifierComponent::push_level() { levels_.push_back(trail_.size()); }

void QuantifierComponent::pop_to_level(std::size_t level) {
  if (level < levels_.size()) {
    trail_.resize(levels_[level]);
    levels_.resize(level);
  }
}

void QuantifierComponent::assign(std::span<const AssignedTheoryLiteral> delta) {
  trail_.insert(trail_.end(), delta.begin(), delta.end());
}

TheoryCheckResult QuantifierComponent::check(TheoryCheckEffort effort,
                                             std::uint64_t assignment_epoch,
                                             std::span<const InterfaceRelation>) {
  statistics_.increment(":quant.checks");
  TheoryCheckResult result;
  result.model.token = {descriptor_.id, assignment_epoch, ++revision_};
  if (!queue_.empty()) {
    result.status = TheoryCheckStatus::LemmasPending;
    return result;
  }
  const bool full = effort == TheoryCheckEffort::FullModel;
  // Instances are chosen once every quantifier atom is assigned (section 2's loop).
  if (trail_.size() < registered_atoms_) {
    result.status = full ? TheoryCheckStatus::Unknown : TheoryCheckStatus::Consistent;
    if (full) result.diagnostic = "a full model left quantifier atoms unassigned";
    return result;
  }

  std::vector<std::pair<core::TermId, bool>> universal;
  bool uncovered = false;
  for (const AssignedTheoryLiteral& assigned : trail_) {
    const bool exists = terms_.get(assigned.atom).kind == core::TermKind::Exists;
    if (exists != assigned.value) {
      universal.emplace_back(assigned.atom, assigned.value);
    } else if (full) {
      uncovered = uncovered || !skolemized_.contains({assigned.atom, assigned.value});
    } else {
      skolemize(assigned.atom, assigned.value);
    }
  }

  if (full) {
    // The verdict on an accepted model; never new lemmas. `sat` needs every
    // assigned atom covered by a Skolem lemma, which SAT has satisfied.
    if (universal.empty() && !uncovered) {
      result.status = TheoryCheckStatus::Consistent;
    } else if (instances_ >= options_.max_instances) {
      result.status = TheoryCheckStatus::ResourceLimit;
      result.diagnostic = "quantifier instantiation budget reached";
    } else {
      statistics_.increment(":quant.exhausted");
      result.status = TheoryCheckStatus::Unknown;
      result.diagnostic = "enumerative instantiation found no new instance";
    }
    return result;
  }

  if (!universal.empty()) {
    // Fairness across quantifiers: a rotating start and an even share first.
    const std::size_t share =
        std::max<std::size_t>(1, options_.instances_per_round / universal.size());
    std::size_t remaining = options_.instances_per_round;
    for (std::size_t step = 0; step < universal.size() && remaining > 0; ++step) {
      const auto& [atom, value] = universal[(next_obligation_ + step) % universal.size()];
      remaining -= enumerate(atom, value, std::min(share, remaining));
    }
    next_obligation_ = (next_obligation_ + 1) % universal.size();
  }
  if (!queue_.empty()) {
    statistics_.increment(":quant.rounds");
    result.status = TheoryCheckStatus::LemmasPending;
    return result;
  }
  // Nothing new for this assignment. A Partial check may run before SAT has
  // finished, so failing here could stop the search short of a ground conflict
  // or of a Skolem-covered assignment; the FullModel check gives the verdict.
  result.status = TheoryCheckStatus::Consistent;
  return result;
}

std::vector<core::TermId> QuantifierComponent::take_lemmas(std::size_t limit) {
  std::vector<core::TermId> taken;
  while (!queue_.empty() && taken.size() < limit) {
    taken.push_back(queue_.front());
    queue_.pop_front();
  }
  // Handed-over lemmas become assertions; their ground terms join the pools.
  for (const core::TermId lemma : taken) {
    add_pool_terms(lemma);
  }
  statistics_.increment(":quant.lemmas", taken.size());
  return taken;
}

ComponentModelFragment QuantifierComponent::export_model(const TheoryModelToken&) const {
  throw std::logic_error("quantified formulas have no model export");
}

void QuantifierComponent::add_pool_terms(core::TermId root) {
  const core::SortId boolean = terms_.symbols().bool_sort();
  std::vector<core::TermId> pending{root};
  while (!pending.empty()) {
    const core::TermId current = pending.back();
    pending.pop_back();
    if (!walked_.insert(current).second) {
      continue;
    }
    const core::Term& node = terms_.get(current);
    if (is_quantifier(node) || node.kind == core::TermKind::Pattern) {
      continue;
    }
    if ((node.kind == core::TermKind::Constant || node.kind == core::TermKind::Application) &&
        node.sort != boolean && terms_.loose_index(current) == 0) {
      pools_[node.sort].push_back(current);
      statistics_.increment(":quant.pool-terms");
    }
    pending.insert(pending.end(), node.children.begin(), node.children.end());
  }
}

const std::vector<core::TermId>& QuantifierComponent::pool(core::SortId sort) {
  auto& terms = pools_[sort];
  if (terms.empty()) {
    // Theorem 1 assumes a term of each sort; supply one for an empty sort.
    terms.push_back(fresh_constant_(sort, "herbrand"));
  }
  return terms;
}

core::TermId QuantifierComponent::canonical(core::TermId term) {
  return options_.simplify_instances ? simplifier_.simplify(term) : term;
}

bool QuantifierComponent::add_instance(core::TermId atom, bool value, core::TermId instance) {
  // Different tuples can yield the same instance, for example when a variable
  // does not occur in the body.
  if (!raw_instances_.insert({atom, value, instance}).second) {
    statistics_.increment(":quant.instances.raw-duplicates");
    return false;
  }
  if (options_.simplify_instances) {
    const core::TermId simplified = simplifier_.simplify(instance);
    if (simplified != instance) {
      statistics_.increment(":quant.instances.simplified");
    }
    // Distinct as instantiated, equal after normalization: generated before.
    if (!canonical_instances_.insert({atom, value, simplified}).second) {
      statistics_.increment(":quant.instances.simplified-duplicates");
      return false;
    }
    instance = simplified;
  }
  // A true atom's lemma is `not Q or inst` and a false one's `Q or not inst`;
  // either is valid outright when the instance has that value.
  if (const core::Term& node = terms_.get(instance);
      node.kind == core::TermKind::BoolValue && node.bool_value == value) {
    statistics_.increment(":quant.instances.trivial");
    return false;
  }
  if (!enqueue(canonical(lemma_for(atom, value, instance)))) {
    statistics_.increment(":quant.instances.duplicate-lemmas");
    return false;
  }
  return true;
}

bool QuantifierComponent::enqueue(core::TermId lemma) {
  const core::Term& node = terms_.get(lemma);
  if (node.kind == core::TermKind::BoolValue && node.bool_value) {
    return false;
  }
  if (!lemmas_.insert(lemma).second) {
    return false;
  }
  queue_.push_back(lemma);
  return true;
}

core::TermId QuantifierComponent::lemma_for(core::TermId atom, bool value, core::TermId instance) {
  // A true atom implies its instance; a false one is implied by it.
  return value ? terms_.mk_or({terms_.mk_not(atom), instance})
               : terms_.mk_or({atom, terms_.mk_not(instance)});
}

void QuantifierComponent::skolemize(core::TermId atom, bool value) {
  if (!skolemized_.emplace(atom, value).second) {
    return;
  }
  std::vector<core::TermId> constants;
  for (const core::SortId sort : core::quantifier_sorts(terms_, atom)) {
    constants.push_back(fresh_constant_(sort, "skolem"));
  }
  const core::TermId instance = canonical(core::instantiate(terms_, atom, constants));
  if (enqueue(canonical(lemma_for(atom, value, instance)))) {
    statistics_.increment(":quant.skolem-lemmas");
  }
}

std::size_t QuantifierComponent::enumerate(core::TermId atom, bool value, std::size_t budget) {
  Enumeration& state = enumerations_[{atom, value}];
  if (state.sorts.empty()) {
    state.sorts = core::quantifier_sorts(terms_, atom);
  }
  const std::size_t arity = state.sorts.size();
  // Snapshot the pools: instantiating does not change them, but pool() may
  // create a term for an empty sort, which would reallocate the map's vectors.
  std::vector<std::vector<core::TermId>> pools;
  pools.reserve(arity);
  for (const core::SortId sort : state.sorts) {
    pools.push_back(pool(sort));
  }
  std::size_t largest = 0;
  for (const auto& terms : pools) {
    largest = std::max(largest, terms.size());
  }

  std::size_t made = 0;
  std::vector<core::TermId> tuple(arity);
  for (std::size_t level = state.first_open_level; level < largest; ++level) {
    bool clipped = false;
    for (const auto& terms : pools) {
      clipped = clipped || terms.size() <= level;
    }
    // Tuples whose largest index is exactly `level`, grouped by the first
    // position holding it: earlier positions range below the level, later
    // ones up to it. Each such tuple is produced once.
    for (std::size_t first = 0; first < arity; ++first) {
      if (pools[first].size() <= level) {
        continue;
      }
      std::vector<std::size_t> bound(arity);
      bool empty = false;
      for (std::size_t position = 0; position < arity; ++position) {
        const std::size_t size = pools[position].size();
        bound[position] = position < first    ? std::min(level, size)
                          : position == first ? 1
                                              : std::min(level + 1, size);
        empty = empty || bound[position] == 0;
      }
      if (empty) {
        continue;
      }
      std::vector<std::size_t> index(arity, 0);
      while (true) {
        for (std::size_t position = 0; position < arity; ++position) {
          tuple[position] =
              position == first ? pools[position][level] : pools[position][index[position]];
        }
        if (instances_ < options_.max_instances && state.tried.insert(tuple).second) {
          ++instances_;
          statistics_.increment(":quant.instances");
          if (add_instance(atom, value, core::instantiate(terms_, atom, tuple)) &&
              ++made == budget) {
            return made;
          }
        }
        if (instances_ >= options_.max_instances) {
          return made;
        }
        bool done = true;
        for (std::size_t position = arity; position-- > 0;) {
          if (position == first) {
            continue;
          }
          if (++index[position] < bound[position]) {
            done = false;
            break;
          }
          index[position] = 0;
        }
        if (done) {
          break;
        }
      }
    }
    if (!clipped && level == state.first_open_level) {
      ++state.first_open_level;
    }
  }
  return made;
}

}  // namespace primo::theory
