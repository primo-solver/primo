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

enum class InconclusiveReason {
  None,
  Inapplicable,
  ResourceLimit,
  Unsupported,
  Cancelled,
  Unknown,
};

// A goal is the conjunction of its ordered assertions.  The order is retained
// because preprocessing and search heuristics are allowed to observe it.
struct Goal {
  Goal();
  explicit Goal(std::vector<core::TermId> assertions);

  const std::vector<core::TermId>& assertions() const;
  const std::optional<std::string>& model() const;
  InconclusiveReason reason() const;
  const std::string& diagnostic() const;

  Goal with_assertions(std::vector<core::TermId> assertions) const;
  Goal with_model(std::optional<std::string> model) const;
  Goal with_diagnostic(InconclusiveReason reason, std::string diagnostic) const;
  Goal with_diagnostic(std::string diagnostic) const;

  bool operator==(const Goal& other) const;

 private:
  struct Data;
  explicit Goal(std::shared_ptr<const Data> data);

  std::shared_ptr<const Data> data_;
};

// Alternative goals have disjunctive semantics.  An AnyOf is deliberately
// never empty: true is represented by one Goal with no assertions, and false
// by one Goal containing the Boolean constant false.
class AnyOf {
 public:
  explicit AnyOf(std::vector<Goal> alternatives);

  static AnyOf single(Goal goal);

  const std::vector<Goal>& alternatives() const { return *alternatives_; }
  std::size_t size() const { return alternatives_->size(); }

  bool operator==(const AnyOf& other) const;

 private:
  std::shared_ptr<const std::vector<Goal>> alternatives_;
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
  using Function = std::function<AnyOf(TacticContext&, const AnyOf&)>;

  Tactic(std::string name, Function function);

  AnyOf operator()(TacticContext& context, const AnyOf& input) const;
  const std::string& name() const;

 private:
  struct Implementation;
  std::shared_ptr<const Implementation> implementation_;
};

using GoalTactic = std::function<AnyOf(TacticContext&, const Goal&)>;
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
