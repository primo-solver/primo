#include "util/statistics.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace primo::util {

namespace {

using namespace std::string_view_literals;

constexpr std::array<std::string_view, static_cast<std::size_t>(StatKey::Count)> kStatNames = {
    ":input.assertions"sv,
    ":frontend.ite.boolean"sv,
    ":frontend.ite.auxiliaries"sv,
    ":preprocessing.assertions"sv,
    ":preprocessing.passes"sv,
    ":preprocessing.changed-passes"sv,
    ":preprocessing.adaptive-passes"sv,
    ":preprocessing.term-size.initial"sv,
    ":preprocessing.term-size.final"sv,
    ":preprocessing.eq-bridge.equalities"sv,
    ":preprocessing.eq-bridge.skipped-or-nodes"sv,
    ":preprocessing.boolean-units.facts"sv,
    ":preprocessing.boolean-units.rewrites"sv,
    ":preprocessing.forced-atoms.facts"sv,
    ":preprocessing.forced-atoms.disequalities"sv,
    ":preprocessing.forced-atoms.rewrites"sv,
    ":preprocessing.unit-equality.consequences"sv,
    ":preprocessing.unit-equality.substitution-units"sv,
    ":preprocessing.unit-equality.substitutions"sv,
    ":preprocessing.unit-equality.substitution-accepted"sv,
    ":preprocessing.unit-equality.substitution-rejected"sv,
    ":preprocessing.solve-eqs.candidates"sv,
    ":preprocessing.solve-eqs.accepted"sv,
    ":preprocessing.solve-eqs.rewrites"sv,
    ":preprocessing.solve-eqs.rejected-cycles"sv,
    ":preprocessing.solve-eqs.rejected-blowups"sv,
    ":preprocessing.finite-domain.domains"sv,
    ":preprocessing.finite-domain.amo-clauses"sv,
    ":preprocessing.finite-domain.raw-choices"sv,
    ":preprocessing.finite-domain.raw-amo-clauses"sv,
    ":preprocessing.finite-domain.post-amo-clauses"sv,
    ":preprocessing.finite-domain.deduped-amo-clauses"sv,
    ":preprocessing.finite-domain.evaluated-equalities"sv,
    ":preprocessing.finite-domain.evaluation-cutoffs"sv,
    ":preprocessing.equality-definitions"sv,
    ":preprocessing.equality-definitions.candidate-pairs"sv,
    ":preprocessing.equality-definitions.accepted-pairs"sv,
    ":preprocessing.equality-definitions.predicted-clauses"sv,
    ":preprocessing.equality-definitions.rejected-domain-size"sv,
    ":preprocessing.equality-definitions.rejected-clause-limit"sv,
    ":preprocessing.ground-congruence-clauses"sv,
    ":preprocessing.ground-congruence-skipped-groups"sv,
    ":preprocessing.uf-symmetry.accepted-sets"sv,
    ":preprocessing.uf-symmetry.values"sv,
    ":preprocessing.uf-symmetry.terms"sv,
    ":preprocessing.uf-symmetry.constraints"sv,
    ":preprocessing.uf-symmetry.value-precedence-terms"sv,
    ":preprocessing.uf-symmetry.value-precedence-constraints"sv,
    ":preprocessing.uf-symmetry.rejected-noninvariant"sv,
    ":preprocessing.auto.single-assertion"sv,
    ":preprocessing.auto.strong"sv,
    ":preprocessing.auto.basic-single"sv,
    ":preprocessing.auto.basic-multi"sv,
    ":preprocessing.auto.large-assertion-skip"sv,
    ":sat.phase.theory-negative"sv,
    ":sat.phase.auto-polarity"sv,
    ":sat.phase.auto-basic-multi-polarity"sv,
    ":sat.vars"sv,
    ":sat.clauses"sv,
    ":sat.clause-lits"sv,
    ":sat.unit-clauses"sv,
    ":sat.binary-clauses"sv,
    ":solver.boolean"sv,
    ":solver.qf-uf"sv,
    ":sat.solve-calls"sv,
    ":euf.theory-atoms"sv,
    ":euf.pure-filter.enabled"sv,
    ":euf.pure-atoms.positive"sv,
    ":euf.pure-atoms.negative"sv,
    ":euf.pure-literals.filtered"sv,
    ":euf.assignments"sv,
    ":euf.eq-assignments"sv,
    ":euf.diseq-assignments"sv,
    ":euf.checks"sv,
    ":euf.theory-conflicts"sv,
    ":euf.propagation.rounds"sv,
    ":euf.propagation.candidates"sv,
    ":euf.propagation.delivered"sv,
    ":euf.propagation.reason-lits"sv,
    ":euf.propagation.budget"sv,
    ":euf.propagation.delivery-budget"sv,
    ":euf.propagation.budget-cutoffs"sv,
    ":euf.propagation.hit-rate-cutoffs"sv,
    ":euf.propagation.auto-online"sv,
    ":euf.propagation.auto-conflict"sv,
    ":euf.propagation.auto-off"sv,
    ":euf.incremental.terms"sv,
    ":euf.incremental.flat-definitions"sv,
    ":euf.incremental.merges"sv,
    ":euf.incremental.pending-pops"sv,
    ":euf.incremental.lookup-probes"sv,
    ":euf.incremental.lookup-hits"sv,
    ":euf.incremental.congruence-merges"sv,
    ":euf.incremental.disequality-checks"sv,
    ":euf.incremental.conflicts"sv,
    ":euf.incremental.explain-calls"sv,
    ":euf.incremental.explain-cache-hits"sv,
    ":euf.incremental.explain-length"sv,
    ":euf.incremental.backtracks"sv,
    ":euf.incremental.trail-entries"sv,
    ":euf.incremental.class-member-trail-entries-saved"sv,
    ":euf.incremental.use-list-appends"sv,
    ":euf.incremental.use-list-duplicate-skips"sv,
    ":euf.incremental.affected-use-list-dedup"sv,
    ":euf.incremental.max-use-list-size"sv,
};

}  // namespace

