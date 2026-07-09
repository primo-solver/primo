#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace primo::util {

class Statistics {
 public:
  using Value = std::uint64_t;

  void set(std::string_view name, Value value);
  void increment(std::string_view name, Value delta = 1);

  Value get(std::string_view name) const;
  bool contains(std::string_view name) const;
  bool empty() const { return order_.empty(); }

  const std::vector<std::string>& keys() const { return order_; }

 private:
  void remember_key(std::string_view name);

  std::vector<std::string> order_;
  std::unordered_map<std::string, Value> values_;
};

void print_statistics(std::ostream& output, const Statistics& statistics);

}  // namespace primo::util
