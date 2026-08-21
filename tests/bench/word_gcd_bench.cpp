// Guard rail for src/util/rational.cpp's gcd_u64.
//
// gcd_u64 carries three guards (operand swap, b == 1 early exit, conditional
// Euclidean step) that were tuned against one measured operand distribution:
// QF_LIA, where 98.3% of numerators are 4 bits or fewer and the denominator is
// usually exactly 1. **That tuning is distribution-dependent.** A future
// workload with a different shape could see less benefit, or a small loss.
//
// This benchmark exists so that question can be re-answered in minutes instead
// of re-derived. It does three things:
//
//   1. verifies both implementations against std::gcd, including the
//      adversarial inputs, so a "faster" variant cannot quietly be wrong;
//   2. times the shipped implementation against the original textbook loop
//      across distributions chosen to make each guard useless in turn;
//   3. measures what runtime dispatch through a function pointer would cost,
//      which is the obvious way to make the algorithm configurable and is the
//      reason we do not.
//
// Not part of ctest: it is a timing harness, and timings are not assertions.
// Build and run it directly:
//
//     g++ -O2 -std=c++20 -o /tmp/word_gcd_bench tests/bench/word_gcd_bench.cpp
//     /tmp/word_gcd_bench
//
// See docs/papers/word-gcd-notes.md for the literature and the standing
// warning, and docs/qf-lia-profile.md for the profile that started it.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

namespace {

// The original textbook implementation, kept as the comparison point.
std::uint64_t gcd_textbook(std::uint64_t a, std::uint64_t b) {
  if (a == 0) return b;
  if (b == 0) return a;
  const int shift = __builtin_ctzll(a | b);
  a >>= __builtin_ctzll(a);
  do {
    b >>= __builtin_ctzll(b);
    if (a > b) std::swap(a, b);
    b -= a;
  } while (b != 0);
  return a << shift;
}

// Mirror of what src/util/rational.cpp ships. Keep the two in step: if
// gcd_u64 changes and this does not, the comparison below is measuring
// history rather than the current code.
std::uint64_t gcd_odd(std::uint64_t u, std::uint64_t v) {
  u >>= 1;
  v >>= 1;
  while (u != v) {
    const std::uint64_t t = u - v;
    const std::uint64_t v_greater = ~(t >> 63) + 1;
    v += v_greater & t;
    u = (t ^ v_greater) - v_greater;
    const int trailing = __builtin_ctzll(t);
    u = (u >> 1) >> trailing;
  }
  return (u << 1) + 1;
}

std::uint64_t gcd_shipped(std::uint64_t a, std::uint64_t b) {
  if (a == 0) return b;
  if (b == 0) return a;
  if (a < b) std::swap(a, b);
  if (b == 1) return 1;
  const int a_trailing = __builtin_ctzll(a);
  const int b_trailing = __builtin_ctzll(b);
  const int shift = a_trailing < b_trailing ? a_trailing : b_trailing;
  a >>= a_trailing;
  b >>= b_trailing;
  if (b == 1) return 1ULL << shift;
  if ((a >> 16) > b) {
    a %= b;
    if (a == 0) return b << shift;
    a >>= __builtin_ctzll(a);
  }
  return gcd_odd(a, b) << shift;
}

using Pairs = std::vector<std::pair<std::uint64_t, std::uint64_t>>;
using GcdFn = std::uint64_t (*)(std::uint64_t, std::uint64_t);

// volatile so the compiler cannot devirtualize the indirect call away, which
// is the whole point of the measurement.
volatile GcdFn g_dispatch = &gcd_shipped;

template <typename F>
double bench(F fn, const Pairs& pairs) {
  const auto start = std::chrono::steady_clock::now();
  std::uint64_t sink = 0;
  for (const auto& [a, b] : pairs) sink += fn(a, b);
  const auto stop = std::chrono::steady_clock::now();
  if (sink == 424242) std::printf(" ");  // keep the loop
  return std::chrono::duration<double, std::nano>(stop - start).count() /
         static_cast<double>(pairs.size());
}

int verify() {
  long long checked = 0;
  long long bad = 0;
  for (std::uint64_t a = 0; a <= 400; ++a) {
    for (std::uint64_t b = 0; b <= 400; ++b) {
      ++checked;
      const auto want = std::gcd(a, b);
      if (gcd_shipped(a, b) != want || gcd_textbook(a, b) != want) ++bad;
    }
  }
  std::mt19937_64 rng(20260821);
  for (int i = 0; i < 5000000; ++i) {
    const int wa = 1 + static_cast<int>(rng() % 64);
    const int wb = 1 + static_cast<int>(rng() % 64);
    std::uint64_t a = rng() >> (64 - wa);
    std::uint64_t b = rng() >> (64 - wb);
    switch (rng() % 6) {
      case 0:
        b = 1;
        break;
      case 1:
        b = 1ULL << (rng() % 64);
        break;
      case 2:
        a = b;
        break;
      default:
        break;
    }
    ++checked;
    const auto want = std::gcd(a, b);
    if (gcd_shipped(a, b) != want || gcd_textbook(a, b) != want) ++bad;
  }
  std::printf("correctness: %lld pairs checked, %lld mismatches\n\n", checked, bad);
  return bad == 0 ? 0 : 1;
}

}  // namespace

