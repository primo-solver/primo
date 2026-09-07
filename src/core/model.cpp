#include "core/model.hpp"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/ids.hpp"
#include "core/sort.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "util/rational.hpp"

namespace primo::core {

std::size_t ModelTupleHash::operator()(const std::vector<ModelValue>& values) const {
  std::size_t hash = values.size();
  const auto mix = [&hash](std::size_t part) {
    hash ^= part + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
  };
  for (const ModelValue& value : values) {
    mix(value.sort);
    mix(value.payload.index());
    if (const auto* boolean = std::get_if<bool>(&value.payload)) {
      mix(std::hash<bool>{}(*boolean));
    } else if (const auto* number = std::get_if<util::Rational>(&value.payload)) {
      mix(util::RationalHash{}(*number));
    } else {
      mix(std::get<DomainValue>(value.payload).index);
    }
  }
  return hash;
}

void Model::require_value(const ModelValue& value, SortId expected) const {
  if (value.sort != expected || expected >= sorts_.size()) {
    throw ModelError("value sort mismatch");
  }
  const auto kind = sorts_[expected].kind;
  const auto* number = std::get_if<util::Rational>(&value.payload);
  const bool valid =
      (kind == SortKind::Bool && std::holds_alternative<bool>(value.payload)) ||
      (kind == SortKind::Uninterpreted && std::holds_alternative<DomainValue>(value.payload)) ||
      (kind == SortKind::Real && number != nullptr) ||
      (kind == SortKind::Int && number != nullptr && number->is_integer());
  if (!valid) {
    throw ModelError("value payload does not inhabit its sort");
  }
}

Model::Model(const SymbolTable& symbols, std::vector<ModelInterpretation> interpretations)
    : sorts_(symbols.sorts()),
      symbols_(symbols.symbols()),
      interpretations_(std::move(interpretations)),
      tables_(interpretations_.size()) {
  for (std::size_t index = 0; index < interpretations_.size(); ++index) {
    const auto& interpretation = interpretations_[index];
    if (interpretation.symbol >= symbols_.size() ||
        !by_symbol_.emplace(interpretation.symbol, index).second) {
      throw ModelError("invalid or duplicate symbol interpretation");
    }
    const auto& symbol = symbols_[interpretation.symbol];
    require_value(interpretation.default_value, symbol.result_sort);
    if (symbol.kind == SymbolKind::Constant && !interpretation.rows.empty()) {
      throw ModelError("constant interpretation has function rows");
    }
    for (const auto& row : interpretation.rows) {
      if (row.arguments.size() != symbol.arg_sorts.size()) {
        throw ModelError("function row arity mismatch for " + symbol.name);
      }
      for (std::size_t arg = 0; arg < row.arguments.size(); ++arg) {
        require_value(row.arguments[arg], symbol.arg_sorts[arg]);
      }
      require_value(row.result, symbol.result_sort);
      const auto [found, inserted] = tables_[index].emplace(row.arguments, row.result);
      if (!inserted && found->second != row.result) {
        throw ModelError("conflicting function rows for " + symbol.name);
      }
    }
  }
  for (const auto& symbol : symbols_) {
    if (!symbol.name.starts_with("$primo_") && !by_symbol_.contains(symbol.id)) {
      throw ModelError("missing interpretation for " + symbol.name);
    }
  }
}

ModelValue Model::apply(SymbolId symbol_id, std::span<const ModelValue> arguments) const {
  const auto found = by_symbol_.find(symbol_id);
  if (found == by_symbol_.end()) {
    throw ModelError("missing interpretation for symbol " + std::to_string(symbol_id));
  }
  const auto& symbol = symbols_.at(symbol_id);
  if (arguments.size() != symbol.arg_sorts.size()) {
    throw ModelError("application arity mismatch for " + symbol.name);
  }
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    require_value(arguments[index], symbol.arg_sorts[index]);
  }
  if (symbol.kind == SymbolKind::Constant) return interpretations_[found->second].default_value;
  const auto& table = tables_[found->second];
  const auto row = table.find(std::vector<ModelValue>(arguments.begin(), arguments.end()));
  return row == table.end() ? interpretations_[found->second].default_value : row->second;
}

