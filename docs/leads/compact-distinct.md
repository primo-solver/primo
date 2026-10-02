# Compact handling of large distinct assertions

**Status: open; design required. Owner: unassigned.**

The [frontend](../implementation/frontend.md) expands `distinct` into pairwise
disequalities. With $n$ arguments that requires $n(n-1)/2$ pairs. The
{source}`historical lead 23 <docs/open-leads.md#L2779-L2802>` records one native
session with arities 1,051 and 73, producing 554,403 pairwise constraints.
That is evidence of a representation cost at the measured revision, not a
current speedup prediction or evidence of another solver's internal encoding.

Before implementation, select and read a primary algorithm and write an
original design covering arbitrary Boolean polarity, scope removal, congruence
explanations, and models. If the first design handles only positive top-level
assertions, state that admission condition and retain general encoding elsewhere.

Measure avoided pairs, terms, atoms, clauses, theory work, time and memory on
the motivating sessions. Independently validate printed models. Reject the
proposal if reduced encoding cost merely moves comparable work into theory
checks, or if unexplained lost solves remain. It must preserve the native
session and Boolean-argument contracts.
