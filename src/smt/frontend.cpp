#include "smt/frontend.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/solver_error.hpp"
#include "smtlib/ast.hpp"
#include "smtlib/commands.hpp"
#include "util/rational.hpp"

namespace primo::smt {

// Builds a core::TermId tree for one top-level Frontend::build_term(root)
// call, iteratively -- deliberately not via recursive descent, since
// fuzzer-generated input can nest arbitrarily deep (long `let`/arithmetic
// chains in particular) and a native-recursive version can stack-overflow
// (found via fuzzing under ASAN, see docs/run-log.json). Every place the
// term-building logic used to call build_term(child) and use its result
// becomes: schedule(child, continuation) -- the continuation is a plain
// heap-allocated closure capturing whatever local state that call site
// needed to resume with, pushed onto `pending_` instead of onto the
// native call stack. run()'s driver loop repeatedly pops and invokes one
// pending closure until the root's result is available; no C++ call
// recursion happens regardless of input nesting depth. A fresh
// TermBuilder is constructed per build_term(root) call (see
// Frontend::build_term below) -- it holds no state that needs to survive
// across separate assertions.
//
// A Frontend::Definition's body may itself invoke another definition (or,
// guarded by expanding_definitions_, itself), so definition instantiation
// is folded into this same driver rather than calling Frontend::build_term
// again -- a second, nested synchronous build_term call from within a
// continuation would just reintroduce native recursion one level higher
// up (a chain of nested definition *calls* in the source, not just nested
// definition *bodies*).
class TermBuilder {
 public:
  explicit TermBuilder(Frontend::Implementation& frontend) : frontend_(frontend) {}

  core::TermId run(const smtlib::SExpr& root);

 private:
  using Continuation = std::function<void(core::TermId)>;

  // Arranges for `expr` to be evaluated and `k` invoked with the result,
  // without recursing (pushes a closure onto `pending_`; the driver loop
  // in run() calls it later).
  void schedule(const smtlib::SExpr& expr, Continuation k);

  // Invokes continuation `k` with `value` -- via `pending_`, never
  // directly. This is the other half of making the whole builder
  // non-recursive: `schedule` alone only defers *descending* into a
  // child's evaluation, but a continuation's body routinely calls
  // *another* continuation synchronously once it has a combined result
  // (e.g. dispatch_plus's "all children ready" callback calling k with
  // mk_plus(...)) -- chained deeply enough (e.g. a long `+`/`let` chain),
  // those direct calls alone reconstruct exactly the same native
  // recursion this class exists to avoid. Routing every delivery through
  // `pending_` here keeps each individual call O(1) stack depth
  // regardless of how many levels of nesting ultimately resolve.
  void resume(Continuation k, core::TermId value);

  // Evaluates every expression in `exprs`, in order, then invokes
  // `done` with all their TermIds (same order) -- the iterative
  // replacement for `for (child : children) results.push_back(build_term(child));`.
  void schedule_all(std::shared_ptr<std::vector<const smtlib::SExpr*>> exprs,
                    std::function<void(std::vector<core::TermId>)> done);
  void schedule_all_from(std::shared_ptr<std::vector<const smtlib::SExpr*>> exprs,
                         std::size_t index, std::vector<core::TermId> results,
                         std::function<void(std::vector<core::TermId>)> done);

  static std::shared_ptr<std::vector<const smtlib::SExpr*>> collect(const smtlib::SExpr& expr,
                                                                    std::size_t start);

  void dispatch(const smtlib::SExpr& expr, Continuation k);
  void dispatch_let(const smtlib::SExpr& expr, Continuation k);
  void dispatch_ite(const smtlib::SExpr& expr, Continuation k);
  void finish_ite(core::TermId condition, core::TermId then_term, core::TermId else_term,
                  Continuation k);
  void dispatch_implies(const smtlib::SExpr& expr, Continuation k);
  void dispatch_xor(const smtlib::SExpr& expr, Continuation k);
  void dispatch_equality(const smtlib::SExpr& expr, Continuation k);
  core::TermId finish_pairwise_equality_to_ite(core::TermId term, core::TermId condition,
                                               core::TermId then_term, core::TermId else_term);
  void dispatch_distinct(const smtlib::SExpr& expr, Continuation k);
  void dispatch_plus(const smtlib::SExpr& expr, Continuation k);
  void dispatch_minus(const smtlib::SExpr& expr, Continuation k);
  void dispatch_times(const smtlib::SExpr& expr, Continuation k);
  void dispatch_divide(const smtlib::SExpr& expr, Continuation k);
  void dispatch_comparison(const smtlib::SExpr& expr, const std::string& head, Continuation k);
  void dispatch_instantiate_definition(const std::string& name, std::vector<core::TermId> args,
                                       Continuation k);

  Frontend::Implementation& frontend_;
  std::vector<std::function<void()>> pending_;
  std::optional<core::TermId> result_;
};

struct Frontend::Implementation {
 public:
  friend class TermBuilder;

  core::TermId build_term(const smtlib::SExpr& expr) {
    TermBuilder builder(*this);
    return builder.run(expr);
  }

