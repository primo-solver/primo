#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ids.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::util {
class Statistics;
}

namespace primo::smt {

// A goal is the conjunction of its ordered assertions.  The order is retained
// because preprocessing and search heuristics are allowed to observe it.
struct Goal {
  Goal() = default;
  explicit Goal(std::vector<core::TermId> goal_assertions)
      : assertions(std::move(goal_assertions)) {}

  std::vector<core::TermId> assertions;
  std::optional<std::string> model;
  std::string diagnostic;

  bool operator==(const Goal&) const = default;
};

// Alternative goals have disjunctive semantics.  An AnyOf is deliberately
// never empty: true is represented by one Goal with no assertions, and false
// by one Goal containing the Boolean constant false.
class AnyOf {
 public:
  explicit AnyOf(std::vector<Goal> alternatives);

  static AnyOf single(Goal goal);

  const std::vector<Goal>& alternatives() const { return alternatives_; }
  std::vector<Goal>& alternatives() { return alternatives_; }
  std::size_t size() const { return alternatives_.size(); }

  bool operator==(const AnyOf&) const = default;

 private:
  std::vector<Goal> alternatives_;
};

enum class GoalTruth {
  True,
  False,
  Inconclusive,
};

class TacticError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class TacticContext {
 public:
  TacticContext(core::TermManager& terms, util::Statistics& statistics,
                std::function<bool()> cancelled = {});

  core::TermManager& terms() { return terms_; }
  const core::TermManager& terms() const { return terms_; }
  util::Statistics& statistics() { return statistics_; }
  const util::Statistics& statistics() const { return statistics_; }
  bool cancelled() const;

 private:
  core::TermManager& terms_;
  util::Statistics& statistics_;
  std::function<bool()> cancelled_;
};

GoalTruth truth(const Goal& goal, const TacticContext& context);
GoalTruth truth(const AnyOf& goals, const TacticContext& context);

class Tactic {
 public:
  using Function = std::function<AnyOf(TacticContext&, AnyOf)>;

  Tactic(std::string name, Function function);

  AnyOf operator()(TacticContext& context, AnyOf input) const;
  const std::string& name() const { return name_; }

 private:
  std::string name_;
  Function function_;
};

using GoalTactic = std::function<AnyOf(TacticContext&, Goal)>;
using NumericProbe = std::function<double(const TacticContext&, const Goal&)>;
using GoalPredicate = std::function<bool(const TacticContext&, const Goal&)>;
using RepeatPredicate =
    std::function<bool(const TacticContext&, const AnyOf&, const AnyOf&, std::size_t)>;

Tactic make_goal_tactic(std::string name, GoalTactic tactic);
Tactic skip();
Tactic sequence(std::string name, std::vector<Tactic> tactics);
Tactic conditional(std::string name, GoalPredicate predicate, Tactic if_true, Tactic if_false);
Tactic repeat(std::string name, Tactic tactic, std::size_t max_iterations,
              RepeatPredicate continue_after_change = {});

class TacticScheduler {
 public:
  virtual ~TacticScheduler() = default;

  virtual AnyOf first_terminal(TacticContext& context, const AnyOf& input,
                               const std::vector<Tactic>& tactics) const = 0;
};

class SequentialTacticScheduler final : public TacticScheduler {
 public:
  AnyOf first_terminal(TacticContext& context, const AnyOf& input,
                       const std::vector<Tactic>& tactics) const override;
};

Tactic first_terminal(std::string name, std::shared_ptr<const TacticScheduler> scheduler,
                      std::vector<Tactic> tactics);

NumericProbe assertion_count_probe();

}  // namespace primo::smt
