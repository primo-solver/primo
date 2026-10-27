#pragma once

#include <cstddef>
#include <optional>
#include <ostream>
#include <string_view>
#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::smt {

// Diagnostic event sink, not a proof store or a source of solver deductions.
// See docs/papers/lra-bound-structure-probe.md for the observation contract.
// The caller owns the stream and keeps it alive until this sink is destroyed.
class LraBoundProbe {
 public:
  using Var = theory::arithmetic::LraVarId;
  using Value = theory::arithmetic::DeltaRational;
  using Tableau = theory::arithmetic::LraTableau;

  LraBoundProbe(std::ostream& out, std::size_t limit,
                std::optional<std::size_t> snapshot_limit = std::nullopt)
      : out_(out), limit_(limit), snapshot_limit_(snapshot_limit) {
    out_ << "{\"event\":\"begin\",\"schema\":" << (snapshot_limit_ ? 2 : 1);
    if (snapshot_limit_) {
      out_ << ",\"causal_limit\":" << limit_ << ",\"snapshot_limit\":" << *snapshot_limit_;
    }
    out_ << "}\n";
  }
  ~LraBoundProbe() {
    out_ << "{\"event\":\"end\",\"events\":" << events_
         << ",\"limit_reached\":" << (full() ? "true" : "false");
    if (snapshot_limit_) {
      out_ << ",\"causal_events\":" << causal_events_ << ",\"snapshot_events\":" << snapshot_events_
           << ",\"snapshot_limit_reached\":" << (snapshots_full() ? "true" : "false");
    }
    out_ << "}\n";
    out_.flush();
  }
  LraBoundProbe(const LraBoundProbe&) = delete;
  LraBoundProbe& operator=(const LraBoundProbe&) = delete;

  bool full() const { return (snapshot_limit_ ? causal_events_ : events_) >= limit_; }
  bool snapshots_full() const {
    return snapshot_limit_ ? snapshot_events_ >= *snapshot_limit_ : full();
  }

  void variable(Var var, std::size_t term, std::string_view name) {
    if (!start("variable")) return;
    out_ << ",\"var\":" << var << ",\"term\":" << term << ",\"name\":\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const char raw : name) {
      const auto c = static_cast<unsigned char>(raw);
      if (c == '"' || c == '\\') out_ << '\\';
      if (c < 32) {
        out_ << "\\u00" << hex[c >> 4] << hex[c & 15];
      } else {
        out_ << static_cast<char>(c);
      }
    }
    out_ << '"';
    finish();
  }

  void atom(int lit, Var var, bool upper, const Value& value) {
    if (!start("atom")) return;
    out_ << ",\"lit\":" << lit << ",\"var\":" << var << ",\"upper\":" << (upper ? "true" : "false")
         << ",\"value\":";
    number(value);
    finish();
  }

  void assignment(int lit, Var var, const Tableau& tableau, bool conflict) {
    if (!start("assignment")) return;
    out_ << ",\"lit\":" << lit << ",\"var\":" << var
         << ",\"conflict\":" << (conflict ? "true" : "false") << ",\"lower\":";
    bound(tableau.lower_bound(var));
    out_ << ",\"upper\":";
    bound(tableau.upper_bound(var));
    finish();
  }

  void literals(std::string_view event, const std::vector<sat::Lit>& reasons, int lit = 0,
                std::string_view kind = "") {
    if (!start(event)) return;
    out_ << ",\"lit\":" << lit << ",\"kind\":\"" << kind << "\",\"reasons\":";
    reason_list(reasons);
    finish();
  }

  void backtrack(const std::vector<int>& removed) {
    if (!start("backtrack")) return;
    out_ << ",\"removed\":[";
    bool first = true;
    for (int lit : removed) {
      if (!first) out_ << ',';
      first = false;
      out_ << lit;
    }
    out_ << ']';
    finish();
  }

  // Snapshot the current row and selected bound values, so the offline reader
  // can check the interval calculation exactly. No hidden bound is installed.
  void derived(const Tableau& tableau, Var row, Var target, bool upper, const Value& value,
               const std::vector<sat::Lit>& reasons) {
    if (!start("derived")) return;
    out_ << ",\"row\":" << row << ",\"target\":" << target
         << ",\"upper\":" << (upper ? "true" : "false") << ",\"value\":";
    number(value);
    out_ << ",\"reasons\":";
    reason_list(reasons);
    out_ << ",\"installed\":";
    bound(upper ? tableau.upper_bound(target) : tableau.lower_bound(target));
    out_ << ",\"basic_bound\":";
    if (target == row) {
      out_ << "null";
    } else {
      const auto& terms = tableau.row(row);
      for (const auto& term : terms) {
        if (term.var != target) continue;
        const bool basic_upper = upper == (term.coeff.sign() > 0);
        bound(basic_upper ? tableau.upper_bound(row) : tableau.lower_bound(row));
        break;
      }
    }
    out_ << ",\"terms\":[";
    bool first = true;
    for (const auto& [var, coeff, var_slot] : tableau.row(row)) {
      if (!first) out_ << ',';
      first = false;
      out_ << "{\"var\":" << var << ",\"coeff\":\"" << coeff.to_string() << "\",\"lower\":";
      bound(tableau.lower_bound(var));
      out_ << ",\"upper\":";
      bound(tableau.upper_bound(var));
      out_ << '}';
    }
    out_ << ']';
    finish();
  }

 private:
  bool start(std::string_view event) {
    if (snapshot_limit_) {
      if (event == "derived") {
        if (snapshots_full()) return false;
        ++snapshot_events_;
      } else if (event != "variable" && event != "atom") {
        if (full()) return false;
        ++causal_events_;
      }
    } else if (full()) {
      return false;
    }
    out_ << "{\"event\":\"" << event << "\",\"seq\":" << ++events_;
    return true;
  }
  void finish() { out_ << "}\n"; }
  void number(const Value& value) {
    out_ << "[\"" << value.rational.to_string() << "\",\"" << value.infinitesimal.to_string()
         << "\"]";
  }
  void bound(const std::optional<Tableau::BoundInfo>& info) {
    if (!info) {
      out_ << "null";
      return;
    }
    out_ << "{\"value\":";
    number(info->value);
    out_ << ",\"reason\":" << info->reason.value << '}';
  }
  void reason_list(const std::vector<sat::Lit>& reasons) {
    out_ << '[';
    bool first = true;
    for (sat::Lit lit : reasons) {
      if (!first) out_ << ',';
      first = false;
      out_ << lit.value;
    }
    out_ << ']';
  }

  std::ostream& out_;
  std::size_t limit_;
  std::size_t events_ = 0;
  std::optional<std::size_t> snapshot_limit_;
  std::size_t causal_events_ = 0;
  std::size_t snapshot_events_ = 0;
};

}  // namespace primo::smt