int main() {
  const int rc = verify();

  std::mt19937_64 rng(2024);
  const int kCount = 3000000;
  std::vector<std::pair<const char*, Pairs>> cases;
  const auto add = [&](const char* name, auto gen) {
    Pairs pairs(kCount);
    for (auto& p : pairs) p = gen();
    cases.emplace_back(name, std::move(pairs));
  };

  // The distribution the guards were tuned for.
  add("TUNED  b==1, 4-bit a", [&] { return std::make_pair((rng() & 0xF) | 1, 1ULL); });
  // Adversarial: b is never 1 and magnitudes are comparable, so every guard is
  // dead weight and only the loop body differs.
  add("ADVERSE tiny 4-bit, b>=3",
      [&] { return std::make_pair((rng() & 0xF) | 3, (rng() & 0xF) | 3); });
  add("ADVERSE 8-bit, b>=3",
      [&] { return std::make_pair((rng() & 0xFF) | 3, (rng() & 0xFF) | 3); });
  add("ADVERSE 16-bit, b>=3",
      [&] { return std::make_pair((rng() & 0xFFFF) | 3, (rng() & 0xFFFF) | 3); });
  add("ADVERSE 32-bit, b>=3",
      [&] { return std::make_pair((rng() & 0xFFFFFFFF) | 3, (rng() & 0xFFFFFFFF) | 3); });
  add("ADVERSE shared factor 2^8",
      [&] { return std::make_pair(((rng() & 0xFFFF) | 1) << 8, ((rng() & 0xFFFF) | 1) << 8); });
  add("ADVERSE just under 2^16 ratio", [&] {
    const std::uint64_t b = (rng() & 0xFF) | 3;
    return std::make_pair((b * ((rng() % 65535) + 1)) | 1, b);
  });

  std::printf("%-32s %10s %10s %9s\n", "distribution", "textbook", "shipped", "ratio");
  for (const auto& [name, pairs] : cases) {
    const double a = bench(gcd_textbook, pairs);
    const double b = bench(gcd_shipped, pairs);
    std::printf("%-32s %9.2f %9.2f %8.2fx%s\n", name, a, b, a / b,
                a / b < 0.98 ? "   <-- SLOWER than textbook" : "");
  }

  // What it would cost to make the algorithm switchable at runtime.
  std::printf("\ndispatch overhead (why gcd_u64 is not configurable):\n");
  for (const auto& [name, pairs] : cases) {
    if (std::string_view(name).substr(0, 5) != "TUNED" &&
        std::string_view(name) != "ADVERSE 16-bit, b>=3") {
      continue;
    }
    const double direct = bench(gcd_shipped, pairs);
    const double indirect =
        bench([](std::uint64_t a, std::uint64_t b) { return g_dispatch(a, b); }, pairs);
    std::printf("  %-30s direct %6.2f ns   fn-ptr %6.2f ns   %+.2f ns (%+.0f%%)\n", name, direct,
                indirect, indirect - direct, 100.0 * (indirect - direct) / direct);
  }
  return rc;
}
