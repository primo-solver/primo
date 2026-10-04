#include "smt/strategy.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <limits>
#include <locale>
#include <memory>
#include <span>
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

std::string join_rendered(std::string_view head, std::span<const std::string> children) {
  std::string result = "(" + std::string(head);
  for (const std::string& child : children) {
    result += " " + child;
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
  output.imbue(std::locale::classic());
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
  name.imbue(std::locale::classic());
  name << "(<= " << probe.name() << " ";
  if (std::isnan(limit)) {
    name << "nan";
  } else if (std::isinf(limit)) {
    name << (limit < 0 ? "-inf" : "inf");
  } else if (limit == 0) {
    name << "0";
  } else {
    name << std::setprecision(std::numeric_limits<double>::max_digits10) << limit;
  }
  name << ")";
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
  // Rendered with an explicit post-order walk rather than recursion. Strategy
  // trees are built programmatically, so nothing bounds their depth, and
  // canonical() also backs operator== and fingerprint().
  struct Frame {
    const Strategy* node;
    std::size_t next_child;
  };
  const auto render = [](const Data& node, std::span<const std::string> children) {
    switch (node.kind) {
      case StrategyKind::Leaf: {
        std::string result = "(leaf " + quote(node.leaf_id);
        for (const StrategyParameter& parameter : node.parameters) {
          result += " (" + quote(parameter.key) + " " + quote(parameter.value) + ")";
        }
        return result + ")";
      }
      case StrategyKind::Sequence:
        return join_rendered("sequence", children);
      case StrategyKind::Conditional:
        return "(conditional " + quote(node.probe->name()) + " " + children[0] + " " + children[1] +
               ")";
      case StrategyKind::Repeat:
        return "(repeat " + std::to_string(node.max_iterations) + " " +
               quote(node.repeat_policy_id) + " " + children.front() + ")";
      case StrategyKind::TryInOrder:
        return join_rendered("try-in-order", children);
      case StrategyKind::TryFor:
        return "(try-for " + std::to_string(node.budget.count()) + "ms " + children.front() + ")";
      case StrategyKind::Skip:
        return std::string("skip");
    }
    throw TacticError("unknown strategy kind");
  };

  std::vector<Frame> stack;
  std::vector<std::string> rendered;
  stack.push_back(Frame{.node = this, .next_child = 0});
  while (!stack.empty()) {
    const Strategy& node = *stack.back().node;
    const std::vector<Strategy>& children = node.data_->children;
    if (stack.back().next_child < children.size()) {
      const std::size_t index = stack.back().next_child++;
      stack.push_back(Frame{.node = &children[index], .next_child = 0});
      continue;
    }
    // Every child of this node has been rendered and sits at the end of the
    // output stack, in order.
    const std::size_t first = rendered.size() - children.size();
    std::string text = render(*node.data_, std::span(rendered).subspan(first));
    rendered.resize(first);
    rendered.push_back(std::move(text));
    stack.pop_back();
  }
  // The walk consumes each node's children and leaves exactly one result.
  if (rendered.size() != 1) {
    throw TacticError("canonical rendering did not reduce to a single strategy");
  }
  return std::move(rendered.front());
}

std::string Strategy::fingerprint() const { return "fnv1a64:" + hash_string(canonical()); }

bool Strategy::operator==(const Strategy& other) const {
  return data_ == other.data_ || canonical() == other.canonical();
}

Tactic compile(const Strategy& strategy) {
  // Lowered with an explicit post-order walk, for the same reason canonical()
  // is: a programmatically built strategy has no bounded depth.
  struct Frame {
    const Strategy* node;
    std::size_t next_child;
  };
  const auto lower = [](const Strategy::Data& node, std::span<Tactic> children) {
    switch (node.kind) {
      case StrategyKind::Leaf:
        return *node.implementation;
      case StrategyKind::Sequence:
        return sequence("strategy-sequence",
                        std::vector<Tactic>(std::make_move_iterator(children.begin()),
                                            std::make_move_iterator(children.end())));
      case StrategyKind::Conditional: {
        const BooleanProbe probe = *node.probe;
        return conditional(
            "strategy-conditional",
            [probe](const TacticContext& context, const Goal& goal) {
              return probe(context, goal);
            },
            std::move(children[0]), std::move(children[1]));
      }
      case StrategyKind::Repeat:
        return repeat(node.repeat_execution_name, std::move(children.front()), node.max_iterations,
                      node.repeat_predicate, node.repeat_equivalence, node.repeat_observer);
      case StrategyKind::TryInOrder:
        return first_terminal("strategy-try-in-order",
                              std::make_shared<const SequentialTacticScheduler>(),
                              std::vector<Tactic>(std::make_move_iterator(children.begin()),
                                                  std::make_move_iterator(children.end())));
      case StrategyKind::TryFor: {
        Tactic child = std::move(children.front());
        const std::chrono::milliseconds budget = node.budget;
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
  };

  std::vector<Frame> stack;
  std::vector<Tactic> compiled;
  stack.push_back(Frame{.node = &strategy, .next_child = 0});
  while (!stack.empty()) {
    const Strategy& node = *stack.back().node;
    const std::vector<Strategy>& children = node.data_->children;
    if (stack.back().next_child < children.size()) {
      const std::size_t index = stack.back().next_child++;
      stack.push_back(Frame{.node = &children[index], .next_child = 0});
      continue;
    }
    const std::size_t first = compiled.size() - children.size();
    Tactic lowered = lower(*node.data_, std::span(compiled).subspan(first));
    // erase rather than resize: Tactic is deliberately not default-constructible.
    compiled.erase(compiled.begin() + static_cast<std::ptrdiff_t>(first), compiled.end());
    compiled.push_back(std::move(lowered));
    stack.pop_back();
  }
  if (compiled.size() != 1) {
    throw TacticError("compilation did not reduce to a single tactic");
  }
  return std::move(compiled.front());
}

}  // namespace primo::smt
