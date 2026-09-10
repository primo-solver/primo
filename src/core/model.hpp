#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "core/ids.hpp"
#include "core/sort.hpp"
#include "core/symbol_table.hpp"
#include "util/rational.hpp"

namespace primo::core {

class TermManager;
struct Term;

class ModelError : public std::logic_error {
 public:
  explicit ModelError(const std::string& message) : std::logic_error("model: " + message) {}
};

struct DomainValue {
  std::size_t index = 0;
  bool operator==(const DomainValue&) const = default;
};

struct ModelValue {
  SortId sort = kInvalidSortId;
  std::variant<bool, util::Rational, DomainValue> payload = false;
  bool operator==(const ModelValue&) const = default;
};

struct ModelTupleHash {
  std::size_t operator()(const std::vector<ModelValue>& values) const;
};

struct ModelRow {
  std::vector<ModelValue> arguments;
  ModelValue result;
};

// For constants, default_value is their value and rows must be empty.
struct ModelInterpretation {
  SymbolId symbol = kInvalidSymbolId;
  ModelValue default_value;
  std::vector<ModelRow> rows;
};

// Owning, immutable interpretation. Original design: docs/model-interpretation.md.
// Construction checks typing and tuple uniqueness; reads never complete values.
class Model {
 public:
  Model(const SymbolTable& symbols, std::vector<ModelInterpretation> interpretations,
        std::optional<std::vector<SymbolId>> visible_symbols = std::nullopt);
  const std::vector<SymbolId>& visible_symbols() const { return visible_symbols_; }
  const std::vector<Sort>& sorts() const { return sorts_; }
  const std::vector<Symbol>& symbols() const { return symbols_; }
  const std::vector<ModelInterpretation>& interpretations() const { return interpretations_; }
  ModelValue apply(SymbolId symbol, std::span<const ModelValue> arguments = {}) const;

 private:
  void require_value(const ModelValue& value, SortId expected) const;
  std::vector<Sort> sorts_;
  std::vector<Symbol> symbols_;
  std::vector<SymbolId> visible_symbols_;
  std::vector<ModelInterpretation> interpretations_;
  std::unordered_map<SymbolId, std::size_t> by_symbol_;
  std::vector<std::unordered_map<std::vector<ModelValue>, ModelValue, ModelTupleHash>> tables_;
};

// A memo is valid only for this frozen model and this live term manager.
class ModelEvaluator {
 public:
  using Resolver = std::function<ModelValue(const Term&, std::span<const ModelValue>)>;
  ModelEvaluator(const TermManager& terms, const Model& model);
  // Construction adapter: the resolver may only resolve constants/applications.
  // Its previously returned values must remain stable for this evaluator's life.
  ModelEvaluator(const TermManager& terms, Resolver resolver);
  ModelValue evaluate(TermId root);
  std::size_t evaluated_terms() const { return memo_.size(); }

 private:
  const TermManager& terms_;
  Resolver resolve_;
  std::unordered_map<TermId, ModelValue> memo_;
};

}  // namespace primo::core
