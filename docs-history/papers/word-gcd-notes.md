# Word-sized GCD: what the literature says

Written 2026-08-21, after `gcd_u64` turned up as a hot spot in
[](../qf-lia-profile.md). The first pass at fixing it went straight to GMP's
source without checking the literature, which was the wrong order: the central
pathology is documented, and had been for over a decade.

## The pathology we rediscovered

Daniel Lemire's 2013 benchmark states it plainly:

Lemire highlights a case where Euclid's remainder step beats binary GCD:
when an operand is one, Euclid finishes immediately while repeated binary
reductions may continue. He suggests hybrid handling for such trivial operands.

That is precisely what this solver hits. A binary GCD makes progress by
subtract-and-shift and never divides, so `gcd(a, 1)` costs O(log a) iterations
instead of one step — and `Rational::operator*` calls
`gcd(|numerator|, denominator)` on a scale-canonicalized tableau where the
denominator is very often exactly 1. Measured here at 15.9 ns (16-bit operand)
and 40.6 ns (63-bit) to compute the constant 1.

Lemire's post also confirms the other half: `__builtin_ctz` is what makes the
binary form competitive at all (17 M ops/s without it, 39 M with), and small
operands favour binary otherwise.

## The standard practical form

Wikipedia's summary matches what GMP implements and is a fair statement of the
settled practical art for machine words:

- **branch-free except the loop exit**, to avoid the cost of
  unpredictable branches;
- **count-trailing-zeros plus one shift**, rather than repeated division by two;
- **iterative, not recursive**, maintaining the invariant that both operands are
  odd on entry to the loop;
- **factor out the common power of two once at the start**, then only the
  odd-odd identities are needed inside.

GMP 6.3.0 (`mpn/generic/gcd_1.c`, `gcd_11.c`, both in `contrib/src`) adds two
things on top, and both are now in `gcd_u64`:

- the inner loop derives min/abs from a mask built off the sign of `u - v`,
  eliminating the `if (a > b) swap` that would otherwise mispredict about half
  the time on unrelated operands;
- the Euclidean step is **conditional**, taken only when the operands differ by
  more than 2^16, so a highly unequal pair is reduced by division before
  binary steps continue.

Reported figures in the wild are consistent with what was measured here: a basic
binary GCD around 7.7 M ops/s against 7.2 M for `std::gcd`, with hybrid
optimizations reaching 12 M. Our own change measured 1.8x on random 16-bit pairs
and 2.1x on random 63-bit pairs.

## What the actual record-holder is, and why it does not apply

Bernstein and Yang's *safegcd* (2019) and Pornin's follow-up work hold the
speed record for GCD-adjacent computation on modern 64-bit x86. Both are aimed
at **constant-time modular inversion** for cryptography, which is a different
objective from ours:

- Their advantage comes from batching *divsteps*: safegcd runs 62 iterations at
  a time against the bottom bits only and touches the full-width operands just
  12 times, so although it performs *more* total iterations (744 vs 512 for a
  256-bit inversion), each is far cheaper.
- That trade pays off for wide, multi-limb operands. For a **single 64-bit word**
  it is the wrong shape: a fixed iteration schedule cannot exit early, and this
  solver's operands are overwhelmingly tiny.
- Constant time is a feature we do not want here. It exists to defeat timing
  side channels, and it forbids exactly the input-dependent early exits that
  give us the largest win.

So the record-holder solves a problem we do not have. There is no faster
general-purpose word GCD to adopt; `std::gcd` was benchmarked too and is not
faster than the plain textbook loop.

## What the literature could not tell us

The operand *distribution*. Instrumenting the solver showed 98.3% of numerators
in the both-integer multiply case are 4 bits or fewer, with the denominator
equal to 1 — see [](../qf-lia-profile.md). At that size no asymptotic property
matters; the early exit is the whole game, and the algorithm choice underneath
it is nearly irrelevant. That is a measurement, not something a paper about
GCD could have supplied.

The general lesson for this project, since it has now cost time twice: check
whether a "basic problem" has a written answer *before* optimizing from a
profile. Here the profile told us where the time went, but the literature would
have told us why, and faster.


## Standing warning: this tuning is distribution-dependent

Everything above was tuned against **one measured operand distribution** --
QF_LIA's, where 98.3% of numerators are 4 bits or fewer and the denominator is
usually exactly 1. That is a real risk worth stating plainly rather than
discovering later: a workload with a different shape gets less benefit, and one
particular shape is measurably *worse*.