  void process_command(const smtlib::Command& command, std::vector<core::TermId>& assertions) {
    switch (command.kind) {
      case smtlib::CommandKind::Ignored:
        return;
      case smtlib::CommandKind::SetOption:
        if (command.option == ":produce-models") {
          if (command.value == "true") {
            produce_models_ = true;
          } else if (command.value == "false") {
            produce_models_ = false;
          } else {
            throw UnsupportedInput(":produce-models expects true or false");
          }
        }
        return;
      case smtlib::CommandKind::SetLogic:
        if (command.logic != "QF_UF" && command.logic != "QF_LRA" && command.logic != "QF_UFLRA" &&
            command.logic != "QF_LIA" && command.logic != "QF_UFLIA") {
          throw UnsupportedInput("unsupported logic: " + command.logic);
        }
        logic_ = command.logic;
        return;
      case smtlib::CommandKind::DeclareSort:
        symbols_.declare_sort(command.name);
        return;
      case smtlib::CommandKind::DeclareFun:
        declare_fun(command);
        return;
      case smtlib::CommandKind::DefineFun:
        define_fun(command);
        return;
      case smtlib::CommandKind::Assert:
        clear_side_assertion_capture();
        {
          const core::TermId assertion = build_term(command.assertion);
          for (const core::TermId side_assertion : side_assertions_) {
            if (emitted_side_assertions_.insert(side_assertion).second) {
              assertions.push_back(side_assertion);
            }
          }
          assertions.push_back(assertion);
        }
        clear_side_assertion_capture();
        return;
      case smtlib::CommandKind::CheckSat:
        saw_check_sat_ = true;
        return;
      case smtlib::CommandKind::GetModel:
        if (!saw_check_sat_) {
          throw UnsupportedInput("get-model before check-sat is unsupported");
        }
        requested_model_ = true;
        return;
    }
  }

  const std::optional<std::string>& logic() const { return logic_; }
  bool saw_check_sat() const { return saw_check_sat_; }
  bool requested_model() const { return requested_model_; }
  bool produce_models() const { return produce_models_; }
  bool has_declared_real_constants() const { return !declared_real_constant_symbols_.empty(); }
  bool has_declared_int_constants() const { return !declared_int_constant_symbols_.empty(); }

  // Materialises a term per declared Real constant. Only call once the search
  // is over: mk_constant interns, so this assigns TermIds. Constants that
  // already occur in an assertion keep the id they were interned with.
  std::vector<core::TermId> declared_real_constant_terms() {
    std::vector<core::TermId> model_terms;
    model_terms.reserve(declared_real_constant_symbols_.size());
    for (const core::SymbolId symbol : declared_real_constant_symbols_) {
      model_terms.push_back(terms_.mk_constant(symbol));
    }
    return model_terms;
  }

  // Same deferred-materialisation reasoning as declared_real_constant_terms(),
  // for QF_LIA's Int constants.
  std::vector<core::TermId> declared_int_constant_terms() {
    std::vector<core::TermId> model_terms;
    model_terms.reserve(declared_int_constant_symbols_.size());
    for (const core::SymbolId symbol : declared_int_constant_symbols_) {
      model_terms.push_back(terms_.mk_constant(symbol));
    }
    return model_terms;
  }
  const core::SymbolTable& symbols() const { return symbols_; }
  const core::TermManager& terms() const { return terms_; }
  core::TermManager& terms() { return terms_; }
  std::size_t boolean_ites() const { return boolean_ites_; }
  std::size_t auxiliary_ites() const { return auxiliary_ites_; }
  std::size_t nullary_definition_direct_uses() const { return nullary_definition_direct_uses_; }
  std::size_t nullary_definition_eager_compiles() const {
    return nullary_definition_eager_compiles_;
  }
  std::size_t nullary_definition_side_assertions() const {
    return nullary_definition_side_assertions_;
  }
  core::TermId fresh_internal_constant(core::SortId sort, std::string_view prefix) {
    return fresh_auxiliary_constant(sort, prefix);
  }

 private:
  struct Definition {
    std::vector<std::string> parameters;
    std::vector<core::SortId> parameter_sorts;
    core::SortId result_sort = core::kInvalidSortId;
    std::unique_ptr<smtlib::SExpr> body;
    core::TermId compiled_nullary = core::kInvalidTermId;
    std::vector<core::TermId> compiled_side_assertions;
  };

  void clear_side_assertion_capture() {
    side_assertions_.clear();
    side_assertion_ids_.clear();
  }

  void record_side_assertion(core::TermId assertion) {
    if (side_assertion_ids_.insert(assertion).second) {
      side_assertions_.push_back(assertion);
    }
  }

  void replay_side_assertions(const std::vector<core::TermId>& assertions) {
    for (const core::TermId assertion : assertions) {
      record_side_assertion(assertion);
    }
  }

  std::optional<core::TermId> lookup_bound_term(const std::string& name) const {
    for (auto scope = let_scopes_.rbegin(); scope != let_scopes_.rend(); ++scope) {
      const auto found = scope->find(name);
      if (found != scope->end()) {
        return found->second;
      }
    }
    return std::nullopt;
  }

  static void require_arity(const smtlib::SExpr& expr, std::size_t arity, std::string_view op) {
    if (expr.list.size() != arity) {
      throw UnsupportedInput(std::string(op) + " has wrong arity");
    }
  }

  static bool has_head(const smtlib::SExpr& expr, std::string_view head) {
    return expr.kind == smtlib::SExpr::Kind::List && !expr.list.empty() &&
           expr.list[0].kind == smtlib::SExpr::Kind::Atom && expr.list[0].atom == head;
  }

  core::TermId build_pairwise_equality(core::TermId lhs, core::TermId rhs) {
    const core::SortId lhs_sort = terms_.get(lhs).sort;
    if (lhs_sort != terms_.get(rhs).sort) {
      throw UnsupportedInput("equality sort mismatch");
    }
    if (lhs_sort == symbols_.bool_sort()) {
      // UF applications do not change Bool equality into an independent theory atom: EUF
      // receives each Boolean application's SAT-selected canonical value separately.
      return terms_.mk_and({terms_.mk_implies(lhs, rhs), terms_.mk_implies(rhs, lhs)});
    }
    return terms_.mk_eq(lhs, rhs);
  }

  core::TermId build_pairwise_disequality(core::TermId lhs, core::TermId rhs) {
    const core::SortId lhs_sort = terms_.get(lhs).sort;
    if (lhs_sort != terms_.get(rhs).sort) {
      throw UnsupportedInput("distinct sort mismatch");
    }
    if (lhs_sort == symbols_.bool_sort()) {
      return terms_.mk_or(
          {terms_.mk_and({lhs, terms_.mk_not(rhs)}), terms_.mk_and({terms_.mk_not(lhs), rhs})});
    }
    return terms_.mk_not(terms_.mk_eq(lhs, rhs));
  }

