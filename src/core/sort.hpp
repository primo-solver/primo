#pragma once

#include <string>

#include "core/ids.hpp"

namespace primo::core {

enum class SortKind {
  Bool,
  Uninterpreted,
};

struct Sort {
  SortId id = kInvalidSortId;
  std::string name;
  SortKind kind = SortKind::Uninterpreted;
};

}  // namespace primo::core
