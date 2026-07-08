#pragma once

namespace primo::util {

template <typename T>
struct Result {
  T value;
  bool ok = true;
};

}  // namespace primo::util