  core::TermId bind_bool_argument_if_needed(core::TermId term) {
    const core::Term& node = terms_.get(term);
    if (node.sort != symbols_.bool_sort() || node.kind == core::TermKind::BoolValue ||
        node.kind == core::TermKind::Constant) {
      return term;
    }
    const core::TermId auxiliary = fresh_auxiliary_constant(symbols_.bool_sort(), "bool_arg");
    record_side_assertion(
        terms_.mk_and({terms_.mk_implies(auxiliary, term), terms_.mk_implies(term, auxiliary)}));
    return auxiliary;
  }

  core::SortId lookup_sort_name(const std::string& name) const {
    const auto sort = symbols_.lookup_sort(name);
    if (!sort.has_value()) {
      throw UnsupportedInput("unknown sort: " + name);
    }
    return sort.value();
  }

  void declare_fun(const smtlib::Command& command) {
    std::vector<core::SortId> arg_sorts;
    for (const std::string& name : command.arg_sorts) {
      arg_sorts.push_back(lookup_sort_name(name));
    }
    const core::SortId result_sort = lookup_sort_name(command.result_sort);
    if (logic_ == "QF_LRA" && !arg_sorts.empty()) {
      throw UnsupportedInput("QF_LRA supports free constants but not uninterpreted functions");
    }
    const bool integer_logic = logic_ == "QF_LIA" || logic_ == "QF_UFLIA";
    if (logic_ == "QF_LIA") {
      if (!arg_sorts.empty()) {
        throw UnsupportedInput("QF_LIA supports free constants but not uninterpreted functions");
      }
    }
    if (integer_logic) {
      if (std::find(arg_sorts.begin(), arg_sorts.end(), symbols_.real_sort()) != arg_sorts.end()) {
        throw UnsupportedInput("integer arithmetic logics do not support Real arguments");
      }
      if (result_sort == symbols_.real_sort()) {
        throw UnsupportedInput("integer arithmetic logics do not support Real-valued symbols");
      }
    }
    if (arg_sorts.empty() && result_sort == symbols_.int_sort() && !integer_logic) {
      throw UnsupportedInput("Int constants require an integer arithmetic logic");
    }
    if (arg_sorts.empty()) {
      const core::SymbolId symbol = symbols_.declare_constant(command.name, result_sort);
      if (result_sort == symbols_.real_sort()) {
        // Record the symbol, not a term. Interning a term here would allocate
        // TermIds in *declaration* order rather than first-use order, and
        // preprocessing's Gaussian elimination pivots in ascending TermId
        // order (collect_gaussian_elimination_candidates sorts
        // eligible_variables), so doing it eagerly silently changes which
        // substitutions preprocessing accepts -- on miplib/vpm2-0 that moved
        // :preprocessing.solve-eqs.rewrites 390 -> 252 and :lra.pivots
        // 26890 -> 12537, on every run, whether or not a model was asked for.
        // Terms are materialised in declared_real_constant_terms(), after
        // solving, where they cannot perturb the search.
        declared_real_constant_symbols_.push_back(symbol);
      } else if (result_sort == symbols_.int_sort()) {
        declared_int_constant_symbols_.push_back(symbol);
      }
    } else if (result_sort == symbols_.bool_sort()) {
      symbols_.declare_predicate(command.name, std::move(arg_sorts));
    } else {
      symbols_.declare_function(command.name, std::move(arg_sorts), result_sort);
    }
  }

  void define_fun(const smtlib::Command& command) {
    if (definitions_.contains(command.name) || symbols_.lookup_symbol(command.name).has_value()) {
      throw UnsupportedInput("duplicate function definition: " + command.name);
    }
    Definition definition;
    definition.parameters.reserve(command.parameters.size());
    definition.parameter_sorts.reserve(command.parameters.size());
    for (const smtlib::SortedVar& parameter : command.parameters) {
      definition.parameters.push_back(parameter.name);
      definition.parameter_sorts.push_back(lookup_sort_name(parameter.sort));
    }
    definition.result_sort = lookup_sort_name(command.result_sort);
    if (definition.parameters.empty()) {
      clear_side_assertion_capture();
      core::TermId body = core::kInvalidTermId;
      try {
        body = build_term(command.body);
      } catch (...) {
        clear_side_assertion_capture();
        throw;
      }
      if (terms_.get(body).sort != definition.result_sort) {
        clear_side_assertion_capture();
        throw UnsupportedInput("define-fun result sort mismatch: " + command.name);
      }
      definition.compiled_nullary = body;
      definition.compiled_side_assertions = std::move(side_assertions_);
      side_assertion_ids_.clear();
      ++nullary_definition_eager_compiles_;
      nullary_definition_side_assertions_ += definition.compiled_side_assertions.size();
    } else {
      definition.body = std::make_unique<smtlib::SExpr>(command.body);
    }
    definitions_.emplace(command.name, std::move(definition));
  }

  // Definition instantiation's actual body-building (which used to be
  // here, calling build_term(definition.body) recursively) now lives in
  // TermBuilder::dispatch_instantiate_definition, folded into the same
  // iterative driver as everything else -- see the TermBuilder class
  // comment above Frontend for why a second nested build_term call
  // wouldn't have actually fixed the recursion.

  core::TermId fresh_auxiliary_constant(core::SortId sort, std::string_view prefix) {
    while (true) {
      const std::string name =
          std::string{"$primo_"} + std::string{prefix} + "_" + std::to_string(auxiliary_counter_++);
      if (symbols_.lookup_symbol(name).has_value()) {
        continue;
      }
      return terms_.mk_constant(symbols_.declare_constant(name, sort));
    }
  }

