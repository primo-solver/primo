# Compact term storage and future payloads

**Status: open lead; implementation deferred. Owner: unassigned.**

Keep the current representation while investigating how to reduce per-term
storage. No union, variant, or separate payload store has been selected.
The design should account for additional kinds of leaf data as Primo grows,
rather than optimize only the payloads present today.

## Current representation and initial observation

{source}`Term <src/core/term.hpp#L77-L85>` stores a symbol/index, Boolean value,
and rational value simultaneously. Only the payload appropriate to `TermKind`
is meaningful, but every node reserves space for all three fields.
{source}`TermManager::Key <src/core/term_manager.hpp#L88-L97>` repeats this
layout, and {source}`intern <src/core/term_manager.cpp#L540-L560>` retains
copies of child lists and numeric payloads in the term and its hash key.

A local C++ layout probe on 2026-09-24 produced these inline sizes:

| Record | `sizeof`, bytes |
| --- | ---: |
| Current `Term` | 72 |
| Illustrative record with `std::variant` payload | 72 |
| Illustrative record with a union payload and existing `TermKind` tag | 64 |

The variant candidate holds `monostate`, `bool`, `SymbolId`, or `Rational`.
The union candidate holds `bool`, `SymbolId`, or `Rational`. Both retain the
ID, kind, sort, and separate child vector. Children cannot simply be an
alternative to the scalar payload: an application needs a symbol and children,
and `Times` needs a rational coefficient and a child.

These are layout observations on the local Linux x86-64 GCC 13.3.0/libstdc++ build,
not portable size guarantees or measured solver memory/runtime improvements.
The straightforward variant adds its own discriminator alongside `TermKind`
and provides no inline size reduction in this probe. This does not establish
that variant access is slow. The union candidate saves eight inline bytes but
still reserves room for the largest alternative in every node. Neither
candidate is a working replacement for `Term`.

The {download}`layout probe <../examples/term-layout.cpp>` preserves the exact
candidate records. After installing the existing GMP dependency, reproduce it
from the repository root with:

```bash
c++ --version
c++ -std=c++20 -Isrc -isystem contrib/local/include \
  docs-next/examples/term-layout.cpp -o /tmp/primo-term-layout
/tmp/primo-term-layout
```

The probe performs no solving or dynamic allocation measurement. Total storage
also includes child arrays, big-number allocations, hash-table entries,
capacity, and auxiliary data; see the [core chapter](../implementation/core.md).

## Design questions

The [Yices survey](../papers/yices.md#other-ideas-retained-from-the-talk)
adds the SMT 2016 talk's integer-indexed term tables and separate descriptors
as a concrete representation reference. It is a candidate to compare with the
current layout, not an adopted design or evidence of a particular reclamation
policy.

- Compare a union whose active member is determined by `TermKind` with a
  compact node containing IDs into separate payload stores. Include the hash
  key in the design; shrinking only the visible node leaves duplicated data.
- Account for future leaf payloads with different size, alignment, and ownership
  requirements. For example, adding bit-vector literals or strings would need
  its own representation design; neither feature is implemented now. A larger
  inline alternative can enlarge every term, even in inputs that never use it.
- Specify construction, destruction, copy/move behavior, and exception safety
  for nontrivial payloads such as `Rational`. Keep those operations in the core
  representation instead of duplicating lifetime logic across solver callers.
- Preserve kind/payload consistency, sort checking, structural equality, and
  hash consistency. Separate stores also need an owner, stable handles, and
  explicit rules for borrowing, growth, reclamation, and handle reuse.
- Measure the tradeoff between smaller nodes and extra indirection, allocation,
  and cache traffic. Include Boolean/UF-heavy inputs, arithmetic inputs with
  both small and large exact values, and long incremental sessions.

Compact storage and [incremental reclamation](incremental-memory.md) address
different factors: bytes per retained term and the number of retained terms.
Coordinate their ownership rules with the [library API lead](library-api.md).

The [compact rational storage lead](rational-storage.md) considers reducing
`Rational` itself from 24 to 16 inline bytes while retaining owned big values.
This would affect both term payloads and arithmetic data structures; evaluate
it separately from the outer term-payload union described here.

## Decision and completion

Start by attributing memory to nodes, keys, edges, and payload allocations.
Compare candidate designs against the current representation on the same
inputs, with construction cost, solve time, and peak/retained memory recorded.
Retain the current design if savings do not justify the runtime or maintenance
cost. A smaller `sizeof` alone does not establish that a candidate is better.

Any implementation needs a local representation/invariant design note, updated
core documentation, hash-consing and lifetime regressions, sanitizer checks,
and the applicable [correctness gates](../development/correctness.md). Record
solver evaluations in the run ledger before claiming an improvement. Those
experiments and any implementation remain deferred.
