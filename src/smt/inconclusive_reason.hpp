#pragma once

namespace primo::smt {

// Typed operational reasons shared by formula tactics, solver engines, and
// session portfolios. Diagnostic strings are presentation only.
enum class InconclusiveReason {
  None,
  Inapplicable,
  ResourceLimit,
  Unsupported,
  Cancelled,
  Unknown,
};

}  // namespace primo::smt