  core::SymbolTable symbols_;
  core::TermManager terms_{symbols_};
  std::vector<std::unordered_map<std::string, core::TermId>> let_scopes_;
  std::unordered_map<std::string, Definition> definitions_;
  std::unordered_set<std::string> expanding_definitions_;
  std::vector<core::TermId> side_assertions_;
  std::unordered_set<core::TermId> side_assertion_ids_;
  std::unordered_set<core::TermId> emitted_side_assertions_;
  std::vector<core::SymbolId> declared_real_constant_symbols_;
  std::vector<core::SymbolId> declared_int_constant_symbols_;
  std::optional<std::string> logic_;
  std::size_t auxiliary_counter_ = 0;
  std::size_t boolean_ites_ = 0;
  std::size_t auxiliary_ites_ = 0;
  std::size_t nullary_definition_direct_uses_ = 0;
  std::size_t nullary_definition_eager_compiles_ = 0;
  std::size_t nullary_definition_side_assertions_ = 0;
  bool saw_check_sat_ = false;
  bool requested_model_ = false;
  bool produce_models_ = false;
};

core::TermId TermBuilder::run(const smtlib::SExpr& root) {
  schedule(root, [this](core::TermId id) { result_ = id; });
  while (!result_.has_value()) {
    std::function<void()> job = std::move(pending_.back());
    pending_.pop_back();
    job();
  }
  return result_.value();
}

void TermBuilder::schedule(const smtlib::SExpr& expr, Continuation k) {
  pending_.push_back([this, &expr, k = std::move(k)]() mutable { dispatch(expr, std::move(k)); });
}

void TermBuilder::resume(Continuation k, core::TermId value) {
  pending_.push_back([k = std::move(k), value]() { k(value); });
}

void TermBuilder::schedule_all(std::shared_ptr<std::vector<const smtlib::SExpr*>> exprs,
                               std::function<void(std::vector<core::TermId>)> done) {
  schedule_all_from(std::move(exprs), 0, {}, std::move(done));
}

void TermBuilder::schedule_all_from(std::shared_ptr<std::vector<const smtlib::SExpr*>> exprs,
                                    std::size_t index, std::vector<core::TermId> results,
                                    std::function<void(std::vector<core::TermId>)> done) {
  if (index == exprs->size()) {
    done(std::move(results));
    return;
  }
  schedule(*(*exprs)[index], [this, exprs, index, results = std::move(results),
                              done = std::move(done)](core::TermId r) mutable {
    results.push_back(r);
    schedule_all_from(std::move(exprs), index + 1, std::move(results), std::move(done));
  });
}

std::shared_ptr<std::vector<const smtlib::SExpr*>> TermBuilder::collect(const smtlib::SExpr& expr,
                                                                        std::size_t start) {
  auto exprs = std::make_shared<std::vector<const smtlib::SExpr*>>();
  exprs->reserve(expr.list.size() - start);
  for (std::size_t i = start; i < expr.list.size(); ++i) {
    exprs->push_back(&expr.list[i]);
  }
  return exprs;
}

void TermBuilder::dispatch(const smtlib::SExpr& expr, Continuation k) {
  if (expr.kind == smtlib::SExpr::Kind::Atom) {
    if (expr.atom == "true") {
      resume(std::move(k), frontend_.terms_.mk_bool(true));
      return;
    }
    if (expr.atom == "false") {
      resume(std::move(k), frontend_.terms_.mk_bool(false));
      return;
    }
    if (const auto literal = util::Rational::parse(expr.atom); literal.has_value()) {
      const bool integer_logic = frontend_.logic_ == "QF_LIA" || frontend_.logic_ == "QF_UFLIA";
      const core::SortId numeral_sort =
          integer_logic ? frontend_.symbols_.int_sort() : frontend_.symbols_.real_sort();
      resume(std::move(k), frontend_.terms_.mk_rational(literal.value(), numeral_sort));
      return;
    }
    const auto bound = frontend_.lookup_bound_term(expr.atom);
    if (bound.has_value()) {
      resume(std::move(k), bound.value());
      return;
    }
    if (!frontend_.definitions_.empty()) {
      const auto defined = frontend_.definitions_.find(expr.atom);
      if (defined != frontend_.definitions_.end() && defined->second.parameters.empty()) {
        dispatch_instantiate_definition(expr.atom, {}, std::move(k));
        return;
      }
    }
    const auto symbol = frontend_.symbols_.lookup_symbol(expr.atom);
    if (!symbol.has_value()) {
      throw UnsupportedInput("unknown symbol: " + expr.atom);
    }
    resume(std::move(k), frontend_.terms_.mk_constant(symbol.value()));
    return;
  }

  if (expr.list.empty() || expr.list[0].kind != smtlib::SExpr::Kind::Atom) {
    throw UnsupportedInput("unsupported expression shape");
  }

  const std::string& head = expr.list[0].atom;
  if (head == "not") {
    Frontend::Implementation::require_arity(expr, 2, "not");
    schedule(expr.list[1], [this, k = std::move(k)](core::TermId x) mutable {
      resume(std::move(k), frontend_.terms_.mk_not(x));
    });
    return;
  }
  if (head == "and") {
    schedule_all(collect(expr, 1),
                 [this, k = std::move(k)](std::vector<core::TermId> children) mutable {
                   resume(std::move(k), frontend_.terms_.mk_and(std::move(children)));
                 });
    return;
  }
  if (head == "or") {
    schedule_all(collect(expr, 1),
                 [this, k = std::move(k)](std::vector<core::TermId> children) mutable {
                   resume(std::move(k), frontend_.terms_.mk_or(std::move(children)));
                 });
    return;
  }
  if (head == "=>") {
    dispatch_implies(expr, std::move(k));
    return;
  }
  if (head == "xor") {
    dispatch_xor(expr, std::move(k));
    return;
  }
  if (head == "!") {
    if (expr.list.size() < 2) {
      throw UnsupportedInput("annotated term has no body");
    }
    schedule(expr.list[1], std::move(k));
    return;
  }
  if (head == "ite") {
    dispatch_ite(expr, std::move(k));
    return;
  }
  if (head == "let") {
    dispatch_let(expr, std::move(k));
    return;
  }
  if (head == "=") {
    dispatch_equality(expr, std::move(k));
    return;
  }
  if (head == "distinct") {
    dispatch_distinct(expr, std::move(k));
    return;
  }
  if (head == "+") {
    dispatch_plus(expr, std::move(k));
    return;
  }
  if (head == "-") {
    dispatch_minus(expr, std::move(k));
    return;
  }
  if (head == "*") {
    dispatch_times(expr, std::move(k));
    return;
  }
  if (head == "/") {
    dispatch_divide(expr, std::move(k));
    return;
  }
  if (head == "<=" || head == "<" || head == ">=" || head == ">") {
    dispatch_comparison(expr, head, std::move(k));
    return;
  }

  if (!frontend_.definitions_.empty()) {
    const auto defined = frontend_.definitions_.find(head);
    if (defined != frontend_.definitions_.end()) {
      schedule_all(collect(expr, 1),
                   [this, k = std::move(k), head](std::vector<core::TermId> args) mutable {
                     dispatch_instantiate_definition(head, std::move(args), k);
                   });
      return;
    }
  }

  const auto symbol = frontend_.symbols_.lookup_symbol(head);
  if (!symbol.has_value()) {
    throw UnsupportedInput("unknown function symbol: " + head);
  }
  schedule_all(collect(expr, 1), [this, k = std::move(k), symbol = symbol.value()](
                                     std::vector<core::TermId> raw_args) mutable {
    std::vector<core::TermId> args;
    args.reserve(raw_args.size());
    for (const core::TermId a : raw_args) {
      args.push_back(frontend_.bind_bool_argument_if_needed(a));
    }
    resume(std::move(k), frontend_.terms_.mk_app(symbol, std::move(args)));
  });
}

void TermBuilder::dispatch_let(const smtlib::SExpr& expr, Continuation k) {
  Frontend::Implementation::require_arity(expr, 3, "let");
  if (expr.list[1].kind != smtlib::SExpr::Kind::List) {
    throw UnsupportedInput("let bindings must be a list");
  }
  const smtlib::SExpr& bindings_expr = expr.list[1];
  for (const smtlib::SExpr& binding : bindings_expr.list) {
    if (binding.kind != smtlib::SExpr::Kind::List || binding.list.size() != 2 ||
        binding.list[0].kind != smtlib::SExpr::Kind::Atom) {
      throw UnsupportedInput("malformed let binding");
    }
  }
  auto binding_value_exprs = std::make_shared<std::vector<const smtlib::SExpr*>>();
  binding_value_exprs->reserve(bindings_expr.list.size());
  for (const smtlib::SExpr& binding : bindings_expr.list) {
    binding_value_exprs->push_back(&binding.list[1]);
  }
  schedule_all(binding_value_exprs, [this, &expr, &bindings_expr,
                                     k = std::move(k)](std::vector<core::TermId> values) mutable {
    std::unordered_map<std::string, core::TermId> scope;
    for (std::size_t i = 0; i < bindings_expr.list.size(); ++i) {
      const std::string& name = bindings_expr.list[i].list[0].atom;
      if (!scope.emplace(name, values[i]).second) {
        throw UnsupportedInput("duplicate let binding: " + name);
      }
    }
    frontend_.let_scopes_.push_back(std::move(scope));
    schedule(expr.list[2], [this, k = std::move(k)](core::TermId body) mutable {
      frontend_.let_scopes_.pop_back();
      resume(std::move(k), body);
    });
  });
}

void TermBuilder::dispatch_ite(const smtlib::SExpr& expr, Continuation k) {
  Frontend::Implementation::require_arity(expr, 4, "ite");
  schedule(expr.list[1], [this, &expr, k = std::move(k)](core::TermId condition) mutable {
    if (frontend_.terms_.get(condition).sort != frontend_.symbols_.bool_sort()) {
      throw UnsupportedInput("ite condition is not Boolean");
    }
    auto branches = std::make_shared<std::vector<const smtlib::SExpr*>>(
        std::vector<const smtlib::SExpr*>{&expr.list[2], &expr.list[3]});
    schedule_all(branches, [this, condition,
                            k = std::move(k)](std::vector<core::TermId> branch_terms) mutable {
      finish_ite(condition, branch_terms[0], branch_terms[1], std::move(k));
    });
  });
}

void TermBuilder::finish_ite(core::TermId condition, core::TermId then_term, core::TermId else_term,
                             Continuation k) {
  const core::SortId result_sort = frontend_.terms_.get(then_term).sort;
  if (result_sort != frontend_.terms_.get(else_term).sort) {
    throw UnsupportedInput("ite branch sort mismatch");
  }
  if (result_sort == frontend_.symbols_.bool_sort()) {
    ++frontend_.boolean_ites_;
    resume(std::move(k),
           frontend_.terms_.mk_or(
               {frontend_.terms_.mk_and({condition, then_term}),
                frontend_.terms_.mk_and({frontend_.terms_.mk_not(condition), else_term})}));
    return;
  }
  const core::TermId auxiliary = frontend_.fresh_auxiliary_constant(result_sort, "ite");
  ++frontend_.auxiliary_ites_;
  frontend_.record_side_assertion(
      frontend_.terms_.mk_implies(condition, frontend_.terms_.mk_eq(auxiliary, then_term)));
  frontend_.record_side_assertion(frontend_.terms_.mk_implies(
      frontend_.terms_.mk_not(condition), frontend_.terms_.mk_eq(auxiliary, else_term)));
  resume(std::move(k), auxiliary);
}

void TermBuilder::dispatch_implies(const smtlib::SExpr& expr, Continuation k) {
  if (expr.list.size() < 3) {
    throw UnsupportedInput("=> has wrong arity");
  }
  // Matches the original right-fold's evaluation order: last operand
  // first, then descending down to the first.
  auto exprs = std::make_shared<std::vector<const smtlib::SExpr*>>();
  for (std::size_t idx = expr.list.size() - 1; idx >= 1; --idx) {
    exprs->push_back(&expr.list[idx]);
    if (idx == 1) {
      break;
    }
  }
  schedule_all(exprs, [this, k = std::move(k)](std::vector<core::TermId> reversed_results) mutable {
    core::TermId result = reversed_results[0];
    for (std::size_t j = 1; j < reversed_results.size(); ++j) {
      result = frontend_.terms_.mk_implies(reversed_results[j], result);
    }
    resume(std::move(k), result);
  });
}

void TermBuilder::dispatch_xor(const smtlib::SExpr& expr, Continuation k) {
  if (expr.list.size() < 3) {
    throw UnsupportedInput("xor has wrong arity");
  }
  schedule_all(collect(expr, 1), [this,
                                  k = std::move(k)](std::vector<core::TermId> operands) mutable {
    core::TermId result = operands[0];
    for (std::size_t i = 1; i < operands.size(); ++i) {
      const core::TermId rhs = operands[i];
      result =
          frontend_.terms_.mk_or({frontend_.terms_.mk_and({result, frontend_.terms_.mk_not(rhs)}),
                                  frontend_.terms_.mk_and({frontend_.terms_.mk_not(result), rhs})});
    }
    resume(std::move(k), result);
  });
}

void TermBuilder::dispatch_equality(const smtlib::SExpr& expr, Continuation k) {
  if (expr.list.size() < 3) {
    throw UnsupportedInput("= has wrong arity");
  }

  // Mirrors build_equality's original sequential loop exactly, but
  // instead of calling build_term/build_pairwise_equality_to_ite directly
  // (which would recurse), it walks the SAME control flow purely
  // symbolically -- the branching depends only on has_head() checks on
  // the already-fully-parsed SExpr shape, never on any evaluated term, so
  // it's safe to precompute the whole plan upfront -- recording which
  // atomic sub-expressions need evaluating (as indices into one flat job
  // list) and a parallel step plan to replay, non-recursively, once every
  // job's TermId is available.
  struct Step {
    bool current_is_ite = false;
    bool reuse_previous = false;
    std::size_t lhs_index = 0;
    std::size_t ite_cond_index = 0, ite_then_index = 0, ite_else_index = 0;
    std::size_t current_index = 0;
    bool previous_is_ite = false;
    std::size_t prev_ite_cond_index = 0, prev_ite_then_index = 0, prev_ite_else_index = 0;
  };

  auto flat_exprs = std::make_shared<std::vector<const smtlib::SExpr*>>();
  auto push_expr = [&flat_exprs](const smtlib::SExpr& e) {
    flat_exprs->push_back(&e);
    return flat_exprs->size() - 1;
  };
  auto steps = std::make_shared<std::vector<Step>>();

  const smtlib::SExpr* previous_expr = &expr.list[1];
  bool previous_term_valid = false;

  for (std::size_t i = 2; i < expr.list.size(); ++i) {
    const smtlib::SExpr& current_expr = expr.list[i];
    Step step;
    if (Frontend::Implementation::has_head(current_expr, "ite")) {
      Frontend::Implementation::require_arity(current_expr, 4, "ite");
      step.current_is_ite = true;
      step.reuse_previous = previous_term_valid;
      if (!previous_term_valid) {
        step.lhs_index = push_expr(*previous_expr);
      }
      step.ite_cond_index = push_expr(current_expr.list[1]);
      step.ite_then_index = push_expr(current_expr.list[2]);
      step.ite_else_index = push_expr(current_expr.list[3]);
      steps->push_back(step);
      previous_expr = &current_expr;
      previous_term_valid = false;
      continue;
    }
    step.current_is_ite = false;
    step.current_index = push_expr(current_expr);
    if (Frontend::Implementation::has_head(*previous_expr, "ite")) {
      step.previous_is_ite = true;
      Frontend::Implementation::require_arity(*previous_expr, 4, "ite");
      step.prev_ite_cond_index = push_expr(previous_expr->list[1]);
      step.prev_ite_then_index = push_expr(previous_expr->list[2]);
      step.prev_ite_else_index = push_expr(previous_expr->list[3]);
    } else {
      step.previous_is_ite = false;
      step.reuse_previous = previous_term_valid;
      if (!previous_term_valid) {
        step.lhs_index = push_expr(*previous_expr);
      }
    }
    steps->push_back(step);
    previous_expr = &current_expr;
    previous_term_valid = true;
  }

  schedule_all(
      flat_exprs, [this, k = std::move(k), steps](std::vector<core::TermId> results) mutable {
        std::vector<core::TermId> equalities;
        equalities.reserve(steps->size());
        std::optional<core::TermId> previous_term;
        for (const Step& step : *steps) {
          if (step.current_is_ite) {
            const core::TermId lhs =
                step.reuse_previous ? previous_term.value() : results[step.lhs_index];
            equalities.push_back(finish_pairwise_equality_to_ite(lhs, results[step.ite_cond_index],
                                                                 results[step.ite_then_index],
                                                                 results[step.ite_else_index]));
            previous_term.reset();
            continue;
          }
          const core::TermId current = results[step.current_index];
          if (step.previous_is_ite) {
            equalities.push_back(finish_pairwise_equality_to_ite(
                current, results[step.prev_ite_cond_index], results[step.prev_ite_then_index],
                results[step.prev_ite_else_index]));
          } else {
            const core::TermId lhs =
                step.reuse_previous ? previous_term.value() : results[step.lhs_index];
            equalities.push_back(frontend_.build_pairwise_equality(lhs, current));
          }
          previous_term = current;
        }
        resume(std::move(k), frontend_.terms_.mk_and(std::move(equalities)));
      });
}

core::TermId TermBuilder::finish_pairwise_equality_to_ite(core::TermId term, core::TermId condition,
                                                          core::TermId then_term,
                                                          core::TermId else_term) {
  if (frontend_.terms_.get(condition).sort != frontend_.symbols_.bool_sort()) {
    throw UnsupportedInput("ite condition is not Boolean");
  }
  const core::SortId result_sort = frontend_.terms_.get(then_term).sort;
  if (result_sort != frontend_.terms_.get(else_term).sort) {
    throw UnsupportedInput("ite branch sort mismatch");
  }
  if (frontend_.terms_.get(term).sort != result_sort) {
    throw UnsupportedInput("equality sort mismatch");
  }
  return frontend_.terms_.mk_and(
      {frontend_.terms_.mk_implies(condition, frontend_.build_pairwise_equality(term, then_term)),
       frontend_.terms_.mk_implies(frontend_.terms_.mk_not(condition),
                                   frontend_.build_pairwise_equality(term, else_term))});
}

void TermBuilder::dispatch_distinct(const smtlib::SExpr& expr, Continuation k) {
  schedule_all(collect(expr, 1), [this, k = std::move(k)](std::vector<core::TermId> terms) mutable {
    std::vector<core::TermId> disequalities;
    for (std::size_t i = 0; i < terms.size(); ++i) {
      for (std::size_t j = i + 1; j < terms.size(); ++j) {
        disequalities.push_back(frontend_.build_pairwise_disequality(terms[i], terms[j]));
      }
    }
    if (disequalities.empty()) {
      resume(std::move(k), frontend_.terms_.mk_bool(true));
      return;
    }
    if (disequalities.size() == 1) {
      resume(std::move(k), disequalities.front());
      return;
    }
    resume(std::move(k), frontend_.terms_.mk_and(std::move(disequalities)));
  });
}

void TermBuilder::dispatch_plus(const smtlib::SExpr& expr, Continuation k) {
  if (expr.list.size() < 2) {
    throw UnsupportedInput("+ has wrong arity");
  }
  schedule_all(collect(expr, 1),
               [this, k = std::move(k)](std::vector<core::TermId> children) mutable {
                 resume(std::move(k), frontend_.terms_.mk_plus(std::move(children)));
               });
}

void TermBuilder::dispatch_minus(const smtlib::SExpr& expr, Continuation k) {
  if (expr.list.size() == 2) {
    schedule(expr.list[1], [this, k = std::move(k)](core::TermId x) mutable {
      resume(std::move(k), frontend_.terms_.mk_times(util::Rational(-1), x));
    });
    return;
  }
  if (expr.list.size() < 3) {
    throw UnsupportedInput("- has wrong arity");
  }
  schedule_all(collect(expr, 1),
               [this, k = std::move(k)](std::vector<core::TermId> operands) mutable {
                 std::vector<core::TermId> children;
                 children.reserve(operands.size());
                 children.push_back(operands[0]);
                 for (std::size_t i = 1; i < operands.size(); ++i) {
                   children.push_back(frontend_.terms_.mk_times(util::Rational(-1), operands[i]));
                 }
                 resume(std::move(k), frontend_.terms_.mk_plus(std::move(children)));
               });
}

void TermBuilder::dispatch_times(const smtlib::SExpr& expr, Continuation k) {
  Frontend::Implementation::require_arity(expr, 3, "*");
  schedule_all(
      collect(expr, 1), [this, k = std::move(k)](std::vector<core::TermId> operands) mutable {
        const core::TermId lhs = operands[0];
        const core::TermId rhs = operands[1];
        const bool lhs_is_literal = frontend_.terms_.get(lhs).kind == core::TermKind::Rational;
        const bool rhs_is_literal = frontend_.terms_.get(rhs).kind == core::TermKind::Rational;
        if (lhs_is_literal && rhs_is_literal) {
          resume(std::move(k),
                 frontend_.terms_.mk_rational(frontend_.terms_.get(lhs).rational_value *
                                                  frontend_.terms_.get(rhs).rational_value,
                                              frontend_.terms_.get(lhs).sort));
          return;
        }
        if (lhs_is_literal) {
          resume(std::move(k),
                 frontend_.terms_.mk_times(frontend_.terms_.get(lhs).rational_value, rhs));
          return;
        }
        if (rhs_is_literal) {
          resume(std::move(k),
                 frontend_.terms_.mk_times(frontend_.terms_.get(rhs).rational_value, lhs));
          return;
        }
        throw UnsupportedInput("* requires a literal rational coefficient operand (QF_LRA)");
      });
}

void TermBuilder::dispatch_divide(const smtlib::SExpr& expr, Continuation k) {
  Frontend::Implementation::require_arity(expr, 3, "/");
  schedule_all(collect(expr, 1), [this,
                                  k = std::move(k)](std::vector<core::TermId> operands) mutable {
    const core::TermId lhs = operands[0];
    const core::TermId rhs = operands[1];
    if (frontend_.terms_.get(lhs).sort == frontend_.symbols_.int_sort() ||
        frontend_.terms_.get(rhs).sort == frontend_.symbols_.int_sort()) {
      throw UnsupportedInput(
          "/ is a Real operator; integer arithmetic has no supported division (div/mod "
          "unsupported)");
    }
    // Only the divisor has to be a literal. Dividing a linear term by a
    // nonzero rational is linear -- (/ t c) is (* (1/c) t) -- so it belongs in
    // QF_LRA, and both z3 and Yices accept it. Requiring the dividend to be a
    // literal too rejected ordinary input such as (/ x 2.0).
    if (frontend_.terms_.get(rhs).kind != core::TermKind::Rational) {
      throw UnsupportedInput("/ requires a literal rational divisor (QF_LRA)");
    }
    const util::Rational& denominator = frontend_.terms_.get(rhs).rational_value;
    if (denominator.is_zero()) {
      throw UnsupportedInput("division by zero is not a valid QF_LRA literal");
    }
    if (frontend_.terms_.get(lhs).kind == core::TermKind::Rational) {
      resume(std::move(k),
             frontend_.terms_.mk_rational(frontend_.terms_.get(lhs).rational_value / denominator));
      return;
    }
    resume(std::move(k), frontend_.terms_.mk_times(util::Rational(1) / denominator, lhs));
  });
}

void TermBuilder::dispatch_comparison(const smtlib::SExpr& expr, const std::string& head,
                                      Continuation k) {
  if (expr.list.size() < 3) {
    throw UnsupportedInput(head + " has wrong arity");
  }
  const bool strict = head == "<" || head == ">";
  const bool flipped = head == ">=" || head == ">";
  schedule_all(collect(expr, 1), [this, k = std::move(k), strict,
                                  flipped](std::vector<core::TermId> operands) mutable {
    std::vector<core::TermId> atoms;
    atoms.reserve(operands.size() - 1);
    for (std::size_t i = 0; i + 1 < operands.size(); ++i) {
      const core::TermId lhs = flipped ? operands[i + 1] : operands[i];
      const core::TermId rhs = flipped ? operands[i] : operands[i + 1];
      atoms.push_back(strict ? frontend_.terms_.mk_lt(lhs, rhs) : frontend_.terms_.mk_le(lhs, rhs));
    }
    if (atoms.size() == 1) {
      resume(std::move(k), atoms.front());
      return;
    }
    resume(std::move(k), frontend_.terms_.mk_and(std::move(atoms)));
  });
}

void TermBuilder::dispatch_instantiate_definition(const std::string& name,
                                                  std::vector<core::TermId> args, Continuation k) {
  const auto found = frontend_.definitions_.find(name);
  if (found == frontend_.definitions_.end()) {
    throw UnsupportedInput("unknown definition: " + name);
  }
  if (frontend_.expanding_definitions_.contains(name)) {
    throw UnsupportedInput("recursive define-fun is unsupported: " + name);
  }
  const auto& definition = found->second;
  if (args.size() != definition.parameters.size()) {
    throw UnsupportedInput("define-fun application has wrong arity: " + name);
  }
  if (definition.parameters.empty()) {
    assert(definition.compiled_nullary != core::kInvalidTermId);
    ++frontend_.nullary_definition_direct_uses_;
    frontend_.replay_side_assertions(definition.compiled_side_assertions);
    resume(std::move(k), definition.compiled_nullary);
    return;
  }

  std::unordered_map<std::string, core::TermId> scope;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (frontend_.terms_.get(args[i]).sort != definition.parameter_sorts[i]) {
      throw UnsupportedInput("define-fun argument sort mismatch: " + name);
    }
    if (!scope.emplace(definition.parameters[i], args[i]).second) {
      throw UnsupportedInput("duplicate define-fun parameter: " + definition.parameters[i]);
    }
  }

