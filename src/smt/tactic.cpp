#include "smt/tactic.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {

using TacticClock = std::chrono::steady_clock;

std::uint64_t elapsed_microseconds(TacticClock::time_point start, TacticClock::time_point end) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

std::string stat_name(std::string_view tactic, std::string_view suffix) {
  return ":tactic." + std::string(tactic) + "." + std::string(suffix);
}

std::string time_stat_name(std::string_view tactic) {
  return ":time.tactic." + std::string(tactic) + "-us";
}

}  // namespace

struct Goal::Data {
  std::vector<core::TermId> assertions;
  std::optional<std::string> model;
  InconclusiveReason reason = InconclusiveReason::None;
  std::string diagnostic;

  bool operator==(const Data&) const = default;
};

Goal::Goal() : data_(std::make_shared<const Data>()) {}

Goal::Goal(std::vector<core::TermId> assertions)
    : data_(std::make_shared<const Data>(Data{.assertions = std::move(assertions),
                                              .model = {},
                                              .reason = InconclusiveReason::None,
                                              .diagnostic = {}})) {}

Goal::Goal(std::shared_ptr<const Data> data) : data_(std::move(data)) {}

const std::vector<core::TermId>& Goal::assertions() const { return data_->assertions; }

const std::optional<std::string>& Goal::model() const { return data_->model; }

InconclusiveReason Goal::reason() const { return data_->reason; }

const std::string& Goal::diagnostic() const { return data_->diagnostic; }

Goal Goal::with_assertions(std::vector<core::TermId> assertions) const {
  if (assertions == data_->assertions) {
    return *this;
  }
  Data updated = *data_;
  updated.assertions = std::move(assertions);
  return Goal(std::make_shared<const Data>(std::move(updated)));
}

Goal Goal::with_model(std::optional<std::string> model) const {
  if (model == data_->model) {
    return *this;
  }
  Data updated = *data_;
  updated.model = std::move(model);
  return Goal(std::make_shared<const Data>(std::move(updated)));
}

Goal Goal::with_diagnostic(InconclusiveReason reason, std::string diagnostic) const {
  if (reason == data_->reason && diagnostic == data_->diagnostic) {
    return *this;
  }
  Data updated = *data_;
  updated.reason = reason;
  updated.diagnostic = std::move(diagnostic);
  return Goal(std::make_shared<const Data>(std::move(updated)));
}

Goal Goal::with_diagnostic(std::string diagnostic) const {
  return with_diagnostic(
      diagnostic.empty() ? InconclusiveReason::None : InconclusiveReason::Unknown,
      std::move(diagnostic));
}

bool Goal::operator==(const Goal& other) const {
  return data_ == other.data_ || *data_ == *other.data_;
}

AnyOf::AnyOf(std::vector<Goal> alternatives)
    : alternatives_(std::make_shared<const std::vector<Goal>>(std::move(alternatives))) {
  if (alternatives_->empty()) {
    throw std::invalid_argument("AnyOf requires at least one goal");
  }
}

AnyOf AnyOf::single(Goal goal) {
  std::vector<Goal> alternatives;
  alternatives.push_back(std::move(goal));
  return AnyOf(std::move(alternatives));
}

bool AnyOf::operator==(const AnyOf& other) const {
  return alternatives_ == other.alternatives_ || *alternatives_ == *other.alternatives_;
}

TacticContext::TacticContext(core::TermManager& terms, util::Statistics& statistics,
                             std::function<bool()> cancelled)
    : terms_(terms), statistics_(statistics), cancelled_(std::move(cancelled)) {}

bool TacticContext::cancelled() const { return cancelled_ && cancelled_(); }

GoalTruth truth(const Goal& goal, const TacticContext& context) {
  if (goal.assertions().empty()) {
    return GoalTruth::True;
  }
  for (const core::TermId assertion : goal.assertions()) {
    const core::Term& term = context.terms().get(assertion);
    if (term.sort != context.terms().symbols().bool_sort()) {
      throw TacticError("goal assertion is not Bool-sorted");
    }
    if (term.kind == core::TermKind::BoolValue && !term.bool_value) {
      return GoalTruth::False;
    }
  }
  return GoalTruth::Inconclusive;
}

GoalTruth truth(const AnyOf& goals, const TacticContext& context) {
  bool all_false = true;
  for (const Goal& goal : goals.alternatives()) {
    const GoalTruth goal_truth = truth(goal, context);
    if (goal_truth == GoalTruth::True) {
      return GoalTruth::True;
    }
    if (goal_truth != GoalTruth::False) {
      all_false = false;
    }
  }
  return all_false ? GoalTruth::False : GoalTruth::Inconclusive;
}

struct Tactic::Implementation {
  const std::string name;
  const Function function;
  const std::string invocation_stat;
  const std::string time_stat;

