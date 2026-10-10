# QF_LRA Speedup Heuristics: Design Note

Historical design/evidence ledger: timings, rankings and source line references
below belong to their recorded revisions. For current routes and qualification,
use [](current-status.md); [](arithmetic.md) describes retained mechanisms.
SMT engine helpers formerly in `check_sat.cpp` now live in `solver_engines.cpp`.
The default-off pure-LRA fixed-elimination policy is distinct from the default-on
dedicated QF_UFLRA setting.

## Reverification of the paper profile (2026-10-09)

The LPAR manuscript's `Primo*` uses `--lra-model-phase --monotone-elimination`.
The original measured binary keeps both options off by default. The qualified
policy at `f0fbe67`, accepted on 2026-10-10, selects them automatically for
declared QF_LRA rebuild queries with auto preprocessing and adds independent
explicit opt-outs. Later commits update workflows and evidence, with the same
solver sources and tests.
The August measurements and manuscript tables are historical evidence; neither
qualifies the current implementation.

Fresh evaluation compares default, model phase alone, monotone elimination alone,
and both options, using the same binary and resource limits. The two populations
are the complete 1,753-instance non-incremental QF_LRA corpus at 30 seconds and
the official 519-instance SMT-COMP 2026 selection at 1,200 seconds. The latter
manifest is `benchmarks/qf-lra-smtcomp2026-selection.txt`; its companion JSON
records the source and checksums. It contains the original SMT-LIB inputs,
without competition scrambling. The paper's 194/325 training/test assignment
has not been recovered, so only the aggregate selected-corpus result is
reproducible from this manifest.

`scripts/slurm_lra_profile_comparison.sbatch` records timings and input/binary
identities through `benchmark_model_replay.py`. Submit its `benchmark` and
`replay` phases separately; each array task selects a profile and an input
shard. Optional profiles add OpenSMT and Yices reference timings. Resource
limits and exact reference versions must accompany the collected comparison;
cluster hardware differs from the paper's AMD EPYC machine.

For the automatic-policy candidate, the driver uses explicit opt-outs in its
original four ablation arms and calls the old default `legacy-default`.
Profile 6 measures the new automatic default with just `--quiet`. Archived
default-off binaries retain their supported original arguments; detection uses
the measured binary's CLI help. Replay reads the immutable parent provenance rather than replacing
its arguments with the candidate's current defaults.

Before broad timing runs, gate the exact candidate flags against reference
answers, including generated QF_LRA formulas, real-seed mutation, and existing
regression witnesses. Promotion requires a reproducible solved-count/PAR2 gain,
an explicit account of losses and family effects, zero untriaged correctness
failures, and separate direct Dolmen validation of every successful SAT row
for each Primo profile claimed. A QF_LRA gain alone does not justify enabling
these flags for QF_LIA or QF_UFLRA. The completed evaluation below qualifies
the scoped QF_LRA promotion.

The current opt-in profile passed the focused tests and all 1,000 generated
Boolean/arithmetic SAT models with direct Dolmen checks. A separate 3,000-case
FuzzSMT answer campaign produced 2,986 reference agreements and 14 timeouts.
Strict QF_LRA model replay accepted 1,137 of its 2,603 successful SAT rows;
Dolmen rejected the remaining 1,466 inputs' arithmetic syntax. An explicitly
separate replay accepted all 2,603 models with only the validator's logic
broadened to QF_NRA. Assertions and the candidate's QF_LRA logic were unchanged;
this checks model semantics without certifying strict input conformance.

The retained real-seed mutation campaign contains 8,380 inputs: 7,805 independent
reference agreements, 28 resource-only incomplete checks, and 547 invalid
generated inputs (190 nonlinear products and 357 undeclared-name cases).
All 27 candidate rejections with concrete Yices answers were independently
identified as nonlinear inputs by Z3, OpenSMT, and Dolmen. These are generated
input limitations, not accepted model passes. The operator mutation run reached
all 17 families; the two time-boxed TypeFuzz runs each reached seven families.
The dated `slurm_lra_paper_seeded_mutation.sbatch`,
`slurm_lra_paper_mutant_replay.sbatch`, `slurm_lra_paper_mutant_triage.sbatch`,
and `slurm_lra_paper_fuzz_semantic_replay.sbatch` scripts preserve the workflow.
Their evidence is recorded in `docs/run-log.json`; separate complete real-corpus
SAT validation remains required before promotion.

The completed 30-second comparison on a single Xeon Gold 6140 host gives:

| Profile | Solved / 1,753 | Timeouts | PAR2 sum (s) |
|---|---:|---:|---:|
| Previous default | 1,485 | 268 | 20,108.495 |
| Model phase | 1,534 | 219 | 16,931.037 |
| Monotone elimination | 1,508 | 245 | 18,864.799 |
| Both | 1,553 | 200 | 15,923.760 |
| OpenSMT 2.9.2 | 1,539 | 214 | 16,800.822 |
| Yices 2.7.0 | 1,539 | 214 | 15,408.137 |

Both gains 88 inputs and loses 20 against the previous default, for a net 68
and 20.81% lower PAR2. LassoRanker accounts for most solved-count gains;
latendresse loses four and uart loses two. These are aggregate gains with
real regressions. All 3,711 successful native SAT rows passed separate direct
Dolmen model replay, with unchanged input and binary identities. The
1,200-second comparison below is also complete and independently validated.
Timings use CPU cores on `gpu-1`, with no GPU allocation;
this host differs from the paper's EPYC 7513. Subsequent qualification uses
the compute partition, including the selected-corpus model replay,
to avoid occupying GPU-node CPU and memory resources. A new timing comparison
must compare its configurations on matching CPU hardware.

All six 1,200-second timing arms have completed on that same Gold host, with
no wrong answers, errors or pairwise reference disagreements. All 1,115
successful native SAT rows passed separate direct Dolmen replay, with complete
coverage of all 256 shards and unchanged parent input/binary identities.

| Profile | Solved / 519 | Timeouts | PAR2 sum (s) |
|---|---:|---:|---:|
| Previous default | 491 | 28 | 84,024.427 |
| Model phase | 501 | 18 | 59,825.189 |
| Monotone elimination | 498 | 21 | 67,121.127 |
| Both | 500 | 19 | 59,868.270 |
| OpenSMT 2.9.2 | 498 | 21 | 66,453.004 |
| Yices 2.7.0 | 491 | 28 | 83,999.463 |

Both gains 11 inputs and loses two, for nine net solves and 28.75% lower PAR2
against the previous default. Model phase alone solves one more input than both at this cutoff,
with 0.07% lower PAR2. The combined profile therefore does not dominate every
alternative. The manuscript's corresponding counts were 499 and 504;
absolute reproduction is limited by different hardware, concurrency and
OpenSMT revision. Yices matches the manuscript's `4f19700f` revision.
This run uses original, unscrambled inputs; the manuscript's exact input
archive is unavailable. Its 194/325 split is the intersection with a
473-instance tuning population, whose manifest has not been recovered.
The manuscript applies a 7 GB limit to all solvers in this experiment;
30 GB describes the actual competition. This run reserves 7 GiB per worker.
Selected-corpus replay was released after all native timing shards and the
candidate's compute timing stage completed. It ran on compute nodes while
the remaining reference timing shards finished on the Gold host. All new
work is restricted to compute nodes.
Unstarted selected-corpus replay tasks moved to
`slurm_lra_selected_model_replay.sbatch`: 6 GiB per worker with a 5 GiB Dolmen
cap, leaving space for runtime overhead. Already running replay tasks retained
their 7 GiB reservation and 6 GiB Dolmen cap. Parent timing limits, inputs,
binary and arguments were unchanged. The wrapper dispatches only native
profiles and delegates to the existing immutable-parent replay workflow;
all successful SAT rows still require direct validation. This scheduling
adjustment uses available compute memory without occupying GPU nodes.
For future submissions, the compact wrapper defaults to a 2,400-second solver
replay timeout and a 120-second Dolmen timeout: replay on the older CPUs needs
headroom beyond the 1,200-second Gold timing limit. The initial submissions
explicitly selected 1,260 seconds; their recorded limits are unchanged.
Two SAT replays reached that limit on older compute CPUs, one in the former
default and one with monotone elimination alone. Separate 3,600-second
replays of their five-SAT-row shards both passed all models. The original
timeouts and artifacts are retained; the final coverage audit selects those
complete extended attempts without counting rows twice. No benchmark timing changes.

```{figure} _static/qf-lra-reverification-20261009-selection1200.svg
:alt: Six solver configurations on the 519 selected QF_LRA inputs at 1200 seconds

Qualified selected-corpus comparison, source `23f945d`. All 1,115 successful
native SAT rows pass independent direct Dolmen replay. The summary is
`benchmarks/qf-lra-reverification-20261009-selection1200.json`.
```

An additional five-solver plot is being prepared for the same 519-instance
selection and 1,200-second limit. It includes only Primo's new automatic
default alongside OpenSMT, Yices, Z3 and cvc5. The existing selected-corpus
comparison lacks fresh Z3/cvc5 timings on matching hardware, so all five
configurations will be measured separately on Xeon E5-2690 v4 compute nodes.
The accepted source `f0fbe67` and preserved binary are reused; no solver
algorithm changes are involved. Timings and separate direct Dolmen validation
will be recorded in `benchmarks/qf-lra-default-references-20261010-selection1200.json`.
`slurm_lra_default_reference_comparison.sbatch` interleaves the five solver
arms by shard, requires the explicit selection manifest and its hash/count,
and rejects mismatched CPU generations. The initial jobs `184279`/`184328`
were cancelled after the archived generic wrapper omitted the manifest and
started on the full corpus. Those partial artifacts are retained and excluded;
the corrected driver calls the immutable-parent harness directly.
The five-solver cluster canary (`184343`) selected exactly one intended input
for each solver, and all five returned SAT successfully. The summary driver
`summarize_lra_reference_comparison.py` audits matching timing identities,
input hashes, resource limits and CPUs, checks every pair of decisive answers,
and requires complete direct Dolmen coverage for Primo before qualification.

```{figure} _static/qf-lra-reverification-20261009-full30.svg
:alt: Six solver configurations on the full QF_LRA corpus at a 30-second timeout

Qualified 30-second comparison, source `23f945d`. Model validation runs
separately and contributes no time to the curves. The committed summary is
`benchmarks/qf-lra-reverification-20261009-full30.json`.
```

The automatic-policy candidate passed 1,000 fresh generated SAT
answer checks and 1,000 direct Dolmen checks locally before its source commit,
as well as 42 C++ tests, 188 Python tests, and all 42 sanitizer tests.
The `slurm_lra_default_qualification.sbatch` workflow requires another fresh
3,000-case campaign against the committed candidate, independently checks
process exit statuses, and replays only successful SAT answers. Its generated
input model stage explicitly uses Dolmen's QF_NRA semantic superset. Another
1,000 Boolean/arithmetic cases use direct QF_LRA model checks. Real-seed
mutation uses `slurm_lra_default_seed_mutation.sbatch`, followed by independent
retained-input checks through `slurm_lra_default_mutant_replay.sbatch`.
These gates and the completed timing/model comparisons qualify the promotion
within their recorded coverage.
The committed candidate is `f0fbe67`: remote focused tests passed, and fresh
generated checks gave 2,976 agreements and 24 resource-only timeouts out of
3,000 inputs. All 2,563 successful SAT models passed direct Dolmen semantic
replay, and another 1,000 Boolean/arithmetic SAT models passed direct QF_LRA
checks. Real-seed mutation retained 6,785 inputs across all 17 families;
independent replay gave 6,749 agreements and 36 resource-only timeouts.
No wrong answers, crashes, parser/type errors, unsupported inputs, or reference
disagreements remained. The patched YinYang fork is clean at
`04c8618a134e7a6c8b6d54fcd5ee9fd2ec64a2bb`.
The retained-input qualification script requires complete shard coverage,
unchanged inputs and binary identity, all 17 families, and either an exact
successful agreement or a classified resource-only timeout. Unsupported inputs,
nonzero exits after an answer, disagreements, and raw bug artifacts block
the next timing run.
`slurm_lra_default_compute_comparison.sbatch` refuses other CPU generations
and records host, CPU affinity and job identity for each new timing shard.
It compares the candidate's automatic default with explicit opt-outs over the
full 1,753-input corpus, followed by separate SAT replay for both arms. The
longer paper-profile comparison measures explicit flags in source `23f945d`;
the candidate changes only automatic option selection, not the underlying
algorithms. Results from these two CPU generations remain separate.

The paired candidate run on Xeon E5-2690 v4 compute nodes has completed its
timing stage: 1,525 solves for the automatic default versus 1,461 with both
opt-outs; PAR2 sums are 17,495.135 and 21,513.642 seconds, respectively.
The 64 net gains comprise 89 wins and 25 losses, with 18.68% lower PAR2.
All 1,826 successful SAT rows passed separate direct Dolmen replay: 891 for
the former default and 935 for the automatic policy, with complete coverage
of all 64 replay shards and unchanged parent data/binary identities.
This is an independent compatibility measurement of the
automatic policy; its timings are not pooled with the Gold-node data.

The gains survive in the current implementation: the manuscript reported
22.84% lower PAR2 on the full corpus and 27.60% on the selected corpus;
the new Gold comparison gives 20.81% and 28.75%, and the separate compute
comparison gives 18.68%. The combined profile is selected for its larger
full-corpus gain; model phase alone remains a useful long-cutoff alternative,
available through `--no-monotone-elimination`. All 6,652 successful native
SAT runs across the three comparisons are independently validated. This
confirms current relative gains, without attributing absolute timing changes
to implementation fixes or claiming a reproduced held-out split.

```{figure} _static/qf-lra-default-policy-20261009-full30.svg
:alt: Automatic and former Primo defaults on matching compute CPUs

Qualified automatic-policy comparison, source `f0fbe67`, Xeon E5-2690 v4.
The committed summary is `benchmarks/qf-lra-default-policy-20261009-full30.json`.
The longer Gold-node reference comparison remains a separate population.
```

Written 2026-07-27 as a design note and extended through 2026-07-31. It picks
up where the competitiveness roadmap's items 0-8 (see [](arithmetic.md)'s
Milestone roadmap) left off, and is grounded in a fresh local
profiling/instrumentation pass run at commit `35878c4` -- not in the older
3-file evidence items 0-6 were designed against.

## Items at a glance

Every item, its status, and where it is defined. Items are numbered in the
order they were proposed, not the order they were implemented.