  frontend_.expanding_definitions_.insert(name);
  frontend_.let_scopes_.push_back(std::move(scope));
  const core::SortId result_sort = definition.result_sort;
  assert(definition.body != nullptr);
  schedule(*definition.body,
           [this, k = std::move(k), name, result_sort](core::TermId body) mutable {
             frontend_.let_scopes_.pop_back();
             frontend_.expanding_definitions_.erase(name);
             if (frontend_.terms_.get(body).sort != result_sort) {
               throw UnsupportedInput("define-fun result sort mismatch: " + name);
             }
             resume(std::move(k), body);
           });
}

Frontend::Frontend() : implementation_(std::make_unique<Implementation>()) {}
Frontend::~Frontend() = default;

void Frontend::process_command(const smtlib::Command& command,
                               std::vector<core::TermId>& assertions) {
  try {
    implementation_->process_command(command, assertions);
  } catch (const std::invalid_argument& error) {
    // Core constructors deliberately use invalid_argument for violated sort,
    // arity, and declaration preconditions. At the SMT-LIB translation
    // boundary those violations describe unsupported user input; exceptions
    // raised later by preprocessing and solver internals remain uncaught.
    throw UnsupportedInput(error.what());
  }
}

const std::optional<std::string>& Frontend::logic() const { return implementation_->logic(); }
bool Frontend::saw_check_sat() const { return implementation_->saw_check_sat(); }
bool Frontend::requested_model() const { return implementation_->requested_model(); }
bool Frontend::produce_models() const { return implementation_->produce_models(); }
bool Frontend::has_declared_real_constants() const {
  return implementation_->has_declared_real_constants();
}
bool Frontend::has_declared_int_constants() const {
  return implementation_->has_declared_int_constants();
}
std::vector<core::TermId> Frontend::declared_real_constant_terms() {
  return implementation_->declared_real_constant_terms();
}
std::vector<core::TermId> Frontend::declared_int_constant_terms() {
  return implementation_->declared_int_constant_terms();
}
const core::TermManager& Frontend::terms() const { return implementation_->terms(); }
core::TermManager& Frontend::terms() { return implementation_->terms(); }
std::size_t Frontend::boolean_ites() const { return implementation_->boolean_ites(); }
std::size_t Frontend::auxiliary_ites() const { return implementation_->auxiliary_ites(); }
std::size_t Frontend::nullary_definition_direct_uses() const {
  return implementation_->nullary_definition_direct_uses();
}
std::size_t Frontend::nullary_definition_eager_compiles() const {
  return implementation_->nullary_definition_eager_compiles();
}
std::size_t Frontend::nullary_definition_side_assertions() const {
  return implementation_->nullary_definition_side_assertions();
}
core::TermId Frontend::fresh_internal_constant(core::SortId sort, std::string_view prefix) {
  return implementation_->fresh_internal_constant(sort, prefix);
}

}  // namespace primo::smt
