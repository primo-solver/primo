#include "smt/strategy.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "smt/tactic.hpp"

namespace primo::smt {
namespace {

std::string quote(std::string_view text) {
  std::string result;
  result.reserve(text.size() + 2);
  result.push_back('"');
  for (const char character : text) {
    if (character == '"' || character == '\\') {
      result.push_back('\\');
    }
    result.push_back(character);
  }
  result.push_back('"');
  return result;
}

std::string join_children(std::string_view head, const std::vector<Strategy>& children) {
  std::string result = "(" + std::string(head);
  for (const Strategy& child : children) {
    result += " " + child.canonical();
  }
  result += ")";
  return result;
}

std::string hash_string(std::string_view value) {
  // FNV-1a is used only as a deterministic identity fingerprint, not as a
  // cryptographic digest. Canonical text remains the collision-free identity.
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

}  // namespace

struct NumericProbe::Implementation {
  const std::string name;
  const Function function;
};

NumericProbe::NumericProbe(std::string name, Function function)
    : implementation_(std::make_shared<const Implementation>(
          Implementation{.name = std::move(name), .function = std::move(function)})) {
  if (implementation_->name.empty() || !implementation_->function) {
    throw std::invalid_argument("a numeric probe requires a name and implementation");
  }
}

double NumericProbe::operator()(const TacticContext& context, const Goal& goal) const {
  return implementation_->function(context, goal);
}

const std::string& NumericProbe::name() const { return implementation_->name; }

struct BooleanProbe::Implementation {
  const std::string name;
  const Function function;
};

BooleanProbe::BooleanProbe(std::string name, Function function)
    : implementation_(std::make_shared<const Implementation>(
          Implementation{.name = std::move(name), .function = std::move(function)})) {
  if (implementation_->name.empty() || !implementation_->function) {
    throw std::invalid_argument("a Boolean probe requires a name and implementation");
  }
}

bool BooleanProbe::operator()(const TacticContext& context, const Goal& goal) const {
  return implementation_->function(context, goal);
}

const std::string& BooleanProbe::name() const { return implementation_->name; }

NumericProbe assertion_count() {
  return NumericProbe("assertion-count", [](const TacticContext&, const Goal& goal) {
    return static_cast<double>(goal.assertions().size());
  });
}

BooleanProbe at_most(NumericProbe probe, double limit) {
  std::ostringstream name;
  name << "(<= " << probe.name() << " " << limit << ")";
  return BooleanProbe(name.str(), [probe = std::move(probe), limit](const TacticContext& context,
                                                                    const Goal& goal) {
    return probe(context, goal) <= limit;
  });
}

struct Strategy::Data {
  StrategyKind kind = StrategyKind::Skip;
  std::string leaf_id;
  std::vector<StrategyParameter> parameters;
  std::vector<Strategy> children;
  std::shared_ptr<const Tactic> implementation;
  std::shared_ptr<const BooleanProbe> probe;
  std::size_t max_iterations = 0;
  std::string repeat_policy_id = "structural";
  std::string repeat_execution_name = "strategy-repeat";
  RepeatPredicate repeat_predicate;
  RepeatEquivalence repeat_equivalence;
  RepeatObserver repeat_observer;
  std::chrono::milliseconds budget{0};
};

Strategy::Strategy(std::shared_ptr<const Data> data) : data_(std::move(data)) {}

Strategy Strategy::leaf_rendered(std::string id, std::vector<StrategyParameter> parameters,
                                 Tactic implementation) {
  if (id.empty()) {
    throw std::invalid_argument("a strategy leaf requires a stable identifier");
  }
  std::sort(
      parameters.begin(), parameters.end(),
      [](const StrategyParameter& lhs, const StrategyParameter& rhs) { return lhs.key < rhs.key; });
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (parameters[index].key.empty() ||
        (index != 0 && parameters[index - 1].key == parameters[index].key)) {
      throw std::invalid_argument("strategy leaf parameter keys must be non-empty and unique");
    }
  }
  auto data = std::make_shared<Data>();
  data->kind = StrategyKind::Leaf;
  data->leaf_id = std::move(id);
  data->parameters = std::move(parameters);
  data->implementation = std::make_shared<const Tactic>(std::move(implementation));
  return Strategy(std::move(data));
}

Strategy Strategy::sequence(std::vector<Strategy> children) {
  if (children.empty()) {
    return skip();
  }
  auto data = std::make_shared<Data>();
  data->kind = StrategyKind::Sequence;
  data->children = std::move(children);
  return Strategy(std::move(data));
}

Strategy Strategy::conditional(BooleanProbe probe, Strategy if_true, Strategy if_false) {
  auto data = std::make_shared<Data>();
  data->kind = StrategyKind::Conditional;
  data->probe = std::make_shared<const BooleanProbe>(std::move(probe));
  data->children = {std::move(if_true), std::move(if_false)};
  return Strategy(std::move(data));
}

Strategy Strategy::repeat(Strategy child, std::size_t max_iterations) {
  auto data = std::make_shared<Data>();
  data->kind = StrategyKind::Repeat;
  data->children.push_back(std::move(child));
  data->max_iterations = max_iterations;
  return Strategy(std::move(data));
}

Strategy Strategy::repeat(Strategy child, std::size_t max_iterations, std::string policy_id,
                          RepeatPredicate continue_after_change, RepeatEquivalence equivalent,
                          RepeatObserver observer, std::string execution_name) {
  if (policy_id.empty() || execution_name.empty()) {
    throw std::invalid_argument("a repeat policy and execution name must be non-empty");
  }
  auto data = std::make_shared<Data>();
  data->kind = StrategyKind::Repeat;
  data->children.push_back(std::move(child));
  data->max_iterations = max_iterations;
  data->repeat_policy_id = std::move(policy_id);
  data->repeat_execution_name = std::move(execution_name);
  data->repeat_predicate = std::move(continue_after_change);
  data->repeat_equivalence = std::move(equivalent);
  data->repeat_observer = std::move(observer);
  return Strategy(std::move(data));
}

Strategy Strategy::try_in_order(std::vector<Strategy> children) {
  if (children.empty()) {
    throw std::invalid_argument("try-in-order requires at least one arm");
  }
  auto data = std::make_shared<Data>();
  data->kind = StrategyKind::TryInOrder;
  data->children = std::move(children);
  return Strategy(std::move(data));
}

Strategy Strategy::try_for(Strategy child, std::chrono::milliseconds budget) {
  if (budget.count() < 0) {
    throw std::invalid_argument("try-for budget cannot be negative");
  }
  auto data = std::make_shared<Data>();
  data->kind = StrategyKind::TryFor;
  data->children.push_back(std::move(child));
  data->budget = budget;
  return Strategy(std::move(data));
}

Strategy Strategy::skip() { return Strategy(std::make_shared<const Data>()); }

StrategyKind Strategy::kind() const { return data_->kind; }

const std::string& Strategy::leaf_id() const { return data_->leaf_id; }

const std::vector<StrategyParameter>& Strategy::parameters() const { return data_->parameters; }

const std::vector<Strategy>& Strategy::children() const { return data_->children; }

std::string Strategy::canonical() const {
  switch (data_->kind) {
    case StrategyKind::Leaf: {
      std::string result = "(leaf " + quote(data_->leaf_id);
      for (const StrategyParameter& parameter : data_->parameters) {
        result += " (" + quote(parameter.key) + " " + quote(parameter.value) + ")";
      }
      return result + ")";
    }
    case StrategyKind::Sequence:
      return join_children("sequence", data_->children);
    case StrategyKind::Conditional:
      return "(conditional " + quote(data_->probe->name()) + " " + data_->children[0].canonical() +
             " " + data_->children[1].canonical() + ")";
    case StrategyKind::Repeat:
      return "(repeat " + std::to_string(data_->max_iterations) + " " +
             quote(data_->repeat_policy_id) + " " + data_->children.front().canonical() + ")";
    case StrategyKind::TryInOrder:
      return join_children("try-in-order", data_->children);
    case StrategyKind::TryFor:
      return "(try-for " + std::to_string(data_->budget.count()) + "ms " +
             data_->children.front().canonical() + ")";
    case StrategyKind::Skip:
      return "skip";
  }
  throw TacticError("unknown strategy kind");
}

std::string Strategy::fingerprint() const { return "fnv1a64:" + hash_string(canonical()); }

bool Strategy::operator==(const Strategy& other) const {
  return data_ == other.data_ || canonical() == other.canonical();
}

Tactic compile(const Strategy& strategy) {
  switch (strategy.data_->kind) {
    case StrategyKind::Leaf:
      return *strategy.data_->implementation;
    case StrategyKind::Sequence: {
      std::vector<Tactic> children;
      children.reserve(strategy.data_->children.size());
      for (const Strategy& child : strategy.data_->children) {
        children.push_back(compile(child));
      }
      return sequence("strategy-sequence", std::move(children));
    }
    case StrategyKind::Conditional: {
      const BooleanProbe probe = *strategy.data_->probe;
      return conditional(
          "strategy-conditional",
          [probe](const TacticContext& context, const Goal& goal) { return probe(context, goal); },
          compile(strategy.data_->children[0]), compile(strategy.data_->children[1]));
    }
    case StrategyKind::Repeat:
      return repeat(strategy.data_->repeat_execution_name,
                    compile(strategy.data_->children.front()), strategy.data_->max_iterations,
                    strategy.data_->repeat_predicate, strategy.data_->repeat_equivalence,
                    strategy.data_->repeat_observer);
    case StrategyKind::TryInOrder: {
      std::vector<Tactic> children;
      children.reserve(strategy.data_->children.size());
      for (const Strategy& child : strategy.data_->children) {
        children.push_back(compile(child));
      }
      return first_terminal("strategy-try-in-order",
                            std::make_shared<const SequentialTacticScheduler>(),
                            std::move(children));
    }
    case StrategyKind::TryFor: {
      Tactic child = compile(strategy.data_->children.front());
      const std::chrono::milliseconds budget = strategy.data_->budget;
      return Tactic("strategy-try-for", [child = std::move(child), budget](TacticContext& context,
                                                                           const AnyOf& input) {
        TacticContext::DeadlineScope deadline(context, budget);
        AnyOf result = child(context, input);
        if (!context.deadline_reached() || truth(result, context) != GoalTruth::Inconclusive) {
          return result;
        }
        std::vector<Goal> limited;
        limited.reserve(result.size());
        for (const Goal& goal : result.alternatives()) {
          limited.push_back(
              goal.with_diagnostic(InconclusiveReason::ResourceLimit, "deadline reached"));
        }
        return AnyOf(std::move(limited));
      });
    }
    case StrategyKind::Skip:
      return skip();
  }
  throw TacticError("unknown strategy kind");
}

}  // namespace primo::smt