std::string_view stat_name(StatKey key) { return kStatNames.at(static_cast<std::size_t>(key)); }

std::optional<StatKey> Statistics::lookup_key(std::string_view name) {
  for (std::size_t i = 0; i < kStatNames.size(); ++i) {
    if (kStatNames[i] == name) {
      return static_cast<StatKey>(i);
    }
  }
  return std::nullopt;
}

void Statistics::remember_key(StatKey key) {
  const std::size_t slot = index(key);
  if (!seen_[slot]) {
    seen_[slot] = true;
    order_.push_back(key);
  }
}

void Statistics::remember_key(std::string_view name) {
  const std::string key{name};
  if (!extra_values_.contains(key)) {
    extra_order_.push_back(key);
  }
}

void Statistics::set(StatKey key, Value value) {
  remember_key(key);
  values_[index(key)] = value;
}

void Statistics::set(std::string_view name, Value value) {
  if (const std::optional<StatKey> key = lookup_key(name); key.has_value()) {
    set(key.value(), value);
    return;
  }
  remember_key(name);
  extra_values_[std::string{name}] = value;
}

void Statistics::increment(StatKey key, Value delta) {
  remember_key(key);
  values_[index(key)] += delta;
}

void Statistics::increment(std::string_view name, Value delta) {
  if (const std::optional<StatKey> key = lookup_key(name); key.has_value()) {
    increment(key.value(), delta);
    return;
  }
  remember_key(name);
  extra_values_[std::string{name}] += delta;
}

Statistics::Value Statistics::get(StatKey key) const { return values_[index(key)]; }

Statistics::Value Statistics::get(std::string_view name) const {
  if (const std::optional<StatKey> key = lookup_key(name); key.has_value()) {
    return get(key.value());
  }
  const auto found = extra_values_.find(std::string{name});
  if (found == extra_values_.end()) {
    return 0;
  }
  return found->second;
}

bool Statistics::contains(StatKey key) const { return seen_[index(key)]; }

bool Statistics::contains(std::string_view name) const {
  if (const std::optional<StatKey> key = lookup_key(name); key.has_value()) {
    return contains(key.value());
  }
  return extra_values_.contains(std::string{name});
}

std::vector<std::string> Statistics::keys() const {
  std::vector<std::string> result;
  result.reserve(order_.size() + extra_order_.size());
  for (const StatKey key : order_) {
    result.emplace_back(stat_name(key));
  }
  result.insert(result.end(), extra_order_.begin(), extra_order_.end());
  return result;
}

void print_statistics(std::ostream& output, const Statistics& statistics) {
  for (const std::string& key : statistics.keys()) {
    output << key << ' ' << statistics.get(key) << '\n';
  }
}

}  // namespace primo::util