  Implementation(std::string tactic_name, Function tactic_function)
      : name(std::move(tactic_name)),
        function(std::move(tactic_function)),
        invocation_stat(stat_name(name, "invocations")),
        time_stat(time_stat_name(name)) {}
};

Tactic::Tactic(std::string name, Function function)
    : implementation_(
          std::make_shared<const Implementation>(std::move(name), std::move(function))) {
  if (implementation_->name.empty()) {
    throw std::invalid_argument("a tactic requires a non-empty name");
  }
  if (!implementation_->function) {
    throw std::invalid_argument("a tactic requires an implementation");
  }
}

AnyOf Tactic::operator()(TacticContext& context, const AnyOf& input) const {
  if (context.cancelled()) {
    std::vector<Goal> cancelled;
    cancelled.reserve(input.size());
    for (const Goal& goal : input.alternatives()) {
      cancelled.push_back(goal.with_diagnostic(InconclusiveReason::Cancelled, "cancelled"));
    }
    return AnyOf(std::move(cancelled));
  }

  const TacticClock::time_point start = TacticClock::now();
  context.statistics().increment(implementation_->invocation_stat);
  AnyOf output = implementation_->function(context, input);
  context.statistics().increment(implementation_->time_stat,
                                 elapsed_microseconds(start, TacticClock::now()));
  return output;
}

const std::string& Tactic::name() const { return implementation_->name; }

Tactic make_goal_tactic(std::string name, GoalTactic tactic) {
  return Tactic(std::move(name),
                [tactic = std::move(tactic)](TacticContext& context, const AnyOf& input) {
                  std::vector<Goal> output;
                  for (const Goal& goal : input.alternatives()) {
                    if (truth(goal, context) != GoalTruth::Inconclusive) {
                      output.push_back(goal);
                      continue;
                    }
                    AnyOf alternatives = tactic(context, goal);
                    for (const Goal& alternative : alternatives.alternatives()) {
                      output.push_back(alternative);
                    }
                  }
                  return AnyOf(std::move(output));
                });
}

Tactic skip() {
  return Tactic("skip", [](TacticContext&, const AnyOf& input) { return input; });
}

Tactic sequence(std::string name, std::vector<Tactic> tactics) {
  return Tactic(std::move(name),
                [tactics = std::move(tactics)](TacticContext& context, const AnyOf& input) {
                  AnyOf current = input;
                  for (const Tactic& tactic : tactics) {
                    current = tactic(context, current);
                    if (truth(current, context) != GoalTruth::Inconclusive) {
                      break;
                    }
                  }
                  return current;
                });
}

Tactic conditional(std::string name, GoalPredicate predicate, Tactic if_true, Tactic if_false) {
  return make_goal_tactic(
      std::move(name), [predicate = std::move(predicate), if_true = std::move(if_true),
                        if_false = std::move(if_false)](TacticContext& context, const Goal& goal) {
        AnyOf input = AnyOf::single(goal);
        const Goal& probe_goal = input.alternatives().front();
        return predicate(context, probe_goal) ? if_true(context, input) : if_false(context, input);
      });
}

Tactic repeat(std::string name, Tactic tactic, std::size_t max_iterations,
              RepeatPredicate continue_after_change) {
  return Tactic(std::move(name), [tactic = std::move(tactic), max_iterations,
                                  continue_after_change = std::move(continue_after_change)](
                                     TacticContext& context, const AnyOf& input) {
    AnyOf current = input;
    for (std::size_t iteration = 0; iteration < max_iterations; ++iteration) {
      AnyOf after = tactic(context, current);
      if (after == current) {
        break;
      }
      const AnyOf before = current;
      current = std::move(after);
      if (truth(current, context) != GoalTruth::Inconclusive) {
        break;
      }
      if (continue_after_change &&
          !continue_after_change(context, before, current, iteration + 1)) {
        break;
      }
    }
    return current;
  });
}

AnyOf SequentialTacticScheduler::first_terminal(TacticContext& context, const AnyOf& input,
                                                const std::vector<Tactic>& tactics) const {
  AnyOf last = input;
  for (const Tactic& tactic : tactics) {
    last = tactic(context, input);
    if (truth(last, context) != GoalTruth::Inconclusive) {
      return last;
    }
  }
  return last;
}

Tactic first_terminal(std::string name, std::shared_ptr<const TacticScheduler> scheduler,
                      std::vector<Tactic> tactics) {
  if (!scheduler) {
    throw std::invalid_argument("first-terminal requires a scheduler");
  }
  return Tactic(std::move(name), [scheduler = std::move(scheduler), tactics = std::move(tactics)](
                                     TacticContext& context, const AnyOf& input) {
    return scheduler->first_terminal(context, input, tactics);
  });
}

NumericProbe assertion_count_probe() {
  return [](const TacticContext&, const Goal& goal) {
    return static_cast<double>(goal.assertions().size());
  };
}

}  // namespace primo::smt
