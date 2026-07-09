#include "util/statistics.hpp"

#include <ostream>
#include <string>
#include <string_view>

namespace primo::util {

void Statistics::remember_key(std::string_view name) {
  const std::string key{name};
  if (!values_.contains(key)) {
    order_.push_back(key);
  }
}

void Statistics::set(std::string_view name, Value value) {
  remember_key(name);
  values_[std::string{name}] = value;
}

void Statistics::increment(std::string_view name, Value delta) {
  remember_key(name);
  values_[std::string{name}] += delta;
}

Statistics::Value Statistics::get(std::string_view name) const {
  const auto found = values_.find(std::string{name});
  if (found == values_.end()) {
    return 0;
  }
  return found->second;
}

bool Statistics::contains(std::string_view name) const {
  return values_.contains(std::string{name});
}

void print_statistics(std::ostream& output, const Statistics& statistics) {
  for (const std::string& key : statistics.keys()) {
    output << key << ' ' << statistics.get(key) << '\n';
  }
}

}  // namespace primo::util
