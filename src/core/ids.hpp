#pragma once

#include <cstdint>
#include <limits>

namespace primo::core {

using SortId = std::uint32_t;
using SymbolId = std::uint32_t;
using TermId = std::uint32_t;

inline constexpr SortId kInvalidSortId = std::numeric_limits<SortId>::max();
inline constexpr SymbolId kInvalidSymbolId = std::numeric_limits<SymbolId>::max();
inline constexpr TermId kInvalidTermId = std::numeric_limits<TermId>::max();

}  // namespace primo::core
