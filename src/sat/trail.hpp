#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace primo::sat {

// Original ownership design: docs/dpllt.md, shared assignment protocol.
// IPASIR-UP Section 2.2: level boundaries retain the entire earlier prefix.
class Trail {
 public:
  static constexpr std::size_t unassigned = std::numeric_limits<std::size_t>::max();
  Trail() = default;
  Trail(const Trail&) = delete;
  Trail& operator=(const Trail&) = delete;
  Trail(Trail&&) = delete;
  Trail& operator=(Trail&&) = delete;

  bool assign(int literal);
  void push_level();
  void backtrack(std::size_t level, std::vector<int>& removed);
  void clear();
  void load_model(std::span<const int> model);
  [[nodiscard]] std::size_t position(int variable) const;
  [[nodiscard]] int literal(int variable) const {
    const auto index = position(variable);
    return index == unassigned ? 0 : literals_[index];
  }
  [[nodiscard]] int at(std::size_t index) const { return literals_.at(index); }
  [[nodiscard]] std::size_t size() const { return literals_.size(); }
  [[nodiscard]] std::size_t level() const { return boundaries_.size(); }
  [[nodiscard]] std::uint64_t epoch() const { return epoch_; }

 private:
  std::vector<int> literals_;
  std::vector<std::size_t> positions_;
  std::vector<std::size_t> boundaries_;
  std::uint64_t epoch_ = 0;
};

}  // namespace primo::sat
