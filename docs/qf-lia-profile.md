# Where QF_LIA time actually goes (gprof, 2026-08-21)

A gprof profile of ten **medium-hard** QF_LIA instances, taken after stage 5
(patching) landed. Medium-hard means *solved in 5-40s* by the full-corpus run
(Slurm 128125): hard enough that the solver does real work, easy enough that
the profile shows a productive path rather than whatever a hopeless instance
spins on.

## Method, and one trap worth repeating

Build: `cmake -DCMAKE_BUILD_TYPE=None -DCMAKE_CXX_FLAGS="-pg -O2 -g -DNDEBUG"`.

**`-DNDEBUG` is not optional, and leaving it out silently corrupts the
result.** The first run of this profile used `-pg -O2 -g` -- matching the flags
recorded in the pre-existing `build-gprof*` directories -- and produced
`Rational::check_invariant` as the hottest function in the program at **29.8%
of self time**. That function is wrapped entirely in `#ifndef NDEBUG`, so it is
a no-op in Release and the finding was pure build artifact. It also inflated
total runtime by 56% (153.9s vs 98.5s across the same ten instances) and
distorted every other entry's share. `CMAKE_BUILD_TYPE=None` defines no
preprocessor symbols of its own, so `NDEBUG` has to be passed by hand.

Each instance runs in its own directory, because `gmon.out` is written to the
current directory and the next run would overwrite it.

Instances: one or more from `nec-smt`, `20220307-SMPT`, `20210219-Dartagnan`,
`cut_lemmas`, `dillig`, `CAV_2009_benchmarks`, `bofill-scheduling`,
`2019-cmodelsdiff`, `mathsat`. All ten solved under profiling.

## Result

98.5 self-seconds across ten runs.

| component | self | share |
|---|---|---|
| Rational / GMP arithmetic | 40.4s | **41.0%** |
| CaDiCaL (SAT) | 23.8s | 24.1% |
| `LraTableau` (simplex) | 15.0s | 15.2% |
| LRA propagator glue | 9.9s | 10.1% |
| other | 9.4s | 9.5% |
| **LIA stages** | **0.06s** | **0.1%** |

Hottest individual functions:

| share | self | function |
|---|---|---|
| 25.1% | 24.69s | `Rational::operator*` |
| 9.0% | 8.85s | `CaDiCaL::Internal::propagate` |
| 6.8% | 6.71s | `Rational::operator+` |
| 5.7% | 5.58s | `LraTableau::pivot` |
| 5.0% | 4.89s | `LraTableau::unlink_occurrence` |
| 4.7% | 4.60s | `OnlineLraPropagator::notify_assignment_lit` |
| 3.3% | 3.25s | `bound_entails` |

## Two things this settles

**1. Integer reasoning is not the bottleneck — not even close.** Every QF_LIA
stage combined (patching, GCD-consistency, Gomory cuts, branch-and-bound,
conflict generalization) accounts for **0.1%** of self time on medium-hard
instances. This is the same conclusion an earlier instrumented run reached from
a different direction, when it found 24 of 36 sampled timeouts never execute a
single integer round. Further work inside the integer layer cannot move these
instances; the time is in Boolean and LRA search.

**2. The cost is Simplex pivoting arithmetic.** Call-graph attribution over the
same ten runs:

- `Rational::operator*`: **844,353,807 calls, 91.8% of them from
  `LraTableau::pivot`** (8.0% from `LraTableau::update`).
- `Rational::operator+`: 821,727,887 calls, 58.4% from `pivot`.
- `Rational::operator<`: 797,003,808 calls, 55.8% from `bound_entails`,
  27.5% from `update_violation_status`.

So `pivot`'s row-update inner loop, plus the propagator's entailment scan, is
where QF_LIA actually spends its time.

## The concrete opportunity: over half the gcd work in multiply is provably wasted

`Rational::operator*`'s small-form path computes **two binary gcds on every
call**:

```cpp
const std::int64_t left  = gcd_u64(magnitude(num_),       other.den_);
const std::int64_t right = gcd_u64(magnitude(other.num_), den_);
```

When a denominator is 1, the corresponding gcd is `gcd(x, 1) == 1` by
definition and the whole call is wasted. Measured over the same ten instances
(844,353,807 small-form multiplies):

| case | calls | share |
|---|---|---|
| both denominators == 1 (both gcds skippable) | 467,703,359 | **55.4%** |
| one denominator == 1 (one gcd skippable) | 12,146,199 | 1.4% |
| neither (current path is necessary) | 364,504,249 | 43.2% |

**56.1% of all gcd calls in multiply are avoidable** (947.6M of 1.69B), behind a
test as cheap as `den_ == 1`.

The distribution is sharply bimodal per instance, which matters for predicting
the payoff: seven of the ten instances are **100%** both-integer (`nec-smt`,
`SMPT`, `bofill-scheduling`, `cmodelsdiff`, `mathsat`, `Dartagnan`), where the
fast path removes every gcd in multiply. The other three (`cut_lemmas`,
`dillig`, `CAV_2009_benchmarks`) are almost entirely fractional, where it does
nothing at all. So this is a large win on integer-coefficient families and a
no-op elsewhere — not a uniform speedup.

**Not yet implemented.** `Rational` is used by every logic, not just QF_LIA, so
the change needs its own verification pass (full suite, QF_LRA regression, and
differential fuzzing) rather than being folded into an unrelated stage.

## Reproducing

```sh
cmake -S . -B build-gprof-lia -DCMAKE_BUILD_TYPE=None \
      -DCMAKE_CXX_FLAGS="-pg -O2 -g -DNDEBUG"
make -C build-gprof-lia primo -j"$(nproc)"
mkdir -p /tmp/prof && cd /tmp/prof
/workspace/build-gprof-lia/primo --quiet <instance.smt2>
gprof /workspace/build-gprof-lia/primo gmon.out > flat.txt
gprof -q /workspace/build-gprof-lia/primo gmon.out > callgraph.txt
```
