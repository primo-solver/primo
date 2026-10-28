# Reported QF_LRA results

Mikoláš Janota and Jan Jakubův, *Vibe-Coded and Tuned: A State-of-the-Art
SMT Solver for QF-LRA* {cite:p}`janota2026vibecodedtunedstateoftheartsmt`,
[arXiv:2609.16706v1](https://arxiv.org/abs/2609.16706v1),
15 September 2026. The paper reports Primo's development and RamParILS tuning.

## Published experiment

Section 4 reports the following results. PAR2 is summed runtime in seconds,
charging twice the cutoff for each unsolved instance; lower is better.

| Configuration | SMT-LIB: solved / 1,753 | PAR2, 30 s cutoff | Competition selection: solved / 519 | PAR2, 1,200 s cutoff |
|---|---:|---:|---:|---:|
| Primo ⋆ | 1,599 | 12,806 | 504 | 47,653 |
| Primo default | 1,536 | 16,597 | 499 | 65,821 |
| OpenSMT default | 1,578 | 14,193 | 502 | 54,584 |
| Yices default | 1,563 | 13,886 | 495 | 73,077 |

Primo ⋆ enables `--lra-model-phase --monotone-elimination` (Figure 3).
The authors replayed the SMT-COMP 2026 selection with a 7 GB memory limit;
these are their measurements, not an official Primo competition placement.
The 519 instances include 194 tuning instances and 325 held-out instances.
Hardware: two AMD EPYC 7513 processors, 512 GB RAM, 64 concurrent solver
processes. References: OpenSMT `v2.9.2-35-g15b42c6f` and Yices 2.7.0
`4f19700f`. Tables 1–2 also report Primo + and a virtual best solver.

## Relation to the current manual

This is a dated results report. The current
{ref}`automatic LRA policy <automatic-lra-heuristics>` enables
model-based phase selection and monotone elimination for declared QF_LRA
rebuild queries with automatic preprocessing; other routes default them off.
The paper's configuration names must stay attached to its measured snapshot.
Its early capability description also predates the broader implementation
described in the manual.

## Evidence still to connect

The two aggregate tables still need to be matched to exact Primo commits,
input manifests, commands and raw artifacts in the repository's run ledger.
Linking those records is open documentation
work. Do not infer a matching experiment merely from the corpus size or an
option name: the ledger contains other QF_LRA runs with different settings.