ModelValue ModelEvaluator::evaluate(TermId root) {
  // Original explicit DAG evaluator; no reconstruction maps or mutable model
  // state are consulted. See docs/model-interpretation.md for the invariant.
  struct Frame {
    TermId term;
    bool expanded;
  };
  std::vector<Frame> pending{{root, false}};
  while (!pending.empty()) {
    const Frame frame = pending.back();
    if (memo_.contains(frame.term)) {
      pending.pop_back();
      continue;
    }
    const Term& node = terms_.get(frame.term);
    const auto boolean = [this](TermId term) { return std::get<bool>(memo_.at(term).payload); };
    const auto number = [this](TermId term) {
      return std::get<util::Rational>(memo_.at(term).payload);
    };
    if (node.kind == TermKind::Ite) {
      const TermId condition = node.children.at(0);
      if (!memo_.contains(condition)) {
        pending.push_back({condition, false});
        continue;
      }
      const TermId selected = node.children.at(boolean(condition) ? 1 : 2);
      if (!memo_.contains(selected)) {
        pending.push_back({selected, false});
        continue;
      }
      memo_.emplace(frame.term, memo_.at(selected));
      pending.pop_back();
      continue;
    }
    if (!frame.expanded && !node.children.empty()) {
      pending.back().expanded = true;
      for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
        if (!memo_.contains(*child)) pending.push_back({*child, false});
      }
      continue;
    }
    ModelValue value{node.sort, false};
    switch (node.kind) {
      case TermKind::BoolValue:
        value.payload = node.bool_value;
        break;
      case TermKind::Rational:
        value.payload = node.rational_value;
        break;
      case TermKind::Constant:
      case TermKind::Application: {
        std::vector<ModelValue> arguments;
        arguments.reserve(node.children.size());
        for (const TermId child : node.children) arguments.push_back(memo_.at(child));
        value = model_.apply(node.symbol, arguments);
        break;
      }
      case TermKind::Equality:
        value.payload = memo_.at(node.children.at(0)) == memo_.at(node.children.at(1));
        break;
      case TermKind::Not:
        value.payload = !boolean(node.children.at(0));
        break;
      case TermKind::And:
      case TermKind::Or: {
        bool result = node.kind == TermKind::And;
        for (const TermId child : node.children) {
          result = node.kind == TermKind::And ? result && boolean(child) : result || boolean(child);
        }
        value.payload = result;
        break;
      }
      case TermKind::Implies:
        value.payload = !boolean(node.children.at(0)) || boolean(node.children.at(1));
        break;
      case TermKind::Plus: {
        util::Rational result(0);
        for (const TermId child : node.children) result += number(child);
        value.payload = std::move(result);
        break;
      }
      case TermKind::Times:
        value.payload = node.rational_value * number(node.children.at(0));
        break;
      case TermKind::Le:
        value.payload = number(node.children.at(0)) <= number(node.children.at(1));
        break;
      case TermKind::Lt:
        value.payload = number(node.children.at(0)) < number(node.children.at(1));
        break;
      case TermKind::Abs: {
        const auto operand = number(node.children.at(0));
        value.payload = operand.sign() < 0 ? -operand : operand;
        break;
      }
      case TermKind::IntDiv:
      case TermKind::IntMod: {
        if (node.rational_value.sign() <= 0) throw ModelError("nonpositive normalized divisor");
        const auto operand = number(node.children.at(0));
        const auto quotient = (operand / node.rational_value).floor();
        value.payload =
            node.kind == TermKind::IntDiv ? quotient : operand - node.rational_value * quotient;
        break;
      }
      case TermKind::Ite:
        throw ModelError("unexpected ITE evaluation frame");
    }
    memo_.emplace(frame.term, std::move(value));
    pending.pop_back();
  }
  return memo_.at(root);
}

}  // namespace primo::core