`tests/bench/word_gcd_bench.cpp` exists so this can be re-answered in minutes.
It verifies against `std::gcd`, times the shipped form against the original
textbook loop across distributions chosen to make each guard useless in turn,
and flags any case where shipped is slower. Build and run it directly:

```sh
g++ -O2 -std=c++20 -o /tmp/word_gcd_bench tests/bench/word_gcd_bench.cpp && /tmp/word_gcd_bench
```

Measured, ns/call:

| distribution | textbook | shipped | ratio |
| --- | --- | --- | --- |
| **tuned** (b==1, 4-bit a) | 6.58 | 0.83 | **7.89x** |
| **adverse** tiny 4-bit, b>=3 | 9.25 | 10.09 | **0.92x (slower)** |
| adverse 8-bit, b>=3 | 18.08 | 14.32 | 1.26x |
| adverse 16-bit, b>=3 | 39.55 | 23.40 | 1.69x |
| adverse 32-bit, b>=3 | 77.62 | 40.35 | 1.92x |
| adverse shared factor 2^8 | 41.64 | 23.93 | 1.74x |
| adverse just under 2^16 ratio | 22.99 | 22.72 | 1.01x |

**The worst case is real but bounded and narrow: -8%**, on tiny operands where
neither is 1 and the magnitudes are comparable, so all three guards are dead
weight while the loop is too short for the branchless form to repay them.
Everywhere else the change is neutral to 1.9x. There is no distribution in
which it falls off a cliff, because each guard is a single well-predicted
compare when it does not fire.

### Why it is not runtime-configurable

The obvious hedge is to make the algorithm selectable behind a function
pointer. **Measured, that costs far more than the risk it insures against.**
`gcd_u64` is a static function in an anonymous namespace and inlines into
`Rational::operator*`; dispatching through a pointer prevents that and adds an
indirect call:

| distribution | direct | via fn-ptr | overhead |
| --- | --- | --- | --- |
| tuned (b==1, 4-bit) | 0.84 ns | 2.32 ns | **+175%** |
| adverse 16-bit pair | 23.29 ns | 24.51 ns | +5% |

Paying **+175% on the hot path** to hedge an 8% loss on a narrow distribution
is a bad trade, and it is worst precisely where we are fastest: the whole value
of the early exits is that the call collapses to a couple of cycles, which an
indirect call by itself exceeds.

If this ever does need to vary, the mechanism should be **compile-time**
(a `constexpr` or template parameter, chosen per build) or a cheaper guard --
not runtime dispatch. Runtime dispatch is the one option the measurement rules
out.

### The second workload we already have: QF_LRA

`Rational` is not QF_LIA-only, so the concern is concrete rather than
hypothetical -- QF_LRA has genuinely fractional coefficients and therefore a
different gcd distribution. Measured on eight QF_LRA instances spanning 0.01s
to 58s, minimum of three runs each, original gcd vs shipped:

```
TOTAL                          92.36s     92.13s    1.002x
```

Individual ratios ran 0.969x to 1.198x with **every answer identical**. So
QF_LRA is neither helped nor harmed: the win is QF_LIA-specific, and the
guards cost QF_LRA nothing. That is the outcome the "cheap when they do not
fire" design predicts, and it is now measured rather than assumed.

### When to revisit

Re-run the benchmark, and re-measure the operand distribution, if any of these
change:

- a new logic or fragment starts leaning on `Rational` heavily;
- atom canonicalization changes what coefficients reach the tableau (it is what
  manufactures the fractional case today -- see [](../qf-lia-profile.md));
- a profile again shows `gcd_u64` or `Rational::operator*` high in the list.

The instrumentation used to obtain the QF_LIA distribution was a temporary
histogram of operand bit-widths inside `operator*`; it is described in
[](../qf-lia-profile.md) and takes about ten minutes to reapply.

## Sources

- [Fastest way to compute the greatest common divisor — Daniel Lemire (2013)](https://lemire.me/blog/2013/12/26/fastest-way-to-compute-the-greatest-common-divisor/)
- [Binary GCD algorithm — Wikipedia](https://en.wikipedia.org/wiki/Binary_GCD_algorithm)
- [Fast constant-time gcd computation and modular inversion — Bernstein & Yang (2019)](https://gcd.cr.yp.to/safegcd-20190413.pdf)
- [Optimized Binary GCD for Modular Inversion — Pornin (2020)](https://eprint.iacr.org/2020/972.pdf)
- GMP 6.3.0, `mpn/generic/gcd_1.c` and `mpn/generic/gcd_11.c` (vendored in `contrib/src`)
