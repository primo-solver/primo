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

AnyOf::AnyOf(std::vector<Goal> alternatives) : alternatives_(std::move(alternatives)) {
  if (alternatives_.empty()) {
    throw std::invalid_argument("AnyOf requires at least one goal");
  }
}

AnyOf AnyOf::single(Goal goal) {
  std::vector<Goal> alternatives;
  alternatives.push_back(std::move(goal));
  return AnyOf(std::move(alternatives));
}

TacticContext::TacticContext(core::TermManager& terms, util::Statistics& statistics,
                             std::function<bool()> cancelled)
    : terms_(terms), statistics_(statistics), cancelled_(std::move(cancelled)) {}

bool TacticContext::cancelled() const { return cancelled_ && cancelled_(); }

GoalTruth truth(const Goal& goal, const TacticContext& context) {
  if (goal.assertions.empty()) {
    return GoalTruth::True;
  }
  for (const core::TermId assertion : goal.assertions) {
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

Tactic::Tactic(std::string name, Function function)
    : name_(std::move(name)), function_(std::move(function)) {
  if (name_.empty()) {
    throw std::invalid_argument("a tactic requires a non-empty name");
  }
  if (!function_) {
    throw std::invalid_argument("a tactic requires an implementation");
  }
}

AnyOf Tactic::operator()(TacticContext& context, AnyOf input) const {
  if (context.cancelled()) {
    for (Goal& goal : input.alternatives()) {
      goal.diagnostic = "cancelled";
    }
    return input;
  }

  const TacticClock::time_point start = TacticClock::now();
  context.statistics().increment(stat_name(name_, "invocations"));
  AnyOf output = function_(context, std::move(input));
  context.statistics().increment(time_stat_name(name_),
                                 elapsed_microseconds(start, TacticClock::now()));
  return output;
}

Tactic make_goal_tactic(std::string name, GoalTactic tactic) {
  return Tactic(std::move(name), [tactic = std::move(tactic)](TacticContext& context, AnyOf input) {
    std::vector<Goal> output;
    for (Goal& goal : input.alternatives()) {
      if (truth(goal, context) != GoalTruth::Inconclusive) {
        output.push_back(std::move(goal));
        continue;
      }
      AnyOf alternatives = tactic(context, std::move(goal));
      for (Goal& alternative : alternatives.alternatives()) {
        output.push_back(std::move(alternative));
      }
    }
    return AnyOf(std::move(output));
  });
}

Tactic skip() {
  return Tactic("skip", [](TacticContext&, AnyOf input) { return input; });
}

Tactic sequence(std::string name, std::vector<Tactic> tactics) {
  return Tactic(std::move(name),
                [tactics = std::move(tactics)](TacticContext& context, AnyOf input) {
                  for (const Tactic& tactic : tactics) {
                    input = tactic(context, std::move(input));
                    if (truth(input, context) != GoalTruth::Inconclusive) {
                      break;
                    }
                  }
                  return input;
                });
}

Tactic conditional(std::string name, GoalPredicate predicate, Tactic if_true, Tactic if_false) {
  return make_goal_tactic(
      std::move(name), [predicate = std::move(predicate), if_true = std::move(if_true),
                        if_false = std::move(if_false)](TacticContext& context, Goal goal) {
        AnyOf input = AnyOf::single(std::move(goal));
        const Goal& probe_goal = input.alternatives().front();
        return predicate(context, probe_goal) ? if_true(context, std::move(input))
                                              : if_false(context, std::move(input));
      });
}

Tactic repeat(std::string name, Tactic tactic, std::size_t max_iterations,
              RepeatPredicate continue_after_change) {
  return Tactic(std::move(name), [tactic = std::move(tactic), max_iterations,
                                  continue_after_change = std::move(continue_after_change)](
                                     TacticContext& context, AnyOf input) {
    for (std::size_t iteration = 0; iteration < max_iterations; ++iteration) {
      const AnyOf before = input;
      AnyOf after = tactic(context, input);
      if (after == before) {
        break;
      }
      input = std::move(after);
      if (truth(input, context) != GoalTruth::Inconclusive) {
        break;
      }
      if (continue_after_change && !continue_after_change(context, before, input, iteration + 1)) {
        break;
      }
    }
    return input;
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
                                     TacticContext& context, AnyOf input) {
    return scheduler->first_terminal(context, input, tactics);
  });
}

NumericProbe assertion_count_probe() {
  return [](const TacticContext&, const Goal& goal) {
    return static_cast<double>(goal.assertions.size());
  };
}

}  // namespace primo::smt
