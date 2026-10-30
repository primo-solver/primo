#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::smt {

// Diagnostic design: docs/papers/lra-call-ablation.md. Owns its output stream;
// shared by sequential checks in one process. Never changes a tableau.
class LraCallProfiler {
 public:
  using Tableau = theory::arithmetic::LraTableau;
  struct Checker {
    std::size_t id;
    std::size_t variables;
    std::size_t calls = 0;
    std::string definitions;
  };

  LraCallProfiler(const std::string& path, std::string export_directory = "",
                  std::size_t export_min_pivots = 100, std::size_t export_limit = 20,
                  std::string capture_directory = "", std::size_t capture_call = 1);
  ~LraCallProfiler();
  LraCallProfiler(const LraCallProfiler&) = delete;
  LraCallProfiler& operator=(const LraCallProfiler&) = delete;

  Checker register_checker(const Tableau& base);
  void capture_before_check(const Checker& checker, const Tableau& tableau);
  void record(Checker& checker, const Tableau& tableau, std::size_t pivots,
              std::uint64_t nanoseconds, std::size_t row_updates, std::size_t term_ops,
              bool conflict);

 private:
  std::string export_cube(const Checker& checker, const Tableau& tableau, bool conflict);
  std::string write_cube(const Checker& checker, const Tableau& tableau,
                         const std::string& directory, const std::string& name,
                         const std::string& status, std::size_t call) const;

  std::ofstream out_;
  std::string export_directory_;
  std::size_t export_min_pivots_;
  std::size_t export_limit_;
  std::size_t exported_ = 0;
  std::string capture_directory_;
  std::size_t capture_call_;
  bool capture_attempted_ = false;
  std::size_t checkers_ = 0;
  std::size_t calls_ = 0;
};

}  // namespace primo::smt
