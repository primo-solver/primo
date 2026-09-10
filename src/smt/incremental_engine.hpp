#pragma once

#include <memory>
#include <vector>

#include "core/ids.hpp"
#include "smt/check_sat.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::smt {

// SMT-owned scope engine. Terms are borrowed and must outlive this object.
// Appends receive already lowered assertions, including their side constraints.
class IncrementalEngine {
 public:
  virtual ~IncrementalEngine() = default;
  virtual void push() = 0;
  virtual void pop() = 0;
  virtual void append(const std::vector<core::TermId>& assertions) = 0;
  virtual CheckSatOutcome check(const std::vector<core::TermId>& model_terms,
                                const CheckSatOptions& options) = 0;
};

std::unique_ptr<IncrementalEngine> make_incremental_lra_engine(core::TermManager& terms,
                                                               const CheckSatOptions& options);

}  // namespace primo::smt
