// Layout probe only: these candidate records are never constructed or used
// as terms. A working union requires explicit active-member lifetime handling.
#include <iostream>
#include <variant>
#include <vector>

#include "core/term.hpp"

using primo::core::SortId;
using primo::core::SymbolId;
using primo::core::Term;
using primo::core::TermId;
using primo::core::TermKind;
using primo::util::Rational;

union PayloadUnion {
  bool boolean;
  SymbolId symbol;
  Rational rational;
};

using PayloadVariant = std::variant<std::monostate, bool, SymbolId, Rational>;

template <class Payload>
struct CandidateLayout {
  TermId id;
  TermKind kind;
  SortId sort;
  std::vector<TermId> children;
  Payload payload;
};

int main() {
  std::cout << "Term: " << sizeof(Term) << '\n'
            << "Variant layout: " << sizeof(CandidateLayout<PayloadVariant>) << '\n'
            << "Union layout: " << sizeof(CandidateLayout<PayloadUnion>) << '\n';
}
