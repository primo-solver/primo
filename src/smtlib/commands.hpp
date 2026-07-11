#pragma once

#include <string>
#include <vector>

#include "smtlib/ast.hpp"

namespace primo::smtlib {

enum class CommandKind {
  Ignored,
  SetLogic,
  DeclareSort,
  DeclareFun,
  DefineFun,
  Assert,
  CheckSat,
};

struct SortedVar {
  std::string name;
  std::string sort;
};

struct Command {
  CommandKind kind = CommandKind::CheckSat;
  std::string name;
  std::string logic;
  std::vector<std::string> arg_sorts;
  std::vector<SortedVar> parameters;
  std::string result_sort;
  SExpr assertion;
  SExpr body;
};

}  // namespace primo::smtlib
