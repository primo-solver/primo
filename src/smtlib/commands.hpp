#pragma once

#include <cstddef>
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
  GetModel,
  SetOption,
  Push,
  Pop,
  Reset,
  ResetAssertions,
  Exit,
  Unsupported,
};

struct SortedVar {
  std::string name;
  std::string sort;
};

struct Command {
  CommandKind kind = CommandKind::CheckSat;
  std::size_t count = 0;
  std::string name;
  std::string logic;
  std::string option;
  std::string value;
  std::vector<std::string> arg_sorts;
  std::vector<SortedVar> parameters;
  std::string result_sort;
  SExpr assertion;
  SExpr body;
};

}  // namespace primo::smtlib
