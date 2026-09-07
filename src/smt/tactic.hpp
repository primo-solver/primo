#pragma once

#include <chrono>
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
#include "smt/inconclusive_reason.hpp"
#include "smt/model_reconstruction.hpp"

namespace primo::core {
class TermManager;
class Model;
}  // namespace primo::core

namespace primo::util {
class Statistics;
}

namespace primo::smt {

// A goal is the conjunction of its ordered assertions.  The order is retained
// because preprocessing and search heuristics are allowed to observe it.
struct Goal {
  Goal();
  explicit Goal(std::vector<core::TermId> assertions);

  const std::vector<core::TermId>& assertions() const;
  const std::optional<std::string>& model() const;
  const std::shared_ptr<const core::Model>& typed_model() const;
  const ModelReconstruction& reconstruction() const;
  InconclusiveReason reason() const;
  const std::string& diagnostic() const;

  Goal with_assertions(std::vector<core::TermId> assertions) const;
  Goal with_model(std::optional<std::string> model) const;
  Goal with_typed_model(std::shared_ptr<const core::Model> model) const;
  Goal with_reconstruction(ModelReconstruction reconstruction) const;
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
  using Clock = std::chrono::steady_clock;
  // Some transformations must introduce fresh symbols. They receive that one
  // capability from the executing context instead of borrowing the session's
  // Frontend, which is what keeps a strategy node free of session state and
  // keeps Frontend out of this header.
  using FreshConstantFactory = std::function<core::TermId(core::SortId, std::string_view)>;

  TacticContext(core::TermManager& terms, util::Statistics& statistics,
                std::function<bool()> cancelled = {}, bool produce_model = false,
                FreshConstantFactory fresh_constant = {});

  core::TermManager& terms() { return terms_; }
  const core::TermManager& terms() const { return terms_; }
  util::Statistics& statistics() { return statistics_; }
  const util::Statistics& statistics() const { return statistics_; }
  bool cancelled() const;
  bool deadline_reached() const;
  bool interrupted() const;
  bool produce_model() const { return produce_model_; }
  std::optional<Clock::time_point> deadline() const { return deadline_; }
  bool can_create_fresh_constants() const { return static_cast<bool>(fresh_constant_); }
  // Throws TacticError when the executing session did not supply a factory,
  // rather than silently producing a term the caller cannot own.
  core::TermId fresh_constant(core::SortId sort, std::string_view prefix) const;

  // A deadline scope is execution state owned by the context, never by a
  // tactic or strategy node. Nested scopes retain the earliest deadline.
  class DeadlineScope {
   public:
    DeadlineScope(TacticContext& context, Clock::duration budget);
    DeadlineScope(const DeadlineScope&) = delete;
    DeadlineScope& operator=(const DeadlineScope&) = delete;
    DeadlineScope(DeadlineScope&&) = delete;
    DeadlineScope& operator=(DeadlineScope&&) = delete;
    ~DeadlineScope();

   private:
    TacticContext& context_;
    std::optional<Clock::time_point> previous_;
  };

 private:
  core::TermManager& terms_;
  util::Statistics& statistics_;
  std::function<bool()> cancelled_;
  bool produce_model_ = false;
  std::optional<Clock::time_point> deadline_;
  FreshConstantFactory fresh_constant_;
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
using GoalPredicate = std::function<bool(const TacticContext&, const Goal&)>;
// The continuation predicate and the observer may record work, so they receive
// the executing context by mutable reference: a strategy node must take its
// term manager and statistics from the context it runs under rather than
// capturing the session that built it. Equivalence only inspects goals.
using RepeatPredicate =
    std::function<bool(TacticContext&, const AnyOf&, const AnyOf&, std::size_t)>;
using RepeatEquivalence = std::function<bool(const TacticContext&, const AnyOf&, const AnyOf&)>;
using RepeatObserver = std::function<void(TacticContext&, const AnyOf&, const AnyOf&, std::size_t)>;

Tactic make_goal_tactic(std::string name, GoalTactic tactic);
Tactic skip();
Tactic sequence(std::string name, std::vector<Tactic> tactics);
Tactic conditional(std::string name, GoalPredicate predicate, Tactic if_true, Tactic if_false);
Tactic repeat(std::string name, Tactic tactic, std::size_t max_iterations,
              RepeatPredicate continue_after_change = {}, RepeatEquivalence equivalent = {},
              RepeatObserver observer = {});

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

}  // namespace primo::smt
