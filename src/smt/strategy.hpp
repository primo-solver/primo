#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "smt/tactic.hpp"

namespace primo::smt {

// Stable leaf identity is intentionally separate from the executable closure.
// Parameters are supplied by typed leaf builders and rendered in canonical key
// order when the immutable strategy node is created.
struct StrategyParameter {
  std::string key;
  std::string value;

  bool operator==(const StrategyParameter&) const = default;
};

template <typename Parameters>
concept StrategyParameterRecord = requires(const Parameters& parameters) {
  { parameters.strategy_parameters() } -> std::same_as<std::vector<StrategyParameter>>;
};

struct NoStrategyParameters {
  std::vector<StrategyParameter> strategy_parameters() const { return {}; }
};

class NumericProbe {
 public:
  using Function = std::function<double(const TacticContext&, const Goal&)>;

  NumericProbe(std::string name, Function function);
  double operator()(const TacticContext& context, const Goal& goal) const;
  const std::string& name() const;

 private:
  struct Implementation;
  std::shared_ptr<const Implementation> implementation_;
};

class BooleanProbe {
 public:
  using Function = std::function<bool(const TacticContext&, const Goal&)>;

  BooleanProbe(std::string name, Function function);
  bool operator()(const TacticContext& context, const Goal& goal) const;
  const std::string& name() const;

 private:
  struct Implementation;
  std::shared_ptr<const Implementation> implementation_;
};

NumericProbe assertion_count();
BooleanProbe at_most(NumericProbe probe, double limit);

enum class StrategyKind {
  Leaf,
  Sequence,
  Conditional,
  Repeat,
  TryInOrder,
  TryFor,
  Skip,
};

class Strategy {
 public:
  template <StrategyParameterRecord Parameters>
  static Strategy leaf(std::string id, const Parameters& parameters, Tactic implementation) {
    return leaf_rendered(std::move(id), parameters.strategy_parameters(),
                         std::move(implementation));
  }
  static Strategy sequence(std::vector<Strategy> children);
  static Strategy conditional(BooleanProbe probe, Strategy if_true, Strategy if_false);
  static Strategy repeat(Strategy child, std::size_t max_iterations);
  static Strategy repeat(Strategy child, std::size_t max_iterations, std::string policy_id,
                         RepeatPredicate continue_after_change, RepeatEquivalence equivalent,
                         RepeatObserver observer = {},
                         std::string execution_name = "strategy-repeat");
  static Strategy try_in_order(std::vector<Strategy> children);
  static Strategy try_for(Strategy child, std::chrono::milliseconds budget);
  static Strategy skip();

  StrategyKind kind() const;
  const std::string& leaf_id() const;
  const std::vector<StrategyParameter>& parameters() const;
  const std::vector<Strategy>& children() const;
  std::string canonical() const;
  std::string fingerprint() const;

  bool operator==(const Strategy& other) const;

 private:
  struct Data;
  explicit Strategy(std::shared_ptr<const Data> data);
  static Strategy leaf_rendered(std::string id, std::vector<StrategyParameter> parameters,
                                Tactic implementation);

  std::shared_ptr<const Data> data_;

  friend Tactic compile(const Strategy& strategy);
};

Tactic compile(const Strategy& strategy);

}  // namespace primo::smt