| item | what | status |
|---|---|---|
| [H1](#h1) | Direct bounds for single-variable atoms; scale-canonical polynomial keys | implemented, default |
| [H2](#h2) | Small-rational fast path in `util::Rational` | implemented, default |
| [H3](#h3) | Fill-in-aware (Markowitz-style) entering-variable selection | implemented, default |
| [H4](#h4) | Batched bound assertion (deferred `update()`) | design only |
| [H5](#h5) | Model-based phase selection | implemented, **opt-in** -- needs measurement |
| [H6](#h6) | A difference-logic fast path | design only |
| [H7](#h7) | Fixed-variable elimination (equality slacks) | implemented, **opt-in** -- did not clear its bar |
| [H8](#h8) | Merge instead of sort in `pivot`'s affected-row loop | implemented, default; **zero regressions** |
| [H9](#h9) | Bounds-only backtracking (the paper's own scheme) | implemented, default except SOI; largest single gain |
| [H10](#h10) | Delta-maintained `occurs_in_` (no round trip per pivot) | implemented, default; **+41/-0**, took Primo past z3 (Yices and OpenSMT, both added 2026-08-03, are ahead of both) |
| [H11](#h11) | Fill-in-aware pivoting in *preprocessing* Gaussian elimination | proposed, opt-in when built |
| [T1](#t1) | Guarded Real equality distribution for tropical cost definitions | implemented, opt-in; local tropical evidence is mixed |

Where the remaining gap is, and what has been ruled out, is [](#sec-6). Its
ranked list of open directions is [](#sec-6-5): [D2](#d2) and [D4](#d4)
remain open, [D1](#d1) and [D3](#d3) were measured and rejected. The largest
known engineering item is in [](#sec-6-2c).

**Where things stand on the 237-file promotion bar** (10s timeout, z3 167 and
cvc5 150 unchanged throughout):

| | solved / 237 | PAR2 | gap to z3 |
|---|---|---|---|
| before this note (job 114200) | 108 (45.6%) | 2675.6s | 59 files |
| + H2 (job 114900) | 124 (52.3%) | 2380.9s | 43 files |
| + H1a (job 115172) | 136 (57.4%) | 2149.8s | 31 files |
| + H1b (job 117222, `bland`) | 144 (60.8%) | 1962.4s | 23 files |
| + H3 (job 117222, new default) | **168 (70.9%)** | **1538.9s** | **z3 is 167** |

Zero wrong answers throughout, and zero regressed files at every step. **Primo
now solves one file more than z3 (168 vs 167) with PAR2 essentially tied
(1538.9s vs 1531.6s)**, and the two are complementary rather than one
dominating: 6 files z3 solves and Primo does not, 7 the other way round.
cvc5 is at 150.

Sections 1.1 and 1.2 below describe the state *before* any of this; Section
2's per-item status blocks describe what changed.

The roadmap's own closing lesson is taken as binding here: **the 9-file
"dramatic-gap" sample is for *finding* techniques, the 237-file stratified
sample (`benchmarks/qf-lra-stratified-sample.txt`) is the promotion bar.**
Every item below therefore states which families it targets, why, and what
would falsify it.

### Terminology and sources

Defined once here so the rest of the note can use them freely; the sources
are in [](papers/lra-related-work.md).

| term | meaning | source |
|---|---|---|
| **General Form** | `Ax = 0` with `l_j <= x_j <= u_j`, as against Standard Form's `Ax = b, x >= 0`. The authors' own name for the representation, and what makes only *bounds* get asserted during search -- never new rows | Dutertre & de Moura, "A Fast Linear-Arithmetic Solver for DPLL(T)", CAV 2006, and their [CAV 2006 slides](https://yices.csl.sri.com/old/slides-cav06.pdf) |
| **Assert-Bound / Check / Pivot** | the three operations of the General Form Simplex this solver implements. The authors describe the algorithm as "based on the Dual Simplex" | same, Section 3 |
| **basic / non-basic variable** | a variable that does or does not have a row defining it. The authors' slides say **dependent** / **non-dependent** for the same distinction | same |
| **Farkas certificate** | the conflict explanation: a subset of asserted bounds whose non-negative combination is contradictory, returned as a clause | same, Section 3 |
| **`DeltaRational`** | a value `q + k·delta` for a symbolic infinitesimal `delta > 0`, representing strict bounds exactly without choosing a concrete delta | same, Section 3.1 |
| **Bland's rule** | pick the smallest-index eligible leaving and entering variable; the only selection rule with a direct termination proof, and the fallback all others revert to past a per-call pivot cap | same |
| **Dantzig's rule** | pick the largest violation and largest-magnitude coefficient -- prices *progress* | standard LP; in SMT, Griggio's most-violated rule (CVC4, MathSAT5) per King's thesis Section 2.2.12 |
| **SOI** (Sum of Infeasibilities) | replaces the single-row objective with a global one -- minimise the summed bound violation across *every* out-of-bound basic variable at once, giving pivot selection a progress criterion Bland and Dantzig both lack | King, Barrett & Dutertre, "Simplex with Sum of Infeasibilities for SMT", FMCAD 2013. Implemented as `--lra-pivoting-rule soi` |
| **Markowitz product** | `\|occurs_in(entering)\| × \|row(leaving)\|` -- what a pivot *costs*, since it rewrites that many rows by merging in a row of that length. Item H3 prices by it | standard sparse-LU practice; the sparsity criterion is Monniaux, CAV 2009, p.8 |
| **fill-in** | the growth in nonzeros per row as pivoting proceeds. Rows here start at 4-6 nonzeros and reach 22-91 during search, and each pivot makes the next more expensive | section 6.2 measures it |
| **PAR2** | the competition scoring convention: solve time for a solved instance, **twice the timeout** for an unsolved one. Lower is better; it rewards both solving more and solving faster | SAT/SMT competition standard |
| **FTRAN / BTRAN** | in *revised* simplex, solving `Bx = a` (forward) and `Bᵀx = a` (backward) against a factored basis, which is how rows and columns are materialised on demand instead of stored | standard LP; relevant only to direction D4 |
| **Forrest-Tomlin / Bartels-Golub** | the two standard schemes for updating an LU factorization after a pivot rather than refactorizing | standard LP; relevant only to D4 |

Two naming notes worth stating once. **"Sparse" here is Primo's own name**
for the `PivotingRule` that prices by the Markowitz product; the field calls
the entering half minimum-column-length selection, and King's thesis records
it as what MathSAT, OpenSMT, Yices, Yices 2, z3 and CVC4 all do -- so H3 is
standard practice, not a novelty. And **`delta` is symbolic**, never a chosen
small number; that is the point of the representation.

## 1. Where the remaining gap actually is

From the last full 237-file comparison (Slurm job 114200, 10s timeout;
primo 108, z3 167, cvc5 150; zero wrong answers), **61 files are solved by
z3 or cvc5 but not by Primo**. Grouped by family, with the fastest
reference time:

| family | gap files | fastest ref | note |
|---|---|---|---|
| TM | 9 | 0.18s | Boolean-guarded arithmetic chains |
| uart | 9 | 0.19s | ditto |
| sc | 8 | 0.19s | min/max comparator chains |
| 2019-ezsmt | 7 | 0.33s | LP/blending + labyrinth |
| latendresse | 6 | 0.54s | pure conjunctive LP (flux balance) |
| miplib | 5 | 0.57s | LP relaxations |
| tta_startup | 5 | 0.13s | near-difference constraints |
| DTP-Scheduling | 4 | 0.46s | **pure** difference constraints |
| clock_synchro | 4 | 0.49s | near-difference constraints |
| Heizmann / LassoRanker | 3 | 2.17s | hard for everyone |

Families where Primo already matches both references (keymaera,
meti-tarski, sal, spider_benchmarks, check) and families where all three
solvers fail (tropical-matrix) are excluded from the design targets below.

### 1.1 Three distinct bottleneck classes, not one

Fresh `--stats` runs (local, Release `build/primo`, commit `35878c4`)
separate the gap files into three classes with *different* dominant costs:

| instance | answer | time | decisions | conflicts | pivots | check calls |
|---|---|---|---|---|---|---|
| `latendresse/ecoliFBAyicesTest-500` | (timeout) | >30s | **0** | 0 | <2000 | **1** |
| `DTP-Scheduling/constraints-tempo-width-30` | sat | 19.5s | 60,750 | **0** | 7,182 | 63,021 |
| `clock_synchro/clocksynchro_2clocks.main_invar.induct` | unsat | 13.9s | 1,515 | 116 | 8,997 | 1,072 |
| `miplib/pp08a-12000` | sat | 13.9s | 4,564 | 70 | 8,314 | 910 |
| `tta_startup/simple_startup_4nodes.bug.induct` | sat | 7.1s | 35,878 | 2,893 | 46,490 | 19,287 |

- **Class A -- pure LP, one `check()` call.** `latendresse` has *zero*
  decisions: the formula is one big conjunction, so the entire runtime is a
  single Simplex solve that never finishes. Nothing about SAT integration,
  propagation, or decision heuristics can matter here. (Note the irony:
  these are the very flux-balance benchmarks the SOI paper was written for,
  and `--lra-pivoting-rule soi` times out on them too.)
- **Class B -- pivot-cost dominated.** `clock_synchro` and `miplib` need
  only 1.5k-4.5k decisions but spend ~1.5 ms *per pivot*.
- **Class C -- assignment-volume dominated.** `DTP-Scheduling` makes 60,750
  decisions and **zero conflicts** -- the SAT solver never learns anything,
  it just walks to a model -- yet takes 19.5s, i.e. ~320 µs per decision.

### 1.2 Profile: the cost is allocation, not arithmetic

`perf record -g` on `constraints-tempo-width-30` (Class C, the *least*
pivot-heavy of the three classes), flat self-time:

```
31.4%  LraTableau::pivot
42.0%  allocator  (_int_malloc 10.5, _int_free 7.9, malloc 5.8, cfree 5.2,
                   malloc_consolidate 5.2, unlink_chunk 4.3, gmp_default_allocate 1.5, ...)
12.0%  GMP copy/init (__gmpn_copyi 5.6, __gmpz_init_set 3.4, __gmpq_clear 1.6, ...)
 5.0%  GMP arithmetic (__gmpz_mul 1.4, __gmpq_mul 1.0, __gmpz_gcd 0.8,
                       __gmpn_gcd_1 0.7, __gmpz_divexact_gcd 0.5, __gmpq_aors 0.5)
 1.3%  LraTableau::rebuild_occurs_in_index
```

This **corrects a conclusion recorded during roadmap item 0's follow-up**,
which stated that what remained after the container fixes was "GMP's own --
`mpq_class`'s arithmetic operators canonicalize (GCD-reduce) on every
add/multiply by design, an inherent cost of exact rational arithmetic no
container-level fix can remove." Measured: canonicalization/GCD is under
2%, and *all* real bignum arithmetic is ~5%. Roughly **54% of runtime is
malloc/free and copying of tiny numbers**. The values are small; the
representation is expensive.

### 1.3 Instrumentation: tableau shape and fill-in

Temporary instrumentation (added, measured, reverted -- not committed)
reported the prepared tableau's shape and how row density evolves during
search.

Prepared tableau shape (one row per distinct polynomial, as built by
`PreparedLraTheory`):

| instance | rows | rows that are a **single variable** | share | avg row | max row |
|---|---|---|---|---|---|
| `latendresse/ecoliFBAyicesTest-500` | 4,555 | 3,161 | **69%** | 3.39 | 691 |
| `TM/p6-zenonumeric_s5` | 1,378 | 1,311 | **95%** | 1.97 | 50 |
| `miplib/pp08a-12000` | 400 | 262 | **66%** | 2.52 | 123 |
| `sc/sc-17.induction3` | 1,185 | 462 | 39% | 1.82 | 8 |
| `uart/uart-17.induction` | 1,446 | 526 | 36% | 1.75 | 3 |
| `tta_startup/simple_startup_4nodes.bug.induct` | 584 | 84 | 14% | 2.01 | 4 |
| `clock_synchro/clocksynchro_2clocks` | 264 | 5 | 2% | 2.75 | 7 |
| `DTP-Scheduling/constraints-tempo-width-30` | 3,932 | 0 | 0% | **2.00** | **2** |
| `LassoRanker/selectSort...phaseTemplate` | 9,469 | 653 | 7% | 5.50 | 15 |

Fill-in during search (sampled every N pivots):

| instance | initial avg row | avg row during search | max row | max `occurs_in` |
|---|---|---|---|---|
| `DTP-Scheduling/constraints-tempo-width-30` | 2.00 | 3.7 - 8.9 | 33 | **2,127** |
| `clock_synchro/clocksynchro_2clocks` | 2.75 | 8.1 - 14.3 | 34 | 174 |
| `latendresse/ecoliFBAyicesTest-500` | 3.39 | 7.9 - 23.9 | 961 | 183 |

Two facts follow directly, and they drive most of this note:

1. **A third to 95% of all tableau rows are trivial rows of the form
   `s = c·x`** -- one slack variable and one row allocated per bare
   single-variable atom. They are pure overhead: rows to pivot on, entries
   in `occurs_in_`, candidates in the violated-variable scan, and targets
   for fill-in.
2. **Rows densify 3-5x during search**, and a single variable can occur in
   over 2,000 rows. Since `pivot()`'s cost is
   `|occurs_in(entering)| × |row(leaving)|` row-combinations of GMP
   rationals, this is exactly the 31%-self-time hot spot -- and *no* rule
   currently implemented (Bland, Dantzig, SOI) prices a pivot by its cost.
   All three price only for *progress*.

## 2. Proposed heuristics, in priority order

Priority = (measured share of the bottleneck) × (breadth of families hit) ÷
(implementation and correctness risk).

(h1)=
### H1 -- Direct bounds for single-variable atoms (+ scale-canonical polynomials)

**Status: both halves implemented, unconditional and promoted.** H1a: 124 ->
136 solved on the 237-file bar, PAR2 -9.7%, zero regressions. H1b: 136 -> 144,
PAR2 -8.7% (+10 solved, -2 regressed), measured as the `bland` column of item
H3's four-way run rather than a run of its own. H1b's local evidence had been
genuinely mixed -- clock_synchro 3.1x faster and uart-18 1.5x, but
DTP-Scheduling 0.74x *slower* despite 8.6x fewer decisions -- and that
regression did not generalize: on the broad sample DTP-Scheduling went 11 ->
12. A reminder of why the broad sample is the bar and a single instance is
not.** See [](arithmetic.md)'s
"Direct bounds for single-variable atoms". The predicted row reductions were
confirmed exactly, family by family, via a new `:lra.rows` statistic --
ecoliFBA -69%, TM/p6 -95%, pp08a -66%, sc-17 -39%, uart-17 -36%, tta_startup
-14%, clock_synchro -2%, DTP-Scheduling unchanged -- each matching Section
1.3's measured single-variable share for that instance. Paired local A/B on
top of H2: TM/p5 5.3x, sc-21 3.2x, uart-18 1.3x, miplib/pp08a 1.2x,
clock_synchro and DTP flat (as their row counts predict).

**Prior art**: Monniaux (CAV 2009) Section 2.1 states this as ordinary
practice -- see [](papers/lra-related-work.md).

**Targets:** latendresse, TM, miplib, sc, uart (36-95% of rows removed);
neutral elsewhere. **Class A and B, plus the whole TM/sc/uart cluster.**

**Problem.** `PreparedLraTheory` allocates a slack variable and a tableau
row for *every* distinct polynomial, including a bare `x`. So `(<= x 2)`
becomes `s = x; s ≤ 2` instead of just `x ≤ 2`. Worse, polynomial keys are
exact-content matches, so `(<= 0 r1)` (normalizing to `-r1 ≤ 0`) and
`(<= r1 1000)` produce **two different slacks for the same variable** --
the tableau cannot even see that `r1` has both a lower and an upper bound
without pivoting. In `ecoliFBAyicesTest-500` that is 3,161 of 4,555 rows.

This is also a deviation from the source algorithm: Dutertre & de Moura's
Section 2 introduces slack variables only for *compound* polynomials;
atoms over a single variable are asserted directly as bounds on it.

**Design.**

- Build the sharing map once in `PreparedLraTheory`, while walking the complete
  theory-atom list. Normalize each atom to $p\mathrel{\sim}c$ with no constant
  in $p$, sort $p$'s terms by `TermId`, and divide every coefficient and
  $c$ by the coefficient of the first term. A negative divisor reverses the
  bound orientation. The resulting exact sorted coefficient vector is the
  `PolynomialKey`; relation, strictness and scaled right-hand side are not in
  the key because they are bounds on the shared polynomial value.
- The mandatory early atom pass now performs the primitive-integer GCD rewrite
  on the core predicate, so `2x+3y<=5` and `4x+6y<=10` hash-cons to the same
  SAT atom. `PreparedLraTheory` still applies leading-coefficient division as a
  defensive boundary, representing that direction internally as
  `x+3/2y<=5/2`. Both forms identify the same rational scale class.
- Look up each compound key in `slack_by_polynomial`. The first occurrence
  allocates a slack and defining row; every later proportional atom reuses that
  slack as its `PreparedAtom::bound_var`. This lookup, rather than a comparison
  during an individual theory call, is what identifies the opportunity to
  share.
- If the canonical polynomial has exactly one term, it is now exactly `x`:
  do **not** allocate a slack or a row. Store
  `PreparedAtom{var = x, relation, rhs}` and assert the bound directly on
  the structural variable.
- Rename/generalize `atoms_by_slack` to an index from "tableau variable a
  bound was just asserted on" to "atoms that could now be entailed". The
  existing same-slack propagation scan works unchanged over it, and now also
  relates atoms that previously lived on different slacks. *(As built, this
  is `PreparedLraTheory::atoms_by_bound_var`; the design text above said
  `atoms_by_var`, which never existed under that name.)*
- Delete `single_variable_slack` / `variable_for_single_slack` entirely.
  Row-based propagation (`compute_implied_bound`) can then read a row
  term's bound *directly* from `LraTableau::upper_bound`/`lower_bound`
  with no indirection, which both simplifies it and strictly increases its
  reach -- including for terms that today have no single-variable wrapper
  atom at all.

**Bonus:** the canonicalization step is exactly the not-yet-scheduled
roadmap item 3 (scale-invariant polynomial matching). `x <= 5` and
`2x <= 10` normalize to the same direct bound and need no slack. Compound
atoms are prepared globally: `PreparedLraTheory` allocates one stable
`s = x + y` row for the complete scale-equivalence class, before any theory
call. Thus `x + y <= 5` and `2x + 2y <= 10` both become `s <= 5`, while
`2x + 2y <= 12` becomes the distinct bound `s <= 6` and
`-x - y <= -5` becomes `s >= 5`. Every one of those SAT atoms points to the
same `bound_var` across all theory calls and backtracking states. A currently
weaker asserted upper bound is ignored by the tableau, but cannot be removed
from the whole formula because another Boolean assignment may assert it alone
or negate it. With the default-off `--arithmetic-atom-canonicalization`, exact
equivalent source atoms are interned by the primitive-integer pass before
clausification; distinct right-hand sides remain distinct SAT atoms. See
[](papers/lra-atom-canonicalization.md).

**Also fixes**, without extra work, the quirk item 7 documented and worked
around: "`single_variable_slack` only registers a bare variable's
*positive*-coefficient normalized form, so `>=` atoms never get an entry".

**Risk: low-to-moderate.** Bounds now land on structural variables, which
can become basic through pivoting -- but `assert_upper`/`assert_lower`
already handle a basic target (`update_violation_status` + SOI direction
branch), and Farkas conflict generation already reads each variable's own
bound reason regardless of whether it is a slack. `LraTableau` itself needs
**no change at all**; this is entirely a `check_sat.cpp`
`PreparedLraTheory`/`OnlineLraPropagator` change. The correctness-sensitive
part is the rhs rescaling and relation flip (`c·x ≤ k` is `x ≤ k/c` for
`c > 0` and `x ≥ k/c` for `c < 0`), plus the strict/non-strict handling in
`DeltaRational` -- all directly unit-testable.

**Falsifier:** if row counts drop as predicted but wall time does not
improve on latendresse/TM/miplib, then row *count* was never the cost and
H3/H2 are the whole story.

**Falsifier outcome: partially triggered, and informative.** Row counts fell
exactly as predicted everywhere. Wall time followed on TM (5.3x), sc (3.2x),
uart and miplib -- but **not on latendresse**, which lost 69% of its rows and
did not move at all. So for the Class A pure-LP instance the falsifier fired:
row count was never its cost. What remains there is a 1,394-row LP with a
691-term dense row and no Boolean structure whatsoever, which is precisely
H3's fill-in problem. That sharpens H3 from "helps Class B" to "is the only
remaining lever for Class A", and it is the concrete reason to do H3 next.

(h2)=
### H2 -- Small-rational fast path in `util::Rational`

**Status: implemented, unconditional, 2026-07-28.** See
[](arithmetic.md)'s "Two-tier rational representation" for the shipped design
and `docs/run-log.json` for the verification trail. Paired local A/B on the
profiled instances: **2.1x-5.2x faster** (best-of-3), with every search counter
bit-identical (a pure representation change). **On the 237-file promotion bar:
solved 108 -> 124, PAR2 -11%, 16 newly solved, zero regressions, zero wrong
answers** (Slurm job 114900) -- it clears the bar that item 8 failed. Clean at
merge-gate and campaign-gate fuzz tiers on both pivoting paths. Two design
points below were
revised during implementation and are corrected in place: the small form is
64-bit rather than 32-bit (an ablation, not a settled question), and demotion
is **required**, not forbidden -- see the canonical-form paragraph.

**Prior art**: this is Monniaux's own implementation choice (CAV 2009,
Section 4), found only after the profiling above independently reached the same
diagnosis -- see [](papers/lra-related-work.md).

**An alternative that was never evaluated:** GMP cannot provide a small form
itself (its struct is pointer-based and its layout is frozen ABI), but other
libraries can and do -- FLINT's `fmpq` is this exact scheme. So the real choice
was "keep GMP and add a fast path" against "adopt a library that already has
one", and only the first was considered. Tracked as [D5](#d5), where it is
deliberately ranked as not a priority: H2 already took the win that was on the
table, and the reason to revisit would be shedding the canonical-form
invariant rather than chasing speed.

**Targets:** everything. **The single largest measured lever (54%).**

**Problem.** `util::Rational` wraps `mpq_class`; every value is two
heap-allocated GMP integers, and `DeltaRational` holds two of them. Section
1.2 measures ~42% allocator + ~12% GMP copy/init against ~5% actual
arithmetic.

**Design.** Tagged small/big representation behind the existing (already
fully encapsulating) public API:

- Fast path: `std::int64_t num, den` with `den > 0` and `gcd(num,den) = 1`,
  stored inline. Knuth's two-gcd addition and cross-reduced multiplication
  (TAOCP Vol. 2, Sec. 4.5.1) in `__int128`; binary GCD on machine words.
  *As shipped*, no overflow checks on the intermediates are needed at all:
  canonical operands bound every product by `2^126` and every sum by
  `2^127`, so only the reduced result needs a range test.
- Slow path: promote to a heap `mpq_class` when the reduced result doesn't
  fit -- and **demote again as soon as a result does fit**. The original
  version of this note had that backwards ("stay promoted -- demotion is a
  correctness footgun"); it is the opposite. Demotion is what makes the
  representation a function of the *value*, which is precisely what
  `RationalHash` needs: without it the same number could be stored two ways
  and hash two ways, and `PolynomialKey` hash-consing would silently stop
  matching. Implemented as a canonical-form invariant (big form iff the
  value does not fit small) established at exactly two private factories and
  asserted by a debug/ASAN-only `check_invariant()`.
- `sign()`, `is_zero()`, comparison, and negation are branch-light and
  allocation-free on the fast path. With the invariant in place,
  `RationalHash` can hash the small pair directly and `operator==` can
  reject a form mismatch outright.
- No call-site changes: `mpq_class` already appears only inside
  `rational.hpp/.cpp`, exactly as that file's own comment intends.
- Width: 64-bit rather than Monniaux's 32-bit, behind the single `kSmallMax`
  knob so the two can be compared empirically. **Ablation run 2026-07-28 --
  64-bit kept.** On instances whose coefficients never leave 32 bits the two
  widths are within noise, or 32-bit is up to ~13% faster (`__int128`
  operations are multi-instruction); on the one instance that does overflow,
  64-bit is 2.2x faster because it cuts big-form operations 12-fold. A few
  percent on the common case for a factor of two on the hard one. The
  ablation also turned up a real latent bug -- see [](arithmetic.md).

**Risk: moderate, concentrated and testable.** It is the numeric core, so a
bug is a wrong answer rather than a slowdown. Mitigation as planned:
randomized differential unit tests against `mpq_class` for `+ - * /`, all
comparisons, `sign`, `is_zero`, `to_string`, and `parse`, with adversarial
magnitudes straddling the overflow boundary; ASAN/UBSan suite; and the full
campaign-tier fuzz gate on both pivoting paths (unconditional core code, so a
campaign-gate change by the standing precedent).

*This paid off during implementation*: the ASAN run caught a real disagreement
between `from_mpq`'s "fits small" test (`mpz_fits_slong_p`, which admits
`INT64_MIN`) and the small form's own range (which excludes `INT64_MIN` so
negation can never overflow) -- the two differed by exactly that one value.
Fixed by giving both a single shared predicate. A separate deliberate-mutation
check (dropping demotion) confirmed the unit tests fail loudly when the
invariant is broken, which matters because that particular bug is otherwise
silent.

**Expected:** removal of most of the ~54%; realistically 1.5-2x
solver-wide on QF_LRA, and it compounds with every other item here since
they all reduce *counts* of the same operations.

**Measured: 2.1x-5.2x** on the profiled instances -- better than expected,
because the allocator cost was compounding across `DeltaRational` (two
`Rational`s per bound and per assignment) rather than being paid once. The
weakest case (`clocksynchro_2clocks`, 2.1x) is the one instance whose
coefficients genuinely outgrow 64 bits: 1.22M promotions, so it keeps paying
GMP's cost. That is also the first hard evidence for which instances the
width ablation could still move.

(h3)=
### H3 -- Fill-in-aware (Markowitz-style) entering-variable selection

**Prior art, checked 2026-07-30 (after implementation).** This rule is the field's standard rather than a technique transferred from one paper. King's thesis (NYU 2014) Section 2.2.12 records minimum-column-length entering selection with a Bland fallback past a finite cap as what MathSAT, OpenSMT, Yices, Yices 2, z3 and CVC4 all do -- i.e. H3 and its anti-cycling story exactly. The Monniaux citation below remains an accurate source for the sparsity criterion, but H3 should not be read as novel. The *leaving*-side knob is the part with no counterpart there: King records Griggio's most-violated rule (CVC4, MathSAT5), which is our Dantzig choice, not our shortest-row one. See [](papers/lra-related-work.md).

**Status: implemented 2026-07-28, promoted to default 2026-07-29
(`LraPivotingRule::Sparse` with the leaving-side half on).** On the 237-file
bar: **168/237 against the former default's 144, PAR2 1538.9s vs 1962.4s,
+24 files with zero regressions**, campaign-gated on both pivoting paths.
See [](arithmetic.md) and `docs/run-log.json`. See [](arithmetic.md)'s "Fill-in-aware pivot
selection" for the shipped design. First local numbers, and one result that
was not anticipated:

| instance | bland | dantzig | sparse | sparse + leaving |
|---|---|---|---|---|
| `sc-21.induction3` | 1.56s | 3.43s | **0.72s** | -- |
| `clock_synchro/clocksynchro_2clocks` | 1.17s | 0.50s | **0.22s** | -- |
| `DTP-Scheduling/constraints-tempo-width-30` | 7.41s | 5.96s | **1.93s** | -- |
| `miplib/pp08a-12000` | 1.63s | **0.86s** | 1.82s | -- |
| `latendresse/ecoliFBAyicesTest-*` (6 files) | timeout | ~19.5s | timeout | **~0.65s** |

**The unanticipated result is the last row.** The leaving-side policy
(repair a short row first, now default-on and ablated by
`--no-lra-sparse-leaving`) was planned as a
lower-priority follow-up precisely because it is *not* progress-neutral. It
turns out to be what cracks Class A: every `latendresse` file goes from
"never solved by Primo" to ~0.65s, roughly 30x faster than the only other
rule that solves them at all, with `:lra.pivot-term-ops` 8.7M against
Dantzig's 130.9M. That family is 0/15 on the promotion bar today and z3 only
manages 6/15.

In hindsight it is the expected shape: a pure-LP instance is one `check()`
call with no Boolean structure, so "which infeasibility to repair" has no
search-trajectory consequences to get wrong -- the objection that made the
knob risky simply does not apply to Class A. It is still a real risk on the
Boolean-heavy classes, which is why it remains separately ablatable and gets
its own measurement column.

Second finding, also worth keeping: **Dantzig had not been run on the
237-file bar before this promotion experiment**, and it solves `latendresse`
where Bland does not. Any sparse-vs-bland comparison that omits it would
misattribute part of the win, which is why the promotion run is four-way
(bland / dantzig / sparse / sparse+leaving).

**Targets:** Class B (clock_synchro, miplib) and Class A (latendresse)
directly; helps Class C too, since Section 1.2's profile is from a Class C
file and still shows `pivot()` first.

**Problem.** `pivot(leaving, entering)` rewrites every row in
`occurs_in_[entering]`, merging in the new entering row -- cost
≈ `|occurs_in(entering)| × |row(leaving)|` rational operations. Bland picks
the *smallest index* entering candidate; Dantzig picks the *largest
|coefficient|*; SOI picks by global objective decrease. **None of the three
considers what the pivot will cost or how much fill-in it creates**, and
Section 1.3 measures rows densifying 3-5x with `occurs_in` reaching 2,127.

In floating-point LP that trade-off is dominated by numerical stability, so
textbook pricing rules ignore fill-in and leave it to the LU
refactorization layer. Primo has *exact* rationals: there is no stability
constraint at all, and no refactorization layer -- so the fill-in term is
unopposed, and this is where Primo's cost structure genuinely differs from
the LP literature's assumptions.

**Shipped definition.** The initial entering-side experiment used Dantzig's
most-violated leaving choice. The promoted profile additionally enables the
shortest-row leaving choice, so describing current Sparse as merely
“most-violated plus minimum column length” is stale. The authoritative formal
definition, including row/column length, movement eligibility, exact
lexicographic tie-breaks, bounded cyclic pricing, both leaving-policy
ablations, and the Bland suffix, is in [](arithmetic.md)'s “Fill-in-aware pivot
selection” section.

In brief, the default first minimizes
`(row_length, -violation, variable_id)` over violated basic rows. Within the
chosen row it scores at most *k* eligible entering variables and minimizes
`(column_length, nonunit_coefficient, variable_id)`. Row length is the number
of nonzero right-hand-side terms; column length is the number of current basic
rows containing that nonbasic variable. This is a sequential, partially priced
cost heuristic, not a global minimization over every feasible pivot pair.

**Risk: low.** Purely a selection change inside `check()`; no new state, no
trail interaction, no explanation change. The real risk is *empirical*:
cheaper pivots but more of them. That is why `:lra.pivots` must be reported
alongside wall time in the evaluation, and why it lands opt-in.

**Falsifier:** if pivot counts rise more than per-pivot cost falls on
clock_synchro/miplib, the rule is net-negative and should be rejected
outright rather than tuned -- item 8's cost-ranking episode is the
cautionary precedent for tuning a heuristic that was steering search wrong.

**Falsifier outcome: fired on `miplib/pp08a` for the entering-side rule
alone** (pivots +43%, term-ops +19%, 1.63s -> 1.82s) -- and that was the
right signal to keep measuring rather than to tune. Adding the leaving-side
half turned the same instance into 1.36s, better than Bland, and the broad
sample settled the question decisively at +24 files with no regressions.
The lesson is the one item 8 taught, applied correctly: take the broad
sample's verdict, and neither tune a losing heuristic nor discard one on a
single instance.

(h7)=
### H7 -- Fixed-variable elimination (equality slacks)

**Status: designed 2026-07-29, implemented 2026-07-30, and NOT promoted --
it ships opt-in behind `--lra-fixed-elimination` (`eed259c` .. `ebd2533`).**
It did not clear the 595-file scrambled bar, and its row-freezing half was
deleted outright. See the falsifier outcome at the end of this item; that is
the part worth reading. This is also the first item in this note whose
evidence comes from a set Primo was never tuned against.

**Where it comes from** *(figures as of 2026-07-29, when H7 was designed --
[H8](#h8) and [H9](#h9) have since taken this to 399; see [](arithmetic.md)
for the current standing)*. On the 595-file scrambled QF_LRA competition set
at a 60s timeout, Primo solved 373 against z3's 429 and cvc5's 351 -- ahead
of cvc5, behind z3, with zero cross-solver disagreements. The whole deficit sits in one
size band:

| file size | n | primo | z3 | cvc5 |
|---|---|---|---|---|
| 0-50 KB | 182 | 180 | 181 | 181 |
| 50-500 KB | 191 | **142** | 134 | 122 |
| 500 KB - 5 MB | 173 | **43** | **100** | 43 |
| > 5 MB | 49 | 8 | 14 | 5 |

Primo leads *both* references below 500 KB and collapses above it, landing on
exactly cvc5's number in the 500 KB-5 MB band while z3 more than doubles them.

**It is not a preprocessing size problem.** That was the first hypothesis and
it is wrong: on four profiled gap instances Primo's prepared tableau matches
z3's reported dimensions within ~5% every time (9,315x11,633 against z3's
9,000x11,330; 8,960x10,479 against 8,892x10,415; and so on). Primo also already
folds the scrambler's `(* 1.0 t)` / `(+ t 0.0)` obfuscation -- checked
directly, term size and solve-eqs counts are identical with and without it.
Both solvers build the same problem.

**The difference is what happens to equalities.** These instances are dominated
by them -- 8,226 to 40,820 lowered Real equalities per file, against 4,822 to
24,976 tableau rows, so most rows exist because of an equality. Primo lowers
`t1 = t2` to `t1 <= t2` and `t2 <= t1`; since item H1b those two normalize onto
one shared slack, which then receives an upper *and* a lower bound at the same
value. **That slack is fixed, and Primo does nothing with the fact** -- it stays
a live, pivotable row for the rest of the search.

z3 does something with it, and reports how much: `:arith-fixed-eqs` 168-52,364
and `:arith-offset-eqs` 855-68,937 per instance. Its search then barely runs --
on `scrambled126657` it needs **9 decisions and 4 conflicts** where Primo makes
141,124 decisions and 321M pivot term-operations. Profiling Primo there puts
86% of runtime in `LraTableau::pivot`, averaging 431 rows rewritten per pivot.

**Prior art, and the setting difference that explains the result.** Removing
fixed variables is standard LP *presolve*, and the canonical reference is
Andersen & Andersen, "Presolving in linear programming", Mathematical
Programming 71 (1995), 221-245
(<https://link.springer.com/article/10.1007/BF01586000>; see
[](papers/lra-related-work.md)). H7 is not that technique transferred intact,
and the difference is exactly where the result comes from: presolve removes
fixed variables **once, before solving**, from a problem whose bounds are all
known, so the removal is paid for once and never undone. In DPLL(T) a variable
becomes fixed *during* search, when two bounds asserted at some decision level
happen to coincide, and every elimination must be trailed and restored on
backtracking. The paper offers no guidance on that regime, and it is the most
likely reason H7's per-instance wins do not survive aggregation -- see the
falsifier outcome below. (Paper cited for provenance, not read in full; no
result from it is claimed here.)

**Design, as implemented (commit `eed259c`).** The mechanism turned out to be
much smaller than the design sketch above suggested, because of one property
the tableau already had: **`can_enter()` already rejects a fixed variable.**
With `lower == upper == assignment`, every branch of its `assignment < upper` /
`assignment > lower` test is false. A fixed *non-basic* variable is therefore
already completely inert -- it is never selected to enter, so its column never
moves. Only fixed *basic* variables churn, and the fix is to stop them being
basic.

So `assert_upper`/`assert_lower` now do exactly this when they make a basic
variable's bounds coincide:

1. **Pivot it out**, choosing an entering variable from its row, then
   `update()` it to its fixed value -- which is mandatory anyway, since
   non-basic variables must always sit within their bounds, and is precisely
   what makes it inert. No substitution pass and no constant column are needed:
   the new basic variable's dependence on the now-inert fixed variable *is* the
   constant term.
2. **Prefer an entering candidate with no bounds**, then the Markowitz
   occurrence count, then a `+/-1` coefficient -- and never one that is
   *itself* already fixed, which the other criteria actively steer into
   (a fixed slack tends to occur in few rows and to carry a unit
   coefficient). An unbounded basic variable's row can never be violated, so
   it never demands another repair pivot.

A third step -- **freezing** the resulting row out of the working set
entirely when its variable was provably unconstrainable -- was implemented,
measured, and removed. What it cost, and why it went, is below.

**Offset equalities (`x - y = c`) remain a follow-on**, matching z3's
`arith-offset-eqs`: merge the two columns rather than carrying both. Better
attempted only once fixed-variable elimination is measured, since the two
overlap.

**Risk, and where it actually lives.** The elimination pivot is a pure change
of representation -- the same thing `check()` does on every repair -- so it
cannot produce a wrong answer. `build_conflict` needs no change either: it
takes each row term's bound reason, so a fixed variable appearing in a row
contributes the asserting literal of the equality that fixed it, which is
exactly the right Farkas antecedent.

Freezing was different, and that asymmetry is the whole reason the two halves
were built with separate switches. Its soundness reduced to a single
condition: **only freeze a row whose basic variable has no bounds and can
never acquire any.** "Unbounded right now" was not sufficient. A frozen row is
deliberately dropped out of the machinery that maintains it, so its own
assignment stops tracking its terms and its terms are no longer kept
non-basic; a bound asserted against it later would be tested against a stale
value. Only the caller knows which variables can ever be bounded -- it owns
the atom-to-tableau-variable map -- so `LraTableau::mark_boundable()` made it
say so, and an assert against a frozen row threw rather than answering from a
stale assignment.

None of that machinery survives (`ebd2533`), so **the retained mechanism has
no wrong-answer mode at all**: a pivot is what `check()` already does on every
repair. The guard is recorded here because its mutation check is the evidence
that removing it was the right call rather than a lucky one -- it was
genuinely load-bearing, so keeping the feature would have meant keeping the
risk:

- Dropping it entirely (freeze any row): caught by its dedicated unit test,
  *and* it makes an existing end-to-end test return the wrong verdict
  (`gaussian_elimination_can_be_disabled` goes from sat to not-sat). So the
  condition is genuinely load-bearing for soundness, not just for tidiness.
- Dropping only the `mark_boundable` half, keeping "unbounded right now" --
  the plausible-looking weaker guard: caught by the unit test, and on real
  benchmarks the throw fired immediately (variable 1133 on `scrambled335326`,
  1584 on `latendresse/ecoliFBAyicesTest-500`). That is the case that would
  otherwise have been a silent stale-value read.

**Falsifier.** If eliminating fixed variables does not reduce
`:lra.pivot-row-updates` proportionally to the share of rows eliminated, then
the fill-in is not concentrated in those rows and the diagnosis above is wrong.

**Falsifier outcome: the mechanism works and the item still loses.** This is
the interesting part of H7, and the reason it is written up at length despite
not shipping on by default.

The *local* evidence was strong. Paired A/B on the profiled instances
(workstation, 120s cap):

| instance | H7 off | H7 on | `:lra.pivot-term-ops` off -> on |
|---|---|---|---|
| `latendresse/ecoliFBAyicesTest-500` | 0.8s | **0.1s** | 8.66M -> 0.13M |
| `scrambled335326` | 51.0s | **14.9s** | 195M -> 27M |
| `scrambled126657` | timeout | timeout | 492M -> 40M in the same 120s |
| `scrambled412738` | timeout | timeout | 238M -> 140M |

An 8x, a 3.4x, and an order of magnitude less pivot work on instances that
still time out. The diagnosis was right: these tableaus really are dominated
by fixed equality slacks, and eliminating them really does remove most of the
pivoting.

**It does not convert.** On the 595-file scrambled set at 60s (job 117487, 3
Primo configurations x 20 shards, z3/cvc5 reused unchanged):

| config | solved | PAR2 | 0-50KB | 50-500KB | 500KB-5MB | >5MB |
|---|---|---|---|---|---|---|
| H7 off | 373 | 29204 | 177 | **144** | 45 | 7 |
| H7 on | 374 | 29237 | 179 | 140 | 41 | **14** |
| H7 on, freezing off | 375 | 29195 | 179 | 138 | 45 | 13 |
| z3 | 429 | 23138 | 178 | 136 | **101** | 14 |

**+1 file on 595.** PAR2 flat. The churn underneath is +19/-18 -- symmetric --
and over the 139 files both configurations solve in more than a second the
**median speedup is 0.94x**, a slight net *slowdown*. Big wins (4.88x, 3.31x,
3.26x) are matched by big losses (0.18x, 0.19x, 0.23x). On the 237-file
stratified sample the same picture: 167 -> 168, PAR2 1559 -> 1532. Zero
cross-solver disagreements on either set.

There is one real signal: **the >5MB band doubles, 7 -> 14 of 50, exactly
matching z3's own 14** -- the band most dominated by equalities, which is
where the diagnosis said the effect would be. It is paid for in the 50-500KB
band (144 -> 140), and the trade nets out.

So H7 ships opt-in. **No size or row-length threshold was added** to keep the
>5MB win while dodging the mid-range cost: that is tuning a heuristic that
lost its own bar, which is exactly item 8's mistake.

**The freezing half was deleted, not left behind a flag.** It is strictly
negative in the table above -- four files lost in the 500KB-5MB band (41
against 45) for one gained elsewhere -- while carrying every bit of H7's
wrong-answer risk. 352 lines removed against 106 added.

**What this said about the remaining gap at the time.** The 56-file deficit
against z3 was *not* mostly the fixed-equality effect, even though that effect
was real and large per-instance. At the time, two candidates remained:
offset equalities (`x - y = c` as a column merge, z3's other counter,
855-68,937 per instance) and the possibility that the mid-range cost here was
the elimination pivots' own price on tableaus where fixed rows were never the
bottleneck. The offset-equality branch is now closed by the D1/D3 sweep below;
the sentence is kept as history, not as an open task.

(h8)=
### H8 -- Merge instead of sort in `pivot`'s affected-row loop

**Status: implemented, unconditional, promoted 2026-07-31.** Unlike H1-H7
this came from a flat profile rather than a design argument, so its
measurement and the profile that found it are written up in [](#sec-6-2b);
this entry is the definition the rest of the docs refer to.

**First, what "sorted" refers to.** The tableau is a `vector` of `vector`s,
and the two levels are indexed by completely different things -- which is the
single most confusing thing about the representation, so it is worth being
concrete. A real 8-variable tableau with three basic variables, dumped from
the actual structures:

```text
rows_ : indexed by variable id                        <- LEVEL 1
  rows_[0..4] = (x0..x4 are not basic, slot unused)
  rows_[5] = [ {var=x0, coeff=2,  occ_slot=0}, {var=x3, coeff=-1, occ_slot=0} ]
  rows_[6] = [ {var=x3, coeff=1,  occ_slot=1}, {var=x4, coeff=7,  occ_slot=0} ]
  rows_[7] = [ {var=x0, coeff=1,  occ_slot=1}, {var=x1, coeff=1,  occ_slot=0},
               {var=x4, coeff=-3, occ_slot=1} ]
                    ^--------------- LEVEL 2 ---------------^

occurs_in_ : also indexed by variable id
  occurs_in_[0] = [ x5, x7 ]      occurs_in_[3] = [ x5, x6 ]
  occurs_in_[1] = [ x7 ]          occurs_in_[4] = [ x6, x7 ]
```

**Level 1** -- `vars_`, `rows_`, `occurs_in_` -- is dense, one slot per
variable, and the index *is* the variable: `rows_[5]` means "x5's row". No
ordering question arises here.

**Level 2** -- the `Row` inside one of those slots -- is **not** indexed by
variable. It is a sparse list holding one entry per *non-zero term*, each
entry labelled with the variable it concerns:

```cpp
struct RowEntry {
  LraVarId var;         // which variable this term is for
  util::Rational coeff;
  std::uint32_t occ_slot;  // this row's index inside occurs_in_[var]
};
using Row = std::vector<RowEntry>;
```

`rows_[7][0]` is therefore not "x0's coefficient in row 7"; it is "the first
stored term of row 7", and only reading its `.var` reveals it concerns x0. The
row's length is its non-zero count (2, 2, 3 above), never the variable count.
Its `occ_slot=1` says that this row's basic variable, x7, is stored at index 1
of `occurs_in_[0]`: `occurs_in_[0][rows_[7][0].occ_slot] == x7`. The slot is a
back-index into that column list, not an index into `rows_[7]`; see
[](tableau-sparse-representation) for the complete invariant and maintenance
rules.

Mathematically `rows_[5]` is the vector `(2, 0, 0, -1, 0, 0, 0, 0)`. Dense
storage would be 8 slots indexed by variable; sparse storage is 2 entries
carrying their own labels. The numbers force the sparse form: on
`constraint-199552` the tableau has 1,440 rows over ~1,900 variables holding
only 3,519 non-zeros, an average row length of **2.44**
([](tropical-matrix-structure.md)). Dense rows would be ~2.7M slots for 3,519
real values, and every row operation would cost O(variables) rather than
O(non-zeros).

So **"sorted" is a statement about level 2 only**: within a single row, the
entries are kept in strictly ascending order of their `.var` field (and never
carry a zero coefficient) -- x0, x1, x4 in `rows_[7]`. It is an ordering over
a row's *contents*, which only exists because those contents are a sparse
labelled list instead of a dense index.

Sortedness is also decided per container, not globally: `occurs_in_`'s inner
lists have the same two-level shape but are deliberately **not** ordered,
since removal there is a swap-pop that destroys any order (see
[](open-leads.md) lead 3b).

**What H8 is.** `pivot()` combines each affected row with the scaled entering
row -- and because both are sparse lists, combining them means pairing up the
entries that refer to the same variable, which a dense representation would
get for free from the index. It used to concatenate the two into a scratch
buffer and `std::sort` the result, once per affected row per pivot. But both
halves already arrive ordered by variable id, so sorting re-establishes
something that was never lost: the correct operation is a linear two-way
*merge*, walking both lists in step and adding coefficients where the var ids
coincide.

The change makes that ordering an explicit invariant on `Row`, which
`add_row()` establishes by sorting and `pivot()` preserves by splicing
`basic_var` into `new_entering_row` at its ordered position. Two smaller wins
follow: the zero check is needed only in the equal-var-id branch, and the
affected row is taken by reference and *moved* into the trail rather than
copied twice.

Sortedness is a *choice*, not a necessity, and the alternative is a real
design: King's tableau (thesis §2.2.7, see [](papers/summaries.md)) keeps rows
as explicitly **unsorted** linked lists and pays for a dense O(n) scratch map
per row addition instead of a merge. Both reach O(‖Ti‖ + ‖Tk‖); Primo's
version buys cache-friendly contiguous rows and pays by having to maintain the
order.

**Result.** Scrambled competition set (595 files, 60s) 369 -> 378 solved with
churn **+9/-0**; stratified sample 189 -> 192, +3/-0; mid-band gap 37 -> 38,
+1/-0. Median paired speedup 1.16-1.23x with a tight p25-p75 (1.10-1.33), so
a uniform constant factor rather than a few instances carrying an average.
**Zero regressions on any bar**, which no other item here has managed.

**Why it matters beyond its own number.** The `Row` ordering invariant it
established is what later made bounds-only backtracking's merge cheap, and
it is the precondition for the delta-maintained `occurs_in_` that
[](#sec-6-2c) identifies as the largest item left.

(h9)=
### H9 -- Bounds-only backtracking

**Status: implemented, unconditional except under SOI, promoted 2026-07-31.**
Strictly this is not a heuristic but a return to the paper's own design,
found by re-reading the technical report line by line against the code; it
is numbered here so the rest of the docs have something to point at. Full
write-up in [](papers/fast-linear-arithmetic-dpllt.md)'s deviations section.

**What it is.** `pop()` restores bounds and nothing else. It used to restore
bounds, assignments *and* every pivot's row rewrite. The paper (technical
report SRI-CSL-06-01 Section 3.2.4) trails only bounds: the assignment
satisfies `Ax = 0` independently of the basis, and `pop()` only ever
*relaxes* bounds, so neither the assignment nor the basis needs restoring.
Primo's contrary justification was circular -- reverting the assignments is
what forced reverting the pivots. `PivotingRule::Soi` keeps the old scheme,
since its objective row is a sum over the currently-violated rows rather
than a linear combination of `A`.

**Result.** Scrambled competition set **378 -> 399 solved**, PAR2 28691 ->
26661; stratified 192 -> 197. The mechanism is that not undoing pivots lets
the search keep the repair work it already paid for: pivot counts fall
2.3-5.3x on the profiled instances.

**It is not regression-free, unlike [](#h8).** Churn is +32/-11, and among
the 145 commonly-solved files over a second, 62 are more than 10% *slower*
against 64 more than 10% faster -- median 1.01x, tails 0.10x to 24.9x. It
nets +21 because the wins land on instances near the timeout. The variance
is inherent: keeping the basis across a backtrack keeps one that suited the
*previous* subproblem.

(h10)=
### H10 -- Delta-maintained `occurs_in_`

**Status: implemented, unconditional, promoted 2026-08-01.** The item
[](#sec-6-2c) identified as the largest remaining engineering target, and
the last of the three that started from a profile rather than a design.

**The reasoning, which is the whole of it.** `occurs_in_[v]` records *which
basic rows mention `v`* -- a set of row identities, with **no coefficient
information whatsoever**. `pivot()`'s affected-row loop used to remove
`other_basic` from every term's list up front and then re-add it for every
term of the rebuilt row. For any variable present both before and after,
that round trip removes an entry and puts back the identical entry. It is a
no-op performed 40.1 million times, scanning 37.6 billion vector elements
(see [](#sec-6-2c) for the measurement).

Enumerate what a pivot actually does to one affected row, using the fact
that both inputs are ordered by var id ([H8](#h8)'s invariant), so the merge
already classifies every term into exactly one of four cases:

| case | row membership | `occurs_in_` |
|---|---|---|
| in `other_row` only | stays, coefficient unchanged | **unchanged** |
| in both, sum non-zero | stays, coefficient changed | **unchanged** -- no coefficients are recorded |
| in both, sum cancels to zero | leaves the row | **remove** |
| in `new_entering_row` only | newly enters the row | **add** (`push_back`, no scan) |
| `entering_var` | leaves the row | **nothing** -- see below |

So the only scan-bearing update left is cancellation, which is rare.
`entering_var` needs no per-row removal because `pivot()` clears
`occurs_in_[entering_var]` wholesale when it becomes basic -- it is basic
afterwards and so belongs to no row's term list at all.

**Result on the bars** (jobs 118218/118219, base `4f9b741` verified by
content as well as banner): scrambled competition set **398 -> 439 solved**,
PAR2 26723 -> 22207; stratified sample 197 -> 202. **Churn +41/-0 and
+5/-0** -- zero regressions on either, and only 2 of the 173
commonly-solved files over a second are more than 10% slower. Median paired
speedup 1.51x (p25 1.15, p75 2.26, max 8.0x).

**439 against z3's 429 on the same 595 files**, from 56 behind on
2026-07-29. Absolute counts drift a few files between jobs -- this run's own
baseline solved 398 where [H9](#h9)'s recorded 399 -- so the within-job
+41/-0 is the trustworthy figure and the z3 comparison is "ahead, by
roughly ten".

**Local measurement.** Paired interleaved A/B, two reps each, answers identical:
`scrambled228965` 4.85x/4.50x, `scrambled259571` 2.85x/3.00x,
`scrambled365036` 2.23x/2.35x, `scrambled339450` 1.15x/1.22x. gprof on
`scrambled228965` shows `pivot`'s self time falling **10.04s -> 1.26s** at
an identical 3,460 calls -- 90.7% of the profile down to 49.6% -- and what
remains is arithmetic (`Rational::operator*` 6.7%, `update` 4.3%,
`operator<` 3.9%) rather than bookkeeping.

**Correctness.** The risk here is not a crash but a silently wrong index:
`update()` would skip a dependent row, or sparse pricing misjudge a column,
and the answer would be wrong much later and elsewhere.
`occurs_in_index_matches_full_rebuild()` compares the maintained index
against a rebuild, and two tests call it after *every* pivot across all four
pivoting rules plus a dedicated cancellation case. Those tests were written
before the working implementation and caught the first attempt (below).

*A note on the first attempt, since the docs briefly recorded the wrong
diagnosis.* It was reverted on 2026-07-31 after producing duplicate
`occurs_in_` entries, and the failure was written up as an unlocated defect
"surviving a cancellation-adjacent path". The real cause was duller: the
patch that was supposed to rewrite the cancellation branch **silently did
not apply** -- a multi-line pattern that no longer matched after
clang-format reflowed it -- so that branch kept the old round-trip
behaviour, adding entries the new code no longer removed. The algorithm was
right the first time; the edit was not. The lesson is about verifying that
an edit landed, not about the tableau.

(h4)=
### H4 -- Batched bound assertion (deferred `update()`)

**Targets:** Class C (DTP-Scheduling, tta_startup, TM/sc/uart).

**Problem.** Every asserted bound on a nonbasic variable immediately calls
`update()`, which walks `occurs_in_[var]` and rewrites every dependent
basic variable's assignment -- up to 2,127 rows for one variable on
`constraints-tempo-width-30`. CaDiCaL delivers assignments in *batches*
(`notify_assignment(const std::vector<int>&)`), and roadmap item 6 already
established that the repair (`check()`) can be deferred to
`cb_has_external_clause`. The assignment *move* can be deferred the same
way, and for the same verified reason.

**Design.** Split `assert_upper`/`assert_lower` into (a) the eager,
O(1) part -- record the bound, detect a direct
`lower > upper` contradiction, push the trail entry -- and (b) the deferred
part, the nonbasic assignment move. Queue the pending moves; at the
deferred-check point, coalesce them per variable (a variable bounded twice
in one batch moves once) and apply them in a **single sweep** over the
union of affected rows, so each affected row's assignment is recomputed
once per batch instead of once per assertion.

**Risk: moderate.** It changes when the "assignment satisfies all nonbasic
bounds" invariant holds, which is the invariant `check()`, propagation, and
`build_conflict` all rest on. The deferral point must be *exactly* item 6's
(`run_deferred_check_if_needed`), and the eager direct-conflict branch must
keep clearing the pending queue the same way item 6's fix had to clear
`needs_check_` -- that bug is the direct precedent for what goes wrong
here. Same-slack/row-based propagation read *bounds*, not assignments, so
they are unaffected (item 6 already verified that property).

(h5)=
### H5 -- Model-based phase selection (replacing item 8's variable override)

**Targets:** the TM/sc/uart cluster, without item 8's regressions
elsewhere.

**Problem.** Item 8's `cb_decide` round-robin was the roadmap's most
dramatic narrow win (7/9 files 40-97% faster, `sc-27` newly solved) and a
net *loss* on the 237-file sample (108 → 106, 4 regressions). The reason is
structural: it overrides **which variable** CaDiCaL decides, discarding
VSIDS's conflict-driven focus. That is a good trade only when conflicts are
rare and the formula is a chain of similar guarded atoms -- i.e. exactly
TM/sc/uart, and exactly not LassoRanker/sal/tta_startup.

**Design.** Keep VSIDS's *variable* choice; supply only the *polarity*.
For an atom bounding variable `v` at threshold `c`, the tableau's current
assignment β already tells us whether the atom holds under the current
model. Choosing the polarity that β satisfies means the resulting
`assert_upper`/`assert_lower` needs **no assignment move and no pivot at
all** -- the bound is already satisfied. That is a genuine
"free decision", and unlike item 8's round-robin it is a hint, not an
override: VSIDS keeps its ordering, so a family whose search needs VSIDS
focus loses nothing.

Mechanism: the IPASIR-UP backend's `phase(lit)` hook, exposed through
`sat::IpasirUpBackend::phase(Lit)`.
Implemented behind `--lra-model-phase` in `OnlineLraPropagator`: it stores a
reference to the SAT backend, keeps the last phase literal per observed
arithmetic atom, and refreshes phase literals at the same quiescence point
item 6 uses -- not per assignment. Refreshes are dirty-variable scoped after
ordinary non-basic Assert-Bound moves (the moved variable plus basic rows
that mention it), and fall back to a full scan after a successful
`LraTableau::check()` performs pivots, since pivots may change many β values
at once. They also fall back to a full scan after backtracking in the two
modes whose pop can restore pivot/assignment state (`PivotingRule::Soi` and
`--lra-fixed-elimination`); ordinary bounds-only backtracking does not change
β and so does not dirty phases by itself. The truth test is over
`LraTableau::assignment(bound_var)` and the
same `DeltaRational` `bound_assertion()` encoding used by assertion and
propagation, so strict inequalities are handled in the symbolic-delta
ordering rather than by rounding.

**Interpretation to test:** the hypothesis is that item 8's win came mostly
from "decide atoms that need no theory work", and only incidentally from
the always-TRUE polarity happening to fit TM/sc/uart's guard structure. If
so, β-based phases should reproduce most of the 9-file win *and* be neutral
on the 4 files item 8 regressed. If instead the win came from the stable
*order*, this will show up as a much smaller 9-file effect -- a clean,
informative negative result either way.

**Prior art located 2026-08-29, after this design was written.** This is
SPASS-SATT's *decision recommendation*
([](papers/bromberger-linear-arithmetic-thesis.md) §8.1, credited there to
Yices), and it is measured: QF_LRA +11, QF_LIA **+129, of which 116 are
`convert`**. Their rule has two criteria and H5 implements only the second:
prefer a bound that **cannot cause a conflict if asserted** — described as
always possible — and, among those, one the current assignment satisfies. The
missing criterion covers exactly the case H5 goes quiet on, where β satisfies
neither polarity. Registered as
[lead 20](lead-decision-recommendation).

(h5-attribution)=
#### What to cite for `--lra-model-phase`

Traced 2026-08-29, because the obvious answer is wrong and the chain is worth
recording once rather than re-deriving.

**Cite, for the technique:** Martin Bromberger, Mathias Fleury, Simon Schwarz,
Christoph Weidenbach. *SPASS-SATT: A CDCL(LA) Solver.* CADE 2019, LNCS 11716.
DOI
[10.1007/978-3-030-29436-6_7](https://doi.org/10.1007/978-3-030-29436-6_7);
open deposit <https://inria.hal.science/hal-02405524>. For the fuller treatment,
[](papers/bromberger-linear-arithmetic-thesis.md) §8.1 — the thesis is what was
actually read here; see the caveat at the end.

**Do not cite Dutertre, *Yices 2.2*, CAV 2014 for it.** Bromberger writes "(The
SMT solver Yices also uses decision recommendations [56].)" with [56] = that
paper. The paper was fetched and read
(<https://yices.csl.sri.com/papers/cav2014.pdf>, 6 pages): it describes Yices'
architecture and **contains no mention of decision recommendations, phase
selection, or theory-supplied polarity**. The citation identifies the *solver*,
not a description of the technique. Passing it on as the source would be citing
a paper for something it does not say. The correct form is the one Bromberger's
own wording supports: attribute the origin to Yices, cite Bromberger et al. for
the description.

**This is a documented gap, not an oversight.** The SPASS-SATT v1.1 SMT-COMP
2019 system description
(<https://smt-comp.github.io/2019/system-descriptions/SPASS-SATT.pdf>, read in
full) lists decision recommendations among techniques that "are contained in
existing SMT solvers" but where "not all have been described in the respective
literature", and names the CADE-27 paper as the remedy. So Bromberger et al. is
the first written description, by their own account.

**Position it against:** Sebastiani, *Lazy Satisfiability Modulo Theories*,
JSAT 3(3-4):141-224, 2007, **§7.2.5 "DPLL branching heuristics for SMT"** --
a paper this project already cites for §6.10. §7.2.5 is the survey statement
that this was open:

> "So far there seem to be no really-satisfactory proposal in the direction of
> building theory-aware heuristics, and most tools simply use standard DPLL
> heuristics."

It then points at *look-ahead* and *privileged branching variables* -- neither
is phase selection, so §7.2.5 is prior context, not prior art. DOI
[10.3233/SAT190034](https://doi.org/10.3233/SAT190034) (resolves 302 to the
publisher, whose landing page returns 403 to an automated fetch); author copy
<http://disi.unitn.it/rseba/papers/JSAT-2007.pdf>, which is the one read.

**Checked and found *not* to contain it**, recorded so nobody re-checks:
Dutertre and de Moura's SRI technical report CSL-06-01
(`docs/papers/qflra-sri-csl-06-01.pdf`) and Griggio's thesis
(`docs/papers/griggio.pdf`, [](papers/griggio-mathsat-thesis.md)). Neither
mentions phase, polarity, or a theory-driven decision heuristic anywhere.

**For the SAT-side mechanism** H5 rides on, if a paper needs it: phase saving,
Pipatsrisawat and Darwiche, *A Lightweight Component Caching Scheme for
Satisfiability Solvers*, SAT 2007, DOI
[10.1007/978-3-540-72788-0_28](https://doi.org/10.1007/978-3-540-72788-0_28).
H5 *overrides* that saved phase for arithmetic atoms; it does not implement it.

**How to phrase Primo's own claim.** H5 was designed here on 2026-08-04 from the
tableau argument -- "the polarity β satisfies needs no assignment move and no
pivot" -- and this literature was read on 2026-08-29. So it is an independent
rediscovery of *half* of a published technique. The honest form is to say that
and cite Bromberger et al. as the closest prior description. Claiming novelty
for the mechanism would be wrong; claiming to implement decision recommendation
would also be wrong, because criterion 1 is absent (see
[lead 20](lead-decision-recommendation)).

**The CADE 2019 citation is now verified, and it changes the claim.** The paper
was supplied by the user and read on 2026-08-29
(`docs/papers/SPASS-SATT.pdf`, untracked -- copyrighted, and `.gitignore`
excludes `*.pdf`). §3 does describe decision recommendations, so the citation
above stands without a caveat. It is also the better citation than the thesis
here, because its **footnote 3 gives the rule exactly**, where the thesis gives
only prose:

> "In our own theory solver, `GetPhase(L) = ⊤` if the current theory assignment
> satisfies `L` or if `Assert(¬L)` would return `⊥`. Otherwise,
> `GetPhase(L) = ⊥`. (If unate propagations are enabled, then the case that
> `Assert(¬L)` would return `⊥` is impossible because `L` would have been unate
> propagated.)"

Two corrections to what this note said before reading it.

**H5 is not "half" of the technique — under their default configuration it is
effectively all of it.** The thesis prose reads as two criteria, conflict-freedom
and model-satisfaction, and this note previously called the first "the stronger
half" that Primo lacks. The footnote says the opposite: the conflict-freedom
disjunct is *unreachable* when unate propagation is enabled, which it is by
default in SPASS-SATT. **Their +129 was therefore measured with only the
model-satisfaction disjunct doing work — which is exactly what H5 implements.**

That makes +129 QF_LIA / +116 `convert` an estimate of what **H5 itself** is
worth, not of what H5 is missing. H5 is off by default and has only ever been
evaluated on QF_LRA, where the same source measures the technique at +11.
`solve_qf_lia_impl` already constructs `OnlineLraPropagator` with
`options.lra_model_phase`, so this is measurable today with no new code. See
[lead 20](lead-decision-recommendation).

**The conflict-freedom disjunct is dead for us too** -- corrected 2026-08-29,
having first got this backwards. `grep -rn unate src/` is empty, and this note
briefly concluded from that that Primo lacks unate propagation and so the
disjunct could still fire here. **That was a naming false-negative.** Primo's
*same-variable bound propagation* (`--lra-propagation`, **default on**;
[](arithmetic.md)) is the same rule under Dutertre and de Moura's other name for
it: after each bound assertion it scans the remaining unassigned atoms on that
variable and propagates the ones the new bound now entails true or false, with
the single asserting literal as the reason -- a 2-literal clause, exactly
Bromberger's unate propagation clause. So footnote 3's argument transfers intact
and there is no leftover: **H5 is the whole of decision recommendation for Primo
as well.**

**One design difference worth keeping in view.** Their `GetPhase(L)` is *pulled*
by the SAT solver at the moment it decides `L`, so it is always current. H5
*pushes* hints via `IpasirUpBackend::phase()` at quiescence points, so a hint can
be stale between refreshes — which is what all the dirty-scoping machinery above
exists to bound. If an H5 evaluation underperforms the published numbers,
staleness is the first thing to suspect, and CaDiCaL's `cb_decide` would allow
the pull form.

**Risk: low.** Phase hints cannot affect soundness; the worst case is a
worse search order. Keep `--lra-theory-decisions` as-is (it stays a useful
lever for its file shape) and keep this as a separate opt-in flag unless the
tropical/sc/uart target set and a broad-regression bar both clear. The
implementation is observable through `:lra.model-phase.refreshes` and
`:lra.model-phase.hints`.

**First local measurement (2026-08-04, diagnostic only).** The focused
manifest `benchmarks/qf-lra-h5-evaluation.txt` has three slices: the old
item-8 TM/sc/uart dramatic-gap rows, rows verified locally where default
Primo is slower than both OpenSMT and Yices (`sc-21`, `uart-25`,
`tta_startup/simple_startup_12nodes.missing` under a 1.5x/50ms rule), and
one tropical sentinel. At 10s on the workstation, default and
`--lra-model-phase` both solve 12/13 with the tropical sentinel timing out;
PAR2 moves 24.08s to 23.36s. The direction is mixed: H5 helps the sc rows
and the added `tta_startup` reference-slower row, but hurts two added uart
rows (`uart-15`, `uart-25`). This is enough to keep the opt-in experiment,
not enough to promote it.

#### Tropical-matrix polarity follow-up, 2026-08-04

The OpenSMT comparison makes `tropical-matrix` the open family in
[](arithmetic.md): OpenSMT solves 5/10 at 300s where Primo, Yices and z3 each
solve 1/10. A local follow-up on branch `qflra-tropical-investigation`
checked whether the earlier "all solvers struggle" reading was hiding a cheap
phase heuristic.

The family shape is unusual but regular: each file is one huge assertion,
contains thousands of ORs/equalities, no ITEs, and pairs Boolean markers
`fNm` with Real costs `fNc`. On the smallest status-sat file,
`constraint-199552.smt2`, OpenSMT with `:verbosity 1` solves in 29.39s after
about 56k conflicts; Yices times out at 90s. OpenSMT's public help does not
advertise detailed LRA counters, and `:produce-stats`/`:stats-out` produced no
useful final stats file in this build, so the behavioral signal is only the
search trace: many conflicts, low memory, no evidence of an instant
preprocessing shortcut.

A follow-up arithmetic-shape census shows that the family is close to, but not
inside, difference logic. Across the 10 files, about 70-72% of arithmetic atoms
are unary or two-variable bounds/equalities such as `l3c <= l0c`,
`l105c >= l258c`, or `f0c >= 0`. The systematic non-difference-logic residue is
almost entirely the tropical multiplication/cost-composition encoding:
definitions such as `l1c = f13c + f15c` and `l0c = f12c + f12c`, which become
three-variable or non-unit-coefficient rows after normalization. A pure RDL
solver therefore cannot take the whole instance. The exploitable shape would
need to be a hybrid: either recognize these `out = a + b` cost definitions as a
separate acyclic layer, or feed only the guarded min/comparison subgraph to a
difference-logic engine while the sum definitions remain in LRA.

Primo's own option matrix on the same file is more informative. Default,
negative phase, model phase, row+bidirectional propagation at 1024 caps,
fixed-elimination, top-level-OR Tseitin, and disabling sparse leaving all ran
past a 45s diagnostic cap. `--theory-phase positive` solved `sat` in about
19-26s. The solved run still spent real time in the tableau -- 40,504 pivots,
5.88M row updates, 478M pivot term ops -- but it found the satisfying branch.
The default/negative 45s partial runs accumulated roughly 1.36-1.38B pivot
term ops without solving. In other words: pivot cost is still the local
expense, but polarity decides whether we burn that cost on the useful part of
the search.

A 60s local two-solver probe over all 10 tropical files is diagnostic only
(not SLURM), but it supports the same lead:

| solver/config | solved / 10 at 60s | solved files |
|---|---:|---|
| Primo `--theory-phase positive` | 2 | `constraint-199552`, `constraint-223827` |
| OpenSMT `:verbosity 1` | 2 | `constraint-199552`, `constraint-223827` |

This does **not** promote positive phase globally. It says the next cheap
tropical-specific experiment should be an auto phase rule for this shape:
single huge assertion, many top-level ORs, many positive theory atoms, no ITEs,
and `:status sat` on the known solved cases. The bar is a SLURM A/B over all
10 tropical files, with the full QF_LRA division checked for regressions
before any default change. Offset equalities remain closed by D1/D3; they are
not the tropical lead from this probe.

(t1)=
### T1 -- Guarded Real equality distribution for tropical cost definitions

**Status: implemented behind `--guarded-real-equality-lowering`; local
tropical evidence is mixed.** The rewrite is deliberately not a portfolio
branch and not a second difference-logic propagator. It stays inside the
existing preprocessing pipeline and keeps LRA as the only arithmetic checker.

The common tropical shape is a guarded equality:

```text
(or G (= out (+ a b)))
```

The old Real-equality lowering first produced:

```text
(or G (and (<= out (+ a b)) (<= (+ a b) out)))
```

Boolean clausification then had to represent the inner conjunction explicitly.
The new lowering distributes the single equality under the existing guard:

```text
(and (or G (<= out (+ a b)))
     (or G (<= (+ a b) out)))
```

This is just the Boolean identity `G or (A and B) = (G or A) and (G or B)`,
restricted to disjunctions with exactly one Real equality child. Disjunctions
with multiple Real equality children still use the old non-distributed lowering
to avoid an exponential CNF expansion. The counter
`:preprocessing.real-equality-lowering.guarded-distributed` records the
specialized case.

**Focused local result (2026-08-04, branch
`qflra-tropical-investigation`).** With this option enabled on
`constraint-199552.smt2`, Primo solved `sat` in 5.3-5.8s at a 60s cap. The run
distributed 648 guarded equalities, encoded 5,460 SAT variables and 10,059
clauses, and spent 7,679 pivots / 128M pivot term ops. That is a clear
improvement over the earlier same-branch default probe, which timed out at 45s
after about 1.36B pivot term ops, and over the earlier `--theory-phase
positive` run on the old encoding (about 19.8s, 6,108 vars, 11,355 clauses,
40,504 pivots, 478M term ops).

The 10-file tropical probe is not a win yet:

| config | solved / 10 at 60s | note |
|---|---:|---|
| Primo with `--guarded-real-equality-lowering` | 1 | `constraint-199552` only |
| Primo `--theory-phase positive --guarded-real-equality-lowering` | 1 | `constraint-199552` only; `constraint-223827` regressed from the earlier positive-phase diagnostic |

Interpretation: T1 removes real Boolean/SAT overhead for exactly the guarded
cost definitions present in this family, and can materially change search on a
single instance. It does not solve the tropical family, and the mixed local
evidence is why it remains an option rather than a default. Before using it as
performance evidence, run a SLURM A/B over all 10 tropical files and the full
QF_LRA division for regressions.

#### T2 -- Row propagation on top of T1: rejected, 2026-08-04

The obvious follow-up, and the one this branch was opened for: T1 leaves a
leaner CNF (5,460 vars / 10,059 clauses against the pre-T1 6,108 / 11,355), and
the family's census is 70-72% unary/binary atoms whose residue is exactly the
`out = a + b` cost rows that `compute_implied_child_bound` is built to exploit.
Row propagation had only ever been tried on the pre-T1 encoding. **Measured on
all 10 files at 60s, it rejects at every cap setting.**

| config | solved / 10 at 60s |
|---|---:|
| `--guarded-real-equality-lowering` (baseline) | **1** |
| + row/bidirectional propagation, default caps | **0** |
| + both at 128 | **0** |
| + both at 1024 | **0** |

Not neutral -- a regression. Every propagation configuration *loses*
`constraint-199552`, the one file the baseline solves.

**The experiment is well-controlled, and the counters explain it.** On
`constraint-199552` at a short diagnostic cap:

| config | pivots | pivot term ops | row props | bidi props |
|---|---:|---:|---:|---:|
| T1 only | **7,679** | **128M** | -- | -- |
| + propagation, default caps | 64,272 | 1,013M | 2,088 | 128 |
| + propagation at 128 | 64,272 | 1,013M | 2,088 | 128 |
| + propagation at 1024 | 64,272 | 1,013M | 2,088 | 128 |

Two things follow. First, the mechanism definitely ran -- 2,088 row
propagations, so this is not a null result from code that never fired. Second,
and decisively, **the counters are identical across all three cap settings**:
unlike the mid-band files that motivated D1 (fanout reaching 521, rows of 91
terms), tropical's fanout and row sizes never approach even the default caps,
so raising them changes nothing. The caps were never the binding constraint
here. What propagation does instead is *hurt*: 8.4x the pivots and 7.9x the
term ops against T1 alone, perturbing the search away from the cheap branch T1
finds.

No SLURM A/B and no fuzz gate were run, because nothing is being promoted; this
is a local diagnostic recorded as such in `docs/run-log.json`.

**Where this leaves the family.** T2's rejection and the independent structural
result in [](tropical-matrix-structure.md) bound the same conclusion from
opposite sides. That note reads the actual encoding -- a tropical/arctic
semiring where each quantity is a paired Boolean `-infinity` flag and Real value
-- and shows by measurement that *removing* the guards is 27x slower, because
the guard is what lets the solver skip the arithmetic entirely. So the encoding
is already close to the right shape for a lazy solver: it needs neither more
propagation (T2) nor fewer guards. The residual cost is structural fill-in
(rows going from 2.44 to ~102 terms, ~42x), with `:lra.fixed-variables` and
`:lra.elimination-pivots` both zero. The two live leads are recorded there --
`m`-flag-first decision ordering, and relevancy propagation -- and neither is
a pivoting-rule or propagation-cap question.

(h6)=
### H6 -- A difference-logic fast path

**Targets:** DTP-Scheduling (4 gap files, avg row exactly 2.00, max row 2 --
**100% difference constraints**), clock_synchro (4), tta_startup (5).

**Problem.** `constraints-tempo-width-30` is a QF_RDL problem wearing a
QF_LRA label: 3,932 rows, every one of the form `x - y`. Simplex on it
needs 60,750 decisions and 19.5s where z3 takes 0.71s -- and Section 1.3
shows its rows densifying from length 2 to length 33 with one variable
reaching 2,127 occurrences. Fill-in on a problem that has *no* inherent
fill-in is a representation failure, not a tuning problem.

**Design, two tiers.**

- **Tier 1 (cheap, and useful regardless):** detect the fragment at
  `PreparedLraTheory` construction -- every canonical polynomial has
  exactly two terms with coefficients `+1`/`-1` -- and expose it as a stat.
  This alone tells the evaluation which files H6 could ever help, and
  H1/H3 are likely to recover a large part of the gap without any new
  solver. Note H3's fill-in-aware rule is *specifically* well-suited here:
  a difference tableau has an ordering that keeps rows short.
- **Tier 2 (milestone-sized, out of scope for this note's first pass):** a
  real incremental negative-cycle solver for the fragment. The plan already
  exists as DL0-DL4 in [](papers/difference-constraint-propagation.md)
  (Cotton & Maler, "Fast and Flexible Difference Constraint Propagation
  for DPLL(T)", SAT 2006), and `src/theory/arithmetic/difference_logic.hpp`
  is already reserved for it. Its explanations (the edges of the negative
  cycle) are minimal by construction, which is exactly what the DPLL(T)
  layer wants.

**Risk:** Tier 1 is trivial and safe. Tier 2 is a new theory solver with
its own correctness surface and should not be started until H1-H3 have been
measured, since they may move these families on their own.

## 3. What is deliberately *not* proposed

- **More pricing rules for their own sake** (Devex, steepest edge). The
  roadmap already found neither Dantzig nor a full SOI implementation moved
  the narrow sample, and Section 1.2 says the per-pivot *cost*, not the
  pivot *count*, is what dominates. Devex approximates steepest-edge
  pricing to reduce iteration counts -- the wrong axis for this profile.
  H3 attacks the axis the measurements point at.
- **A full implied-equality basis** (Bromberger/Weidenbach). Already
  considered and set aside during item 7 as paper-sized with no measured
  SMT speedup; nothing in this profile changes that.
  **Still true for pure QF_LRA, 2026-08-29.** Having now read the source
  ([](papers/bromberger-linear-arithmetic-thesis.md) Chapter 5), the "no
  measured SMT speedup" judgement holds -- the thesis reports no QF_LRA
  solved-count experiment for it either. What has changed is that it is no
  longer a *pure-QF_LRA* question: it is the completeness mechanism for
  theory combination ([](qf-lra-equality-sharing.md)) and the applicability
  test for the Chapter 6 bounding transformations. See
  [lead 17](lead-equality-basis); it should be judged on
  probe counts in the mixed path, not on this profile.
- **Anything gated on a per-family structural classifier.** Item 8's
  outcome is the argument: a heuristic that must be told which family it is
  running on is a portfolio entry, not a default.

## 4. Suggested order of work, and the evaluation each item needs

### 4.0 Re-ranking after H2 and H1 (2026-07-28)

The original ranking below was written against the Section 1.2 profile, in
which `LraTableau::pivot` was 31.4% of runtime and the allocator 42%. Both
H2 and H1 have since landed, so that profile no longer describes the solver.
Re-profiling `DTP-Scheduling/constraints-tempo-width-30` at the current head:

```
75.8%  LraTableau::pivot          (was 31.4%)
 5.0%  allocator                  (was 42.0%)
 ~8%   util::Rational operators and construction
```

**H3 is now unambiguously the next item, and by a wider margin than the
original ranking gave it.** Three independent lines of evidence converge:

1. `pivot()` went from a third of runtime to three quarters. H2 and H1
   removed the costs *around* it; nothing removed fill-in.
2. H1's own falsifier fired on the Class A instance:
   `latendresse/ecoliFBAyicesTest-500` lost 69% of its rows and did not speed
   up at all, so for the one family that is pure LP, row count was never the
   cost. H3 is the only remaining lever there -- and latendresse is still
   0/15 while z3 solves 6.
3. H1b's DTP-Scheduling regression is a fill-in effect: merging proportional
   rows cut decisions 8.6-fold and pivots to 4,248, yet wall time rose,
   because the surviving rows are denser. A cost-aware entering-variable rule
   is what that asks for.

The rest of the order is unchanged (**H5 → H4 → H6**), with one revision:
H4's premise was "~320 µs per decision" on DTP-Scheduling, measured when
that instance took 19.5s. It now takes ~6-8s with far fewer decisions, so
H4's expected value has dropped and it should be re-measured before being
picked up rather than implemented on the old number.

### 4.1 Original ranking

Recommended sequence: **H2 → H1 → H3 → H5 → H4 → H6 (Tier 1)**.

H2 first because it is the largest measured share, benefits every other
item, and is a self-contained change to one well-encapsulated type. H1
next because it multiplies with H2 (fewer rows × cheaper rationals) and
subsumes roadmap item 3. H3 after both, since its benefit is measured
relative to a pivot cost that H2 will have already changed -- measuring it
first would attribute H2's win to it.

Per the project's standing discipline, each item needs:

1. unit tests for the specific invariant it introduces (not only generic
   correctness examples);
2. `make check` plus the ASAN/UBSan suite;
3. `:lra.pivots` / `:lra.online-check-calls` / `:sat.decisions` /
   `:sat.conflicts` reported alongside wall time, so a "cheaper but more of
   them" outcome is visible rather than hidden;
4. a Slurm merge-gate fuzz run (500 cases) -- and, for H1/H2/H4 (which
   change unconditional core behavior), a campaign-gate run (3000 fuzzsmt
   cases plus opfuzz/yinyang mutation) on **both** the Bland and SOI paths,
   matching the precedent set by the `occurs_in_` and `violated_vars_`
   fixes;
5. **the 237-file stratified sample as the promotion bar**, diffed against
   the existing unchanged `z3.csv`/`cvc5.csv` from job 114200 (per the
   standing practice of not re-running unchanged reference solvers), and
   rendered with `scripts/render_solver_compare_html.py`;
6. a `docs/run-log.json` entry before the result is cited anywhere.

New stats worth adding alongside the implementation, since Section 1.3 had
to be produced with throwaway instrumentation: `:lra.rows`,
`:lra.tableau-vars`, `:lra.row-nonzeros` (post-preparation), and a
periodic or final fill-in figure. Their absence is why the fill-in
explosion went unnoticed through eight roadmap items.

## 5. Evidence trail

The **design** measurements in Sections 1.1-1.3 are local (workstation,
Release build, commit `35878c4`), recorded in `docs/run-log.json` under
`2026-07-27-qf-lra-heuristics-design-profiling`. They were profiling evidence
for a design decision, not benchmark claims.

Every **result** claimed for an implemented item comes from Slurm, per
AGENTS.md, and has its own run-log entry:

| item | implementation | gates | promotion-bar run |
|---|---|---|---|
| H2 | `02e2d47` | 114896-114899 | job 114900 |
| H1a | `077beec` | 115168-115171 | job 115172 |
| H1b | `1e6554d` | 116545-116548 | job 117222 (`bland` column) |
| H3 | `cf491ce`, promoted `dd60655` | 117219-117221, 117227-117228 | job 117222 |

Plus a milestone-wide campaign sweep (jobs 117248-117252) over every other
reachable configuration -- `bland`, `dantzig`, `--no-lra-sparse-leaving`,
`--no-lra-propagation`, and row/bidirectional propagation -- so that no
supported option combination is left un-fuzzed at campaign tier.

Local-only evidence that remains local, and is labelled as such where it
appears: the paired A/B timings, the tableau-shape row counts, and the
32-bit-vs-64-bit width ablation.

(sec-6)=
## 6. Where the gap is after H7 (2026-07-30)

Written immediately after the H7 result, while the measurements are fresh.
**Nothing here is implemented**; it is the diagnosis the next item should be
designed against, and it deliberately records what has been *ruled out* as
well as what is suspected.

### 6.1 The gap is one band, and it is z3-specific

Of the 76 files z3 solves and Primo does not (595-file scrambled set, 60s),
**59 are in the 500KB-5MB band** -- 8 in 50-500KB, 7 above 5MB, 2 below 50KB.
Two things sharpen that:

- **cvc5 solves only 9 of those 59.** This is not "hard for everyone"; it is
  something z3 does that neither other solver does.
- **z3 is not comfortable there either.** Its median time on the 59 is 25.0s
  of a 60s budget (min 1.7s, max 56.2s). A longer competition timeout would
  likely recover a large share of them for Primo -- which is worth
  remembering before treating this as an algorithmic wall.

(sec-6-1a)=
### 6.1a It is a constant factor, not a wall -- and the factor is small

Job 117631 re-ran exactly those 59 files at a 600s budget (two Primo
configurations x 20 shards; z3 was deliberately not re-run, since it solves
all 59 by construction).

**Primo solves 45 of the 59.** Only 8 resist both configurations. So this
band is overwhelmingly a constant-factor problem, and the constant is far
smaller than the 60s cliff suggested:

| | time on the 45 it solves |
|---|---|
| min / p25 / median / p75 / max | 64s / 114s / **167s** / 275s / 563s |

Against a 60s budget that is a **median 2.8x, p75 4.6x, worst 9.4x**. The
payoff is therefore close to linear in the speedup achieved, not
all-or-nothing:

| speedup | files recovered at 60s (of 59) |
|---|---|
| 2x | 15 |
| 3x | 24 |
| 5x | 37 |
| 10x | 45 |

This is the single most useful number in this section: **a 3x is worth 24
files and a 5x is worth 37**, so incremental progress on per-pivot cost pays
proportionally and there is no threshold effect to clear first. Against z3's
own 60s times on the same files Primo is a median 6.6x slower (p90 31.6x),
which is consistent.

The 14 files that do not solve even at 600s are the genuine capability
residue -- about a quarter of the band -- and should not be conflated with
the rest when judging any future mechanism.

**H7 does not convert at a longer budget either**: 41 solved against the
default's 45, +6/-10 churn. Its per-instance wins are real but land on
instances that were not the binding ones, which is the same conclusion the
60s run reached and independent confirmation that shipping it opt-in was
right.

### 6.2 The cost is fill-in, and both Markowitz factors blow up

Profiled on four instances chosen by *z3-time quartile* across the band (p10,
p25, p50, p75) rather than by how dramatic they look -- the H7 write-up
records why that distinction matters.

`:lra.pivot-row-updates` counts rows rewritten (the sum of
`|occurs_in(entering)|`), and `:lra.pivot-term-ops` counts terms merged while
rewriting them, so their ratio decomposes a pivot's cost into its two
Markowitz factors exactly:

| instance | rows rewritten per pivot | terms merged per rewrite | nonzeros/row when prepared |
|---|---|---|---|
| `scrambled228965` | 359 | 31.2 | 5.80 |
| `scrambled15659` | 26 | 21.7 | 4.10 |
| `scrambled312090` | **521** | **91.1** | 5.25 |
| `scrambled180721` | 35 | 44.4 | 4.53 |

Read the last two columns together: rows that hold **4.1-5.8 nonzeros in the
prepared tableau** are merging **21.7-91.1 terms** by the time they are being
rewritten during search. That is a 5-17x densification, and it is happening
*on top of* an entering column that occurs in up to 521 rows. One pivot on
`scrambled312090` therefore costs on the order of **47,000 rational
operations**, and the instance spends its entire 60s budget on 3,904 of them.

This is what "fill-in" means concretely, and why it compounds: every pivot
substitutes the entering variable's row into every row mentioning it, so each
one makes the *next* one more expensive. It is a positive feedback loop, not a
constant overhead.

Note also `scrambled312090`'s search shape: **0 conflicts, 7,555 decisions**.
The SAT solver is not thrashing and learns nothing; essentially all of the
time is inside the tableau.

(sec-6-2a)=
### 6.2a A flat profile of the band, at last (2026-07-31)

Everything in 6.2 above is derived from **counters** (`:lra.pivot-row-updates`,
`:lra.pivot-term-ops`), not from a profiler. The only real QF_LRA flat profile
on record is the `perf` run in the 2026-07-27 design entry -- on
`constraints-tempo-width-30`, a *stratified-sample* file, at a commit predating
H1, H2, H3 and H7. So the band's actual time distribution had never been
measured on the current code. It has now been, with `gprof`: `perf` is
unusable in this sandbox (`perf_event_paranoid=4`, root-only), and `valgrind`
is not installed, so compile-time instrumentation was the only option.

Two mid-band gap files, `-pg` build, self-time:

| | `scrambled339450` | `scrambled228965` |
|---|---|---|
| `LraTableau::pivot` | **61.0%** (3,706 calls) | **85.4%** (11,647 calls) |
| `rebuild_occurs_in_index` | 7.5% (522) | 4.4% (3,870) |
| sort + merge inside `pivot` | 11.6% | 1.6% |
| `util::Rational` operators | ~7% | ~3.5% |
| **allocator (measured separately)** | **6.0%** | **4.0%** |

**The headline is what is no longer there.** The 2026-07-27 profile that
motivated item H2 put **~42% of runtime in the allocator** and ~12% in GMP
copy/init. On the gap band today it is **4-6%**, over 9.8M and 16.9M
malloc/free pairs. H2 did what it was built to do, and **the allocator is
retired as a target** -- an item that would otherwise still look like the
obvious first move to anyone reading the older profile.

*Method note:* the allocator share is the trustworthy number here, because it
was measured on the **Release** build with an `LD_PRELOAD` malloc/free wrapper
accumulating rdtsc cycles -- gprof only samples the executable's own text
segment and cannot see libc at all. The gprof percentages come from a `-pg`
build, which penalises call-heavy code (`Rational`'s copy constructor alone is
called 465M times), so treat their *ranking* as sound and their absolute
values as indicative.

*What the profile confirms.* Section 6.2's counter-derived diagnosis was
right: `pivot` is 61-85% of self time, at 1.96 ms per call on
`scrambled228965`. There is no hidden hotspot elsewhere and no misplaced
overhead left to reclaim. That matters for direction-setting -- it is positive
evidence that the remaining gap needs an algorithmic change (fewer or cheaper
pivots) rather than more tuning, which is the same conclusion D1's rejection
reached from the other side.

*Two ordinary-engineering items it does surface,* neither of them on the
D-list, and worth noting because between them they are ~6-19% against D2's
~14% literature ceiling:

- **`rebuild_occurs_in_index()` is consistently second**, 4.4-7.5%.
  *(Superseded 2026-07-31: this call no longer happens at all on the default
  path. Bounds-only backtracking means nothing structural changes on a pop,
  so there is no index to rebuild -- see [](arithmetic.md)'s roadmap entry.
  The analysis below stands as the record of how the cost was characterised
  and why the incremental fix was the wrong answer.)* Its call
  count is already fixed -- the 2026-07-27 item-0 follow-up cut it from
  once-per-undone-pivot to once-per-`pop()`, a 97% reduction, and the counts
  here (522 calls / 526 pops, 3,870 / 3,874) confirm that holds. What remains
  is that *each* call is a full `O(vars_.size())` rebuild, and these instances
  have ~26,000 variables: ~3.8ms a call. Making it incremental over the undone
  trail entries is a different fix from the one already applied.
- **The sort-merge inside `pivot`'s affected-row loop** is 11.6% on
  `scrambled339450` (and only 1.6% on `scrambled228965`, so it is
  instance-dependent). `combine_scratch_` is built by concatenating the
  affected row's terms with the scaled entering row's terms and then
  `std::sort`-ing the whole thing -- but both halves are already ordered by
  variable id, so this is a merge, not a sort. Worth checking whether rows are
  ordered on *every* path into that loop (`add_row`'s comment at the
  candidate-selection site says a `Row` is not sorted by var id) before
  assuming an `inplace_merge` is safe.

*Caveat on symbol names.* gprof reports one hot symbol as
`vector<SoiBreakPoint>::_M_realloc_insert`, which is impossible under the
default `Sparse` rule -- `soi_select()` is gated behind
`rule_ == PivotingRule::Soi`. Its call-graph parent is the introsort inside
`pivot`, so the name is COMDAT folding of identically-generated template code,
not evidence that SOI runs by default. Checked, because a hot SOI symbol in a
non-SOI run would otherwise read as a real bug.

(sec-6-2b)=
### 6.2b Acting on the profile: H8, and one rejection (2026-07-31)

Both leads 6.2a surfaced were built and measured. One is now the default; the
other was measured and reverted, which is the more useful half of this
subsection.

#### H8 -- merge instead of sort in `pivot`'s affected-row loop (promoted)

The loop concatenated the affected row's terms with the scaled entering row's
terms into `combine_scratch_` and `std::sort`ed the result, once per affected
row per pivot. Both halves are already ordered by var id, so this is a merge.

Rows were only *incidentally* ordered, so the change makes it an **invariant**
(declared on `Row`, checked by a test that fails if `add_row`'s sort is
removed): `add_row` sorts, and `pivot` splices `basic_var` into
`new_entering_row` at its ordered position instead of prepending it.
Everything else already preserved order. Two smaller wins fall out: the
zero check is needed only in the equal-var-id branch, since that is the only
place a coefficient can cancel; and the affected row is now taken by
reference and **moved** into the trail rather than copied twice, which at 55
to 359 affected rows per pivot was the largest single source of the 465M
`Rational` copy-constructions in the profile.

Slurm, paired same-node against the parent commit:

| | base | H8 | churn | median paired |
|---|---|---|---|---|
| mid-band gap (59 files, 300s) | 37 solved, PAR2 19475 | **38**, PAR2 **18309** | +1/-0 | **1.16x** (p25 1.13, p75 1.22) |
| stratified sample (237, 60s) | 189 solved, PAR2 6473 | **192**, PAR2 **6067** | +3/-0 | **1.23x** (p25 1.10, p75 1.33) |

Zero cross-configuration disagreements and zero wrong answers on both.
**Zero regressions on either bar** -- the churn is one-directional, which no
other item in this note has managed. The speedup distribution is tight
(p25-p75 spans 1.10-1.33), so this is a uniform constant-factor win rather
than a few instances carrying an average.

**And on the headline bar** -- all 595 scrambled competition files at 60s,
same-node paired (job 117940):

| | base | H8 |
|---|---|---|
| solved | 369 | **378** (+9, churn +9/-0) |
| PAR2 | 29561 | **28654** |
| median paired over 154 files >1s | -- | **1.19x** (p25 1.12, p75 1.25) |
| 0-50 KB / 50-500 KB / 500 KB-5 MB / >5 MB | 177 / 144 / 41 / 7 | 177 / **148** / **46** / 7 |

Zero disagreements over 595 files. Five of the nine gained files are in the
500 KB-5 MB band, which is where the entire deficit against z3 sits.

*Two honest qualifications.* z3 solved 429 of these in job 117487, so the
deficit now reads 51 where it read 56 at H7 time -- but this job's own
baseline solves 369 where the H7-era run recorded 373 for a comparable
configuration, about 4 files of cross-job drift at a 60s timeout on a shared
cluster. **The within-job +9/-0 is the trustworthy number; the cross-job
deficit figure is not measured to better than a few files.** And section
6.1a's payoff curve says the gap band needs 2x for 15 files and 5x for 37:
H8 is 1.19x and buys 5 there, which matches the curve rather than beating it.
A constant factor of this size is worth having and does not change the
conclusion that closing the band needs an algorithmic change.

#### Incremental `occurs_in_` maintenance (built, measured, rejected)

`rebuild_occurs_in_index()` was 6.2a's other lead and the profile's clear #2
at 8%. Instrumenting it split the cost exactly: the clear pass is **0.05s**
and the fill is **1.65s**, over **138M scattered `push_back`s** into 57,658
separate heap-allocated vectors -- cache-miss-bound, as the ~12ns per push
implies.

The apparent fix: a pop restores only about 384 rows of 47,148, so patching
`occurs_in_` per restored row inside `undo()` looks like ~100x less work than
rebuilding all of it. It was implemented that way (with a checker comparing
the incremental index against a full rebuild after every pivot and pop, which
did catch a deliberately incomplete patch).

**It is 1.4-1.8x slower.** Paired A/B on four gap instances: 0.88x, 0.97x,
0.55x, 0.61x, 0.70x, 0.71x, 0.62x, 0.60x. Reverted.

The estimate was wrong in two compounding ways, both worth recording since
the same reasoning will otherwise be reattempted. First, the cost does not
scale with *pops* (522) but with *pivots undone* (3,706), each restoring ~55
rows. Second, and decisively, removing a listing is not O(1) -- it is a
`std::remove` scan of `occurs_in_[var]`, and the rows being restored are
long (the pivot counters give ~1,048 terms per row update). So the
incremental path trades 138M cheap sequential-ish pushes for ~100M expensive
list scans. **A full rebuild of a cheap index beats a targeted patch of an
expensive one**, which is exactly the tradeoff `rebuild_violated_vars()`'s
own comment already claimed for the analogous case.

**Postscript, 2026-07-31.** The remaining 8% did not stand for long, and
neither fix above was the answer: adopting the paper's bounds-only
backtracking removed the rebuild entirely, because a pop that changes no
row structure has no index to repair. The right lesson is that both
attempts here -- full rebuild and incremental patch -- were optimising a
data structure that only needed maintaining because of a divergence from
the paper elsewhere. Were it still needed, the only fix would be a
different representation for `occurs_in_` (a CSR-style flat
array, so the transpose writes into one contiguous block rather than 57k
separate ones). That touches 29 call sites and is not worth it for 8% on its
own -- but it is the same representation change direction D4 would force
anyway, so it should be considered there rather than separately.

(sec-6-2c)=
### 6.2c Where the time goes now (callgrind, 2026-07-31)

After H8 and bounds-only backtracking, re-profiled with **callgrind** --
which, unlike gprof, sees libc and GMP. On `scrambled228965`:

| share of instructions | attributed to |
|---|---|
| 70.4% | `stl_algobase.h` : `LraTableau::pivot` |
| 11.1% | `stl_iterator.h` : `LraTableau::pivot` |
| 9.8% | `predefined_ops.h` : `LraTableau::pivot` |
| ~1% each | `Rational::operator*`, `from_int128_reduced`, `operator+` |
| 0.22% | libc `_int_malloc` |

**~91% of all instructions are inside `pivot`, and almost none of it is
arithmetic.** The signature -- an STL algorithm, its iterator, and a
predicate wrapper -- is one linear-scan algorithm, and the only such call in
that loop is the `std::remove` that drops `other_basic` from
`occurs_in_[var]`. Counted directly rather than inferred, since symbol
attribution has misled here before (see 6.2a's COMDAT-folding note):

| instance | `std::remove` calls | elements scanned | average scan |
|---|---|---|---|
| `scrambled228965` | 40.1M | **37.6 billion** | 938 |
| `scrambled339450` | 51.5M | **25.9 billion** | 504 |

262.5 billion instructions total on `scrambled228965`, so those scans are
essentially the whole program.

**Most of that work is immediately undone.** The loop removes `other_basic`
from every term's occurrence list, then re-adds it for every term of the
rebuilt row -- and after H8's merge, most terms are in both. Since both rows
are ordered by var id (the `Row` invariant), the true delta is computable in
one linear pass: a term carried over unchanged needs *no* occurrence update
at all, and only terms that genuinely enter the row (from the entering row)
or leave it (cancelled to zero, or `entering_var` itself) need one.

That is [H10](#h10), implemented 2026-08-01: 1.15-4.85x, with `pivot`'s own
profile share falling from 90.7% to 49.6%.

#### The 2026-07-31 attempt, and why it failed

An earlier implementation of the above was reverted the same day after
corrupting `occurs_in_`. The cause is recorded under [H10](#h10): the patch
meant to rewrite the cancellation branch silently did not apply, so that
branch kept adding entries the rest of the new code no longer removed. The
algorithm was correct; the edit had not landed. The guard that caught it --
`occurs_in_index_matches_full_rebuild()` plus two tests that call it after
every pivot -- was written during that attempt and is why the second one
could be trusted.

### 6.3 What this rules out

- **It is not the sparse-pricing cap.** The current default of
  `--lra-sparse-pricing-candidates` is 16, so raising it was the obvious cheap
  fix. Measured on `scrambled312090` at limits 16 / 64 / 256 / 1,000,000, the
  per-pivot cost is essentially flat -- 51,738 / 52,428 / 46,833 / 47,312
  term-ops per pivot across a 60,000x range. Scoring more candidates does not
  find cheaper ones, because there are none: when every eligible candidate
  occurs in hundreds of rows, a better choice among them does not exist. H3
  prices the pivot correctly; the problem is that the cheapest available pivot
  is already expensive.
- **It is not fixed-equality slacks.** That was H7, and it nets zero on this
  band (+8/-8, median 0.64x). See its falsifier outcome above.
- **It is not preprocessing size.** Established on 2026-07-29: prepared
  tableau dimensions match z3's within ~5%.

### 6.4 What z3 appears to be doing

z3's `-st` output on the same instances, with Primo's comparable counters:

| counter | `scrambled228965` | `scrambled15659` | `scrambled312090` |
|---|---|---|---|
| z3 `:arith-offset-eqs` | 139,942 | 259,413 | **1,100,682** |
| z3 `:arith-fixed-eqs` | 45,068 | 158,312 | 373,175 |
| z3 `:arith-bound-propagations-lp` | 302,614 | 498,156 | 1,206,041 |
| z3 `:decisions` | 21,656 | 158,793 | 36,778 |
| Primo `:sat.decisions` | 135,366 | 1,009,955 | 7,555 |
| Primo `:lra.pivot-term-ops` | 130.5M | 201.4M | 185.4M |

**Offset equalities dominate**, and by a wide margin on the largest instance.
But a row-shape census of the *prepared* tableau shows only 175 / 2,267 /
1,844 rows of literal `x - y = c` shape (3.4% / 15% / 7.6% of rows), against
z3's counts in the hundreds of thousands to millions. **So z3 is deriving
these dynamically during search, not recognising them syntactically up
front.** Any Primo version of this is therefore a search-time mechanism, not
a preprocessing pass -- which makes it materially bigger than H7 was, and
that should be priced in before committing to it.

(sec-6-5)=
### 6.5 Candidate directions -- ranked, with status

The open list for this band. **Nothing still open here is implemented** --
D1's mechanism was, which is why it was cheap to evaluate and is now closed.
Ranked by
expected value per unit of work, not by expected value alone, because section
6.1a showed the payoff is close to linear: a 3x is worth 24 files and a 5x is
worth 37, so a cheap partial win is genuinely worth more than an expensive
whole one.

| # | direction | expected size | cost | status |
|---|---|---|---|---|
| D2 | Floating-point-guided basis selection (Monniaux) | **~14% in the literature** | medium | open, but **much smaller than first scoped** |
| D4 | Revised simplex with an LU-factored basis | unknown | largest | open; CSR/library subdirection tabled |
| D5 | Replace the hand-written small-rational form with a library that has one | **unknown, plausibly ~0** | small | open, **not a priority** |
| -- | ~~D1: row-based bound propagation, raised caps~~ | -- | -- | **rejected 2026-07-31** |
| -- | ~~D3: offset-equality detection + bound propagation~~ | -- | -- | **closed 2026-07-31, by D1's sweep** |
| -- | ~~Periodic refactorization~~ | -- | -- | **rejected 2026-07-30** |

D1 was the top entry until its sweep came back flat. Both closures are kept
in place rather than deleted, because the negatives are informative and
because **D1 and D3 turned out to be the same mechanism** -- D3's redirected
design *is* `compute_implied_child_bound`, so the sweep that rejected D1
tested D3's cheap design too, and D3's expensive design was already rejected
on its own census evidence. One residual piece of D3 (a trigger D1's code
lacks) is recorded under D3 below rather than left implicit. D1's rejection
also takes D4's "conflicts with D1" caveat with it.

(d1)=

**D1. Evaluate the row-based bound propagation Primo already has.**
**Evaluated and rejected 2026-07-31 -- the outcome subsection below is the
conclusion; what precedes it is the case that justified running the sweep,
kept so the reasoning can be checked against the result.** z3 reports
`:arith-bound-propagations-lp` of 302k-1.2M on the profiled instances, so
propagation is doing heavy lifting there. Primo has both row-based and
bidirectional row propagation implemented (`--lra-row-propagation`,
`--lra-bidirectional-row-propagation`), but both are opt-in, off by default,
and **have never been evaluated on this band at all**. This is the only entry
that costs a benchmark run rather than an implementation, which is why it is
first regardless of expected size.

*Two findings from a first local look, neither of which was expected.*

**(a) D3's redirected design already exists here.** `compute_implied_child_bound`
derives a bound on one row term from the basic variable's bound plus the
remaining terms -- so for `s = x - y` with `s` fixed and `x` bounded it derives
a bound on `y`, which is exactly the cheap offset-equality mechanism D3 was
redirected to. D1 and the useful part of D3 are the same code.

**(b) It is switched off by its own caps on precisely these instances.**

| gate | default | measured reality (section 6.2) |
|---|---|---|
| `--lra-bidirectional-row-propagation-max-fanout` | 32 | `occurs_in` reaches **521** |
| `--lra-bidirectional-row-propagation-max-row-size` | 32 | rows reach **91** terms |
| `--lra-row-propagation-max-fanout` | 64 | (same 521) |

Both are documented in the source as "not precisely derived". The result is
that bidirectional propagation fires **11-68 times** on instances where the
census in D3 finds 7,000-190,000 binary rows. Raising the caps to 256 raises
it by 30-360x, and the effect on time is real and non-monotone:

| instance | caps 32/32 | 256/256 | unlimited |
|---|---|---|---|
| `scrambled228965` | 9.6s (68 props) | **6.5s** (2,072) | 32.8s (2,181) |
| `scrambled312090` | 61.6s (22) | **58.5s** (7,959) | 64.3s (8,593) |

Note `scrambled312090`: it is a **gap file that times out at 60s in the
default configuration and solves in ~58s here**. Note also that unlimited is
*worse* than 256 on both -- the caps are doing real work, they are simply set
an order of magnitude below what these instances need. This is a
one-constant-sweep experiment, not an implementation.

*Caveat:* 58.5s against a 60s budget is at the noise boundary on this
workstation (see the run-log's environment note), and n=2. What this justifies
is the cluster sweep, not a conclusion.

**Status, 2026-07-31.** The four caps were compile-time constants, which is
why the numbers above came from a rebuild per data point. They are now
`CheckSatOptions` fields with CLI options (see [](arithmetic.md)'s option
table), so the sweep can vary them per run. The gates are pinned by
`row_propagation_caps_are_options_not_constants` in the solver tests, which
checks each of the four against a propagation counter that is nonzero at the
default and zero under the cap -- and was confirmed to fail when the gates
are mutated to ignore the options.

#### D1's outcome: rejected on the band (job 117726, 2026-07-31)

Five configurations over all 59 mid-band files at a 300s timeout. The
timeout is 300s and not 60s deliberately: the manifest *is* the files Primo
fails at 60s, so at 60s the baseline solves 0 of 59 by construction and every
configuration would be scored against zero.

| configuration | solved / 59 | PAR2 | median paired speedup | churn |
|---|---|---|---|---|
| baseline (propagation off) | **37** | 19465 | -- | -- |
| default caps (64/64, 32/32) | 34 | 19719 | 1.01x | +3/-6 |
| all four at 128 | **37** | **18155** | 1.20x | +4/-4 |
| all four at 256 | 36 | 19294 | 1.14x | +5/-6 |
| all four at 1024 | 36 | 18591 | 1.30x | +6/-7 |

Zero cross-configuration disagreements over the 59 files, and zero wrong
answers.

**No cap setting beats the baseline on solved count.** The best result is a
tie. The median paired speedup is real and rises with the caps (1.01x ->
1.20x -> 1.14x -> 1.30x over the ~31 files each pair both solves in more than
a second), but it does not convert: every configuration loses roughly as many
files as it gains, and the losses are not the same files as the gains.

**The experiment is well-controlled -- the mechanism definitely ran.**
Raising the caps turns the propagation on by two to three orders of magnitude
on this band:

| instance | direction | default caps | 256 | 1024 |
|---|---|---|---|---|
| `scrambled312090` | bidirectional | 21 | 7,959 | 8,419 |
| `scrambled312090` | row-based | 213,354 | 904,275 | 955,787 |
| `scrambled228965` | bidirectional | 55 | 2,072 | 2,719 |

So this is not a null result from code that never fired. It is a mechanism
firing 380x harder and still not moving the band.

**The instance that motivated the sweep does not reproduce, and the caveat
above is why.** `scrambled312090` was the whole case for D1: locally it read
61.6s at default caps against 58.5s at 256, i.e. a gap file crossing a 60s
budget. On the cluster it solves in **143s at baseline and 139s at caps 256**
-- a 3% difference, and both figures more than double the local ones. The
local reading was workstation noise at a budget boundary, exactly as the
caveat said it might be. `scrambled228965` does reproduce (67s -> 13s at 256,
a genuine 5x) but is one instance, and 1024 makes it *worse* than baseline
(101s).

**Two files regress under every configuration**: `scrambled178771` (222s at
baseline) and `scrambled21782` (148s), both lost entirely at 300s. The single
file gained by every configuration, `scrambled11414`, lands anywhere from 48s
to 244s depending on caps. That spread is the signature of churn at the
timeout boundary rather than of a systematic effect.

**Conclusion.** D1 as stated -- "raise the caps and evaluate" -- is
**rejected**. Row propagation is not the lever for this band, at any cap
setting tested. The caps stay at their defaults and stay exposed as options,
which cost nothing to keep and are what made this measurable. What is *not*
ruled out is a per-instance or adaptive policy: the 5x on `scrambled228965`
and the 1.30x median are real, and something that could tell those instances
apart in advance would collect them. That is a different and much larger
item than the one being closed here, and it does not get pursued on this
evidence -- the precedent is item H7, rejected rather than tuned after its
own bar came back flat.

**Correctness.** Merge-tier QF_LRA fuzz gate on the 1024-cap configuration
(the point where propagation fires hardest): job 117767, 4 shards x 500 =
2,000 fuzzsmt cases against z3, all four shards' summaries present, **0 wrong
answers**. The 24 recorded timeouts were checked individually against the
default configuration: none is a soundness disagreement, 12 are unknown under
both configurations at a longer budget, and 3 differ only in whether one
configuration finished within the budget. This gate ran the **Release** build
-- the remote ASAN toolchain is still broken, so it carries no sanitizer
coverage.

(d2)=

**D2. Floating-point-guided basis selection** (Monniaux, CAV 2009): run a
floating-point LP, then force-pivot the exact tableau to the basis it found.
Sound, because the numeric result only chooses a starting configuration and is
never trusted -- a bad guess costs exact pivots, not correctness. **Promoted by
section 6.2's finding**: density is a property of the *basis*, and this is the
only technique here that chooses a basis directly instead of walking to one.

*Prior art, surveyed 2026-07-31: this has been done twice more since Monniaux,
and the results are sobering.* King, Barrett & Tinelli (FMCAD 2014) implement
it in CVC4 over GLPK; de Oliveira & Monniaux (PAAR 2012) implement it in
OpenSMT. Both are read into Risk C below, and both into
[](papers/lra-related-work.md). The short version: the technique is real, is
sound when the exact solver verifies, and is worth roughly 14% -- not the
2-5x this band needs.

*Scope: smaller than D4, with a clean hook point.* Three sub-problems.

1. **An FP LP engine.** GLPK (what the paper used) is GPL and this project is
   MIT, so linking it is not an option; other LP libraries would need their
   licences checked. But note the result is only a *hint* -- soundness does not
   depend on it -- so a modest in-house floating-point simplex is acceptable
   and sidesteps the question entirely. It does not need to be competitive,
   only to land near a decent basis.
2. **A force-pivot-to-basis operation** on `LraTableau`. `pivot()` already
   exists and is correct; this is a public entry point that drives it to a
   target basic set.
3. **Deciding what to feed it** -- the real design question, see below.

*Risk A: force-pivoting costs the very thing we are short of.* Reaching an
arbitrary target basis can take on the order of `rows` pivots -- 24,140 on
`scrambled312090` -- and at that instance's measured ~47,000 term-ops per pivot
that is ~1.1e9 operations, against the 1.85e8 it currently performs in its
entire 60s budget. That estimate is pessimistic (pivots from the initial sparse
all-slack basis are far cheaper than pivots from the dense mid-search one), but
it is the number the technique has to beat, and it is directly measurable
before anything is built.

*Risk B: applicability -- raised here, then dissolved by the literature.*
Monniaux's is a **preprocessing** phase, so it needs the constraint system
including its bounds. Primo knows the row structure at `PreparedLraTheory`
construction -- the natural hook, and already the place where everything must
happen "before the first `push()`" -- but **bounds are asserted during search,
not there**. So as a preprocessing phase the technique sees the whole problem
only when the formula is essentially conjunctive, which is class A of section
1.1 (`latendresse`) far more than it is this band.

**King, Barrett & Tinelli (FMCAD 2014) solve this, and their answer is the
interesting part of the entry.** Their `BalancedSolve` runs the *exact* solver
first under a pivot cap (200), and calls the LP solver only when that cap is
exceeded -- a fallback for hard theory calls, not a preprocessing phase. Called
mid-search, the bounds are known, so Risk B goes away. Their `SeedExact` is
then Monniaux's force-pivot by another name; the paper says so.

*Risk C, which replaces it: the literature says the payoff is small.* This is
the part that should be settled before any of the above gets built.

| study | setting | reported gain |
|---|---|---|
| de Oliveira & Monniaux, PAAR 2012 | OpenSMT, LP-first seeding, 634 benchmarks | **14%** overall (and 1.7% of files had a float/exact disagreement) |
| King, Barrett & Tinelli, FMCAD 2014 | CVC4 + GLPK, 652 QF_LRA benchmarks | **+9 files**, all `miplib`, via the MIP path |

And KBT's own summary of everything before them: existing SMT solvers
*outperform* the LP-augmented versions on SMT-LIB, "even if the LP solver
results are not checked for correctness". Their balanced control structure is
what makes it pay at all.

Section 6.1a says this band needs 2x for 15 files and 5x for 37. A 14% overall
speedup and +9 files on a 652-file set are an order of magnitude off that.
**D2 is not a 10-30x technique and should not be scoped as one** -- see
[](papers/lra-related-work.md) for the full survey. Note also that the cvc5
1.3.4 we benchmark reports `glpk : no`, so its 9-of-59 on this band is not
evidence about the technique either way.

*Cheap first steps, still not building it.* Two, in order. (1) Measure what
fraction of each gap instance's bounds are asserted at decision level 0 --
that decides preprocessing-phase versus KBT-style fallback, and it is a
counter, not an implementation. (2) Before either, weigh Risk C: the honest
reading is that the whole line is worth ~14% where this band needs 200-500%.

(d3)=

**D3. Dynamic offset-equality detection.** Investigated 2026-07-30 in two
steps, both before writing any implementation. The result is that the
mechanism is **real and worth having**, but the obvious design for it is
**wrong**.

*Step 1 -- is it load-bearing for z3, on these instances?* Yes, and this is
measured rather than inferred. z3 exposes `smt.arith.propagate_eqs`, so it can
be ablated directly on the profiled instances:

| instance | `propagate_eqs=on` | `off` | speedup from the mechanism |
|---|---|---|---|
| `scrambled228965` | 3.1s | 6.9s | **2.2x** |
| `scrambled15659` | 8.0s | 25.9s | **3.2x** |
| `scrambled312090` | 19.7s | 62.8s | **3.2x** |
| `scrambled180721` | 26.1s | 21.1s | 0.81x -- *harmful* |

`:arith-offset-eqs` drops to zero when disabled, confirming the counter tracks
this mechanism. And since these are pure QF_LRA with no second theory,
**it is not Nelson-Oppen theory combination** -- the usual purpose of implied
equalities in SMT, and the outcome that would have made D3 worthless here.
2.2-3.2x is squarely in the range section 6.1a says is worth 15-24 files.

But note the ceiling: even with the mechanism *off*, z3 solves all four in
under 63s, where Primo needs ~600s. So offset equalities are worth a few x of
a 10-30x gap. Implementing D3 perfectly would be valuable and would not close
this band.

*Step 2 -- can Primo see them, and what would exploiting them buy?* Detection
is nearly free: item H7 already hooks the moment a basic variable becomes
fixed, and a fixed variable whose row has two terms *is* a relation between
two variables. Throwaway instrumentation on that hook (measured, reverted):

| instance | tableau vars | distinct eliminable columns | share | z3's speedup |
|---|---|---|---|---|
| `scrambled312090` | 26,041 | 778 | **3.0%** | **3.2x** |
| `scrambled228965` | 5,694 | 369 | 6.5% | 2.2x |
| `scrambled15659` | 23,571 | 2,524 | 10.7% | 3.2x |
| `scrambled180721` | 17,286 | 4,032 | **23.3%** | **0.81x** |

**The correlation is inverted.** The instance with the most eliminable columns
is the one where z3's mechanism *hurts*; the one with the fewest gets the
largest win. If the payoff came from shrinking the tableau, it would track the
eliminable share. It anti-correlates.

*What that implies for the design.* The expensive interpretation -- merge the
columns, eliminate a variable outright -- requires a **constant term in `Row`**
(17 internal term loops plus 4 external consumers of `row()`, and the constant
would participate in assignments *and* in Farkas certificates, so `pivot()`,
`update()` and `build_conflict()` all change). H7 explicitly dodged that as "a
much larger change". Step 2 says that expense is **not justified**: 3-23% of
columns cannot produce 3x, and the sign is wrong anyway.

The cheap interpretation fits the evidence better: **detect the relation and
propagate bounds across it.** If `x - y = c` and `x >= l`, then `y >= l - c`.
No constant column, no column merge, no structural change -- detection reuses
H7's existing hook and propagation reuses the theory-propagation path Primo
already has. Note also that *scaled* binary rows (`a*x + b*y = c` with
`a != -b`) outnumber literal offset rows on two of four instances and are
equally usable this way, so the opportunity is the `binary_rows` count, not
the narrower `offset` one.

*Status, superseded 2026-07-31: mostly closed, and by D1's sweep rather than
by its own experiment.* The cheap design described just above -- detect the
relation, propagate bounds across it -- **is `compute_implied_child_bound`**,
as D1's finding (a) records. So D1's cap sweep tested it: binary rows have
size 2 and pass any row-size cap, and at caps 1024 the fanout gate is
effectively off, so the mechanism ran at full reach on the whole band and did
not beat the baseline. D3's expensive design was already rejected in step 2
above. Both interpretations of D3 are therefore closed, and it should not be
carried as an open direction.

*What genuinely remains, stated so it is neither lost nor re-proposed at full
cost:* a **trigger** D1's code does not have. D3 proposed firing on H7's hook
-- the moment a basic variable becomes fixed. D1 fires only on a bound change
to a variable that occurs as a *nonbasic term* in some row, and `pivot()`
clears `occurs_in_[entering_var]`, so a basic variable occurs in nothing and a
bound change on it scans nothing. Concretely: when the equality that fixes `s`
is asserted after `x` already has its bound, the implied bound on `y` is not
derived until one of that row's terms moves again. The assertion orders where
the equality lands last are missed.

That gap is real and cheap to close, but the sweep argues against expecting it
to pay: it would buy *more firing*, and going from 21 to 8,419 bidirectional
propagations on `scrambled312090` moved nothing. Worth recording, not worth
building on this evidence.

Two things from D3's own investigation survive independently of all this and
are still the best evidence in this section: the z3 ablation (the mechanism is
load-bearing for z3 on these instances, and it is *not* Nelson-Oppen theory
combination), and the ceiling it establishes -- even with the mechanism off,
z3 solves all four profiled instances in under 63s where Primo needs ~600s.
**Offset equalities were never going to close this band**, which is now
measured from both ends rather than argued from one.

(d4)=

**D4. Revised simplex with an LU-factored basis.** The representation change
the rejected entry below turned out to require, and the hardest item here.
King's thesis records that no Simplex-for-DPLL(T) implementation does it.

*Scope.* Not a feature but a change of the data structure the class is built
on. `LraTableau` is ~1,680 lines with 18 internal uses of `rows_` and 29 of
`occurs_in_`; in revised simplex **neither concept exists**. Instead of
storing `B^-1 N` explicitly, it keeps the original constraint matrix (sparse,
and never changing) plus an LU factorization of the basis matrix `B`, and
materialises what it needs on demand: a row as `e_i^T B^-1 N` (a BTRAN solve
plus a pass over N), a column as `B^-1 A_j` (an FTRAN solve) -- see the
terminology table for both. A pivot then updates the LU factors rather than
rewriting every affected row, which is the cost this whole section is about.
Consequences, which differ per subsystem:

- `update()` *improves* -- walking `occurs_in_` to update dependent basic
  variables is exactly an FTRAN.
- `build_conflict` is fine: one BTRAN per conflict to materialise the violated
  row for the Farkas certificate.
- **Row-based bound propagation breaks entirely** -- it iterates
  `occurs_in(var)` and then reads rows, and neither survives. This used to be
  a conflict with D1; D1's rejection removes it, so what would be lost is a
  mechanism measured not to pay on this band anyway.
- **SOI breaks** -- its objective row is explicit and incrementally maintained.
- **push/pop is the crux.** The trail snapshots `old_basic_row` and
  `old_other_rows` per pivot; there is no un-applying an LU update, so a pop
  means refactorizing. LP solves one problem to optimality, DPLL(T) pops
  thousands of times. One mercy: asserting a bound does not change `B` (bounds
  are on variables, not rows), so the basis matrix is stable across most of
  the search.

*The likely killer is arithmetic, not architecture.* In floating point LU
entries stay bounded; in **exact rational** arithmetic LU factors suffer
coefficient growth, so a sparser factored representation can still lose to a
denser explicit tableau because every entry costs more. That this is real and
studied is evidenced by integer-preserving (Bareiss-style) factorization and
SPEX existing at all -- see "Expediting exact linear programming solvers via
integer preserving factorization" (Operations Research Letters, 2025). This,
rather than oversight, is the plausible reason nobody in SMT does it.

*CSR/library note, tabled 2026-08-04.* The tempting smaller version of D4 is
"replace `rows_` / `occurs_in_` with a respectable sparse-matrix library".
The respectable candidates exist, but they do not make this a local change.
Eigen's `SparseMatrix` is a compressed row/column sparse container with
compressed and insertion modes, and its own documentation explicitly prices
random insertion and sparse write access as special cases rather than the
normal fast path. SuiteSparse is the standard sparse linear-algebra suite, but
it is aimed at sparse factorizations and graph/sparse kernels, not at a
mutable exact DPLL(T) tableau that must emit explanations after every pivot.
HiGHS and COIN-OR CLP are the respectable LP/simplex libraries, but using
them would be a theory-solver architecture change rather than a container
swap: they are numerical optimization engines, not exact SMT theory solvers
with Primo's bound trail, strict-delta arithmetic, and Farkas reasons.

Plain CSR is therefore tabled as an implementation shortcut. It makes the
wrong operations hard for the current explicit tableau: each pivot changes row
lengths, inserts new coefficients, deletes zeros after cancellation, updates
the transpose `occurs_in_` view, and has to be undoable on the SOI path.
Keeping both row-major and column-major compressed mirrors would move the
complexity into synchronization; keeping only row-major CSR would make the
current sparse-pricing and update paths lose their cheap column access. The
2026-08-04 vector-capacity prototype already falsified the simplest
allocation-level version of this idea. Any worthwhile sparse-layout work
should be scoped as D4 proper -- revised/simplex-style representation or a
custom row+column entry structure -- not as "use CSR".

References checked for this note: Eigen sparse-matrix documentation
(`https://libeigen.gitlab.io/eigen/docs-nightly/group__TutorialSparse.html`),
SuiteSparse (`https://github.com/DrTimothyAldenDavis/SuiteSparse`), HiGHS
(`https://highs.dev/`), and COIN-OR CLP (`https://github.com/coin-or/Clp`).

*Cheap first step, which is not building it.* Take a prepared tableau from one
of the four profiled instances, compute an exact LU with a Markowitz ordering
offline (sympy, or SPEX), and compare `nnz x entry bit-size` against the
explicit tableau's. About a day, touches no solver code, and either kills D4
or makes it credible enough to justify the rewrite.

**Rejected: periodic refactorization to discard accumulated fill-in**
(2026-07-30, before implementation). In *revised* simplex the basis is an LU
factorization updated per pivot (Forrest-Tomlin, Bartels-Golub); those updates
accumulate fill-in and numerical error beyond a fresh factorization, and
refactorization discards exactly that surplus. Primo stores the **explicit
tableau**, which for a given basic/non-basic partition is `B^-1 N` --
*uniquely determined*. Recomputing it from the original rows returns the
identical matrix at identical density. The 5-17x densification in section 6.2
is a property of the **basis reached**, not recoverable garbage from the path
taken to it, so there is nothing to discard. See
[](papers/lra-related-work.md)'s refactorization note.

*The lesson generalises, which is why this is kept rather than deleted:*
"measured headroom" is only headroom if some mechanism can actually recover
it. The 5-17x looked like an opportunity and was a description of the problem.

**A falsifier for the whole diagnosis, stated in advance:** if a mechanism
reduces `:lra.pivot-term-ops` per pivot substantially on these four instances
and the band's solved count does not move, then per-pivot cost is not what
separates Primo from z3 here, and the remaining explanation is search *order*
(z3 needs 36,778 decisions where Primo needs 7,555 but never finishes -- the
shapes are not comparable, which is itself a hint worth chasing).


(h11)=
(d5)=
### D5 -- Let a library own the small-rational representation

**Status: open, and deliberately not a priority.** Recorded 2026-08-03 because
it was an unexamined assumption, not because anything measured says it is
wrong.

**What [H2](#h2) actually chose.** The two-tier `util::Rational` is documented
as though the options were "wrap `mpq_class`" or "write a fast path". GMP
really cannot do it itself -- `__mpz_struct` holds a pointer to separately
allocated limbs with no inline storage, `__mpq_struct` is two of those, and
that layout is frozen public ABI, so even a 1-limb value allocates. But other
libraries *do* provide exactly this scheme: FLINT's `fmpz`/`fmpq` is one
machine word holding either a small immediate or a reference to a big value,
which is what H2 implements by hand. The real choice was therefore "keep GMP
and add a fast path" against "adopt a library that already has one", and only
the first was ever considered.

**Why it is not urgent.** H2 already captured the win that was on the table --
54% of runtime was allocation and GMP bookkeeping, and removing it gave
2.1-5.2x on the profiled instances and 108 -> 124 on the promotion bar. A
library-provided form would be solving a problem that is already solved. The
plausible gain is whatever separates `fmpq`'s immediate path from ours, which
there is no reason to assume is large, and it is not where the remaining QF_LRA
gap lives: [](#sec-6) puts that in pivot count and search order, not in
per-rational overhead.

**Why it is worth keeping on the list.** The cost is not performance, it is
ownership. The canonical-form invariant -- big form *iff* the value does not
fit small -- is ours to maintain, and violating it breaks
`LinearPolynomial`/`PolynomialKey` hashing *silently* rather than visibly. That
is precisely the class of obligation a library should carry. If the invariant
ever does produce a bug, this direction stops being cosmetic.

**Cheap to attempt, if it is ever attempted.** Every GMP mention is inside
`src/util/rational.{hpp,cpp}`, by the same containment rule that keeps CaDiCaL
inside `CadicalBackend`, so the backing representation can be swapped without
touching a call site.

**Falsifier, stated in advance:** *if a `fmpq`-backed `util::Rational` is not
at least as fast as the current one on the H2 profiling instances
(`DTP-Scheduling/constraints-tempo-width-30` and the four scrambled
instances), the hand-written path stays and this entry closes.* Speed parity
is the bar, not speed improvement -- the reason to switch would be shedding
the invariant, so matching performance is enough to justify it and failing to
match is enough to reject it.

## H11 -- Fill-in-aware pivoting in preprocessing Gaussian elimination

**Status: proposed, not implemented.** To be added behind an option, default
off, and promoted only on measurement.

### Where this came from

Not from a design argument. Reviewing the model-production merge on 2026-08-02
turned up a QF_LRA behaviour change with no plausible cause in the diff:
`miplib/vpm2-0` moved `:preprocessing.solve-eqs.rewrites` 390 -> 252 and
`:lra.pivots` 26890 -> 12537. Bisecting it landed on the frontend interning a
term per declared Real constant at `declare-fun` time, which assigns TermIds in
declaration order rather than first-use order.

The site is tracked as entry 1 in [](termid-order-sensitivity.md), the docket for
this class of behaviour. It mattered because of this, in
`collect_gaussian_elimination_candidates`:

```cpp
std::sort(eligible_variables.begin(), eligible_variables.end());  // by TermId
for (const core::TermId variable : eligible_variables) {          // pivot order
  for (std::size_t i = 0; i < rows.size(); ++i) {                 // first row with it
```

**Preprocessing Gaussian elimination pivots on whichever eligible variable has
the smallest TermId, and eliminates using the first row that mentions it.**
Deterministic, but arbitrary: pivot choice is a function of term-allocation
order, so an unrelated frontend change reshuffles it. The eager interning was
reverted (see the [regression test](#h11-note-on-the-fix)), which removes *this*
trigger without removing the coupling.

Two things are worth separating. Order sensitivity of this kind is ordinary in
SMT and is not by itself a defect. What makes it worth an item is that the
pivot rule is *uninformed*: it optimises nothing.

### The proposal

Choose pivots by expected fill-in rather than by TermId, exactly as
[H3](#h3) does for the tableau's entering variable -- the Markowitz product of
the variable's occurrence count and the pivot row's length. Two of the three
inputs are already computed a few lines away in `substitute_solved_constants`:
`occurrences` is built there, and row lengths are available from
`rows[i].coefficients`.

This is the naive strategy H3 already replaced once, in the other pivoting
context. The argument that worked there is the argument here.

### Option

`--lra-gaussian-markowitz` (default off), so the preprocessing pivot rule can
be A/B'd without rebuilding, and so a promotion decision rests on a paired run
rather than on the change being obviously sensible. Ablation invariance is the
first check: with the option off, `:preprocessing.solve-eqs.*` and `:lra.pivots`
must be bit-identical to the pre-change build, the check that made H2 and H3
trustworthy.

### Falsifier, stated in advance

*If fill-in-aware pivoting does not reduce `:preprocessing.term-size.final` or
`:lra.pivot-term-ops` on the instances where TermId order demonstrably mattered
-- `miplib/vpm2-0`, `miplib/pp08a-*`, the Heizmann chain -- then preprocessing
pivot choice is not worth optimising and the item should be rejected outright
rather than tuned.* [D1](#d1) and the item-8 cost-ranking episode are the
precedents for rejecting rather than tuning.

A second, cheaper falsifier comes first: on the five-variable chain in
`declaration_order_does_not_change_the_search`, reversing the declarations
changed accepted substitutions from 4 to 0 under the old behaviour. If a
fill-in-aware rule cannot keep that at 4 under both orders, it is not actually
choosing on fill-in.

(h11-note-on-the-fix)=
### Note on the fix that preceded this

The eager interning was replaced by recording `SymbolId` and materialising
terms in `declared_real_constant_terms()`, called only when a model was
requested and only after the search. `mk_constant` interns, so constants
occurring in an assertion keep the id they already had.

The regression test is worth knowing about before writing another one like it.
The obvious test -- compare a `produce-models` run against a plain one -- does
**not** detect the defect, because the interning was unconditional and moved
both arms together; that test passed with the defect reintroduced. The
invariant that discriminates is that permuting `declare-fun` lines must not
change the search.
