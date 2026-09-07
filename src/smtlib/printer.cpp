#include "smtlib/printer.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "core/model.hpp"
#include "core/sort.hpp"
#include "core/symbol_table.hpp"
#include "util/rational.hpp"

namespace primo::smtlib {
namespace {

std::string identifier(const std::string& name) {
  if (name.size() >= 2 && name.front() == '|' && name.back() == '|') return name;
  const std::string_view punctuation = "~!@$%^&*_-+=<>.?/";
  const bool simple = !name.empty() && !std::isdigit(static_cast<unsigned char>(name.front())) &&
                      std::ranges::all_of(name, [&](unsigned char ch) {
                        return std::isalnum(ch) ||
                               punctuation.find(static_cast<char>(ch)) != std::string_view::npos;
                      });
  static const std::unordered_set<std::string> reserved{"true",  "false", "let", "forall", "exists",
                                                        "match", "par",   "as",  "_",      "!"};
  if (simple && !reserved.contains(name)) return name;
  if (name.find_first_of("|\\") != std::string::npos) {
    throw core::ModelError("identifier cannot be represented in SMT-LIB");
  }
  return "|" + name + "|";
}

std::string numeral(const util::Rational& value) {
  const std::string text = value.to_string();
  const auto slash = text.find('/');
  const bool negative = text.starts_with('-');
  if (slash == std::string::npos) return negative ? "(- " + text.substr(1) + ")" : text;
  const std::size_t start = negative ? 1U : 0U;
  const std::string quotient =
      "(/ " + text.substr(start, slash - start) + " " + text.substr(slash + 1) + ")";
  return negative ? "(- " + quotient + ")" : quotient;
}

std::string value_text(const core::Model& model, const core::ModelValue& value) {
  if (const auto* boolean = std::get_if<bool>(&value.payload)) return *boolean ? "true" : "false";
  if (const auto* rational = std::get_if<util::Rational>(&value.payload)) return numeral(*rational);
  return "(as @uc_" + std::to_string(value.sort) + "_" +
         std::to_string(std::get<core::DomainValue>(value.payload).index) + " " +
         identifier(model.sorts().at(value.sort).name) + ")";
}

}  // namespace

std::string print_model(const core::Model& model) {
  // Original read-only serialization contract: docs/model-interpretation.md.
  std::ostringstream out;
  out << "(model\n";
  std::unordered_set<std::string> names;
  for (const auto& symbol : model.symbols()) names.insert(identifier(symbol.name));
  std::vector<const core::ModelInterpretation*> interpretations;
  for (const auto& interpretation : model.interpretations())
    interpretations.push_back(&interpretation);
  std::ranges::sort(interpretations, {},
                    [](const auto* interpretation) { return interpretation->symbol; });
  for (const auto* interpretation : interpretations) {
    const auto& symbol = model.symbols().at(interpretation->symbol);
    if (symbol.name.starts_with("$primo_")) continue;
    std::vector<std::string> parameters;
    out << "  (define-fun " << identifier(symbol.name) << " (";
    for (std::size_t index = 0; index < symbol.arg_sorts.size(); ++index) {
      std::string parameter = "x" + std::to_string(index);
      if (names.contains(parameter)) {
        parameter = "@primo_model_arg_" + std::to_string(index);
        while (names.contains(parameter)) parameter += "_";
      }
      parameters.push_back(parameter);
      if (index != 0) out << " ";
      out << "(" << parameter << " " << identifier(model.sorts().at(symbol.arg_sorts[index]).name)
          << ")";
    }
    out << ") " << identifier(model.sorts().at(symbol.result_sort).name) << " ";
    struct Row {
      std::vector<std::string> args;
      std::string result;
    };
    std::vector<Row> rows;
    for (const auto& row : interpretation->rows) {
      if (row.result == interpretation->default_value) continue;
      Row printed;
      for (const auto& argument : row.arguments)
        printed.args.push_back(value_text(model, argument));
      printed.result = value_text(model, row.result);
      rows.push_back(std::move(printed));
    }
    std::ranges::sort(rows, {}, &Row::args);
    for (const auto& row : rows) {
      out << "(ite ";
      if (row.args.empty()) out << "true";
      if (row.args.size() > 1) out << "(and";
      for (std::size_t index = 0; index < row.args.size(); ++index) {
        if (row.args.size() > 1) out << " ";
        out << "(= " << parameters[index] << " " << row.args[index] << ")";
      }
      if (row.args.size() > 1) out << ")";
      out << " " << row.result << " ";
    }
    out << value_text(model, interpretation->default_value);
    for (std::size_t index = 0; index < rows.size(); ++index) out << ")";
    out << ")\n";
  }
  out << ")";
  return out.str();
}

}  // namespace primo::smtlib
