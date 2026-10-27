# Evidence about useful tactic configurations

## Question and evidence standard

This note asks a narrower question than the strategy-synthesis literature usually
answers: **which tactics and parameter values occur in the generated strategies
that the authors published or released for evaluation?** Search procedures are
included only when they affect the interpretation of that evidence.

Three evidence levels are kept separate:

1. a tactic offered to the synthesizer is only a **candidate**;
2. a tactic occurring in a published final program **survived selection** for
   that benchmark family;
3. only a controlled removal experiment supports a claim about the tactic's
   marginal contribution.

None of the papers below provides a per-tactic ablation. Consequently this note
does not call any individual tactic necessary or generally beneficial. Generated
programs may contain redundant calls, and parameters learned for one Z3 version
and benchmark family are not defaults for Primo.

The papers and artifacts are used as empirical design evidence only. No solver
source or strategy implementation is incorporated into Primo.

## Early generated strategies: Gálvez Ramírez et al.

Nicolás Gálvez Ramírez, Youssef Hamadi, Eric Monfroy, and Frédéric Saubion,
*Towards Automated Strategies in Satisfiability Modulo Theory*, EuroGP 2016,
DOI `10.1007/978-3-319-30668-1_15`.

The authors' six-page companion appendix was read in full on 2026-08-30. It
prints the generated LIA and LRA strategies and is therefore the primary source
for the configurations below. The main chapter was not available in full in
this workspace; no claim about its comparative scores is made here.

- Chapter record: <https://doi.org/10.1007/978-3-319-30668-1_15>
- Authors' companion appendix, archived:
  <https://web.archive.org/web/20240414123740id_/https://www.inf.utfsm.cl/~ngalvez/strategies.pdf>

### Tactics that survived

The generated strategies make a useful distinction between preprocessing and
terminal portfolios:

| Corpus | Selected preprocessing | Selected terminal structure |
|---|---|---|
| LIA | bounded `ctx-simplify` | `qe` followed by `smt` |
| LRA, formulas with known results | configured `simplify` | `smt` |
| LRA, formulas previously returning unknown | repeated `simplify` and `ctx-simplify`, plus `solve-eqs` and `propagate-ineqs` | bounded fallback among `smt`, `qflra`, `qe-sat`, and `qe` followed by `smt` |
| LRA, alternative evolutionary run | `solve-eqs`, `elim-uncnstr`, `propagate-ineqs`, another `solve-eqs`, then `ctx-simplify` | bounded fallback among the same arithmetic solvers |

The exact non-default values printed in the appendix are:

- `ctx-simplify`: `max_depth = 30`, `max_steps = 5,000,000`;
- `simplify`: `pull_cheap_ite = true`, `local_ctx = true`, and a printed
  context limit of `10,000,000`;
- terminal time slices: 500, 789, 1,250, 1,315, 1,500, 2,631, and 3,000 ms.

The printed simplifier parameter is `local_txt_limit`. The documented Z3
parameter is `local_ctx_limit`, so this is likely a typographical error in the
appendix. Until the original artifact is recovered and executed with its stated
Z3 version, the configuration must not be treated as directly reproducible. One
fallback is also printed as `(and-then qe-smt)`, which is ambiguous between a
hyphenated tactic name and a missing separator between `qe` and `smt`. These are
reasons to preserve generated tactic programs as versioned executable artifacts,
not only as typeset figures.

Several generated programs repeat the same tactic, including `solve-eqs`,
`ctx-simplify`, `simplify`, and `propagate-ineqs`. That could reflect useful
phase interaction, a deliberate second fixed-point opportunity, or generator
redundancy. The appendix has no deletion experiment that distinguishes those
explanations.

For Primo, the durable signal is the action boundary, not the literal program:
equation solving, unconstrained elimination, inequality propagation, contextual
simplification, and ordinary simplification were independently selectable
before the terminal arithmetic portfolio. Gaussian elimination does not appear
as a separate selected tactic.

## Z3alpha published programs

Zhengyang Lu, Stefan Siemer, Piyush Jha, Joel Day, Florin Manea, and Vijay
Ganesh, *Layered and Staged Monte Carlo Tree Search for SMT Strategy Synthesis*,
IJCAI 2024, DOI `10.24963/ijcai.2024/211`. The conference paper and its extended
version were read in full. The open 2025 journal extension was also checked for
the candidate tables and the same experimental scope.

- Conference paper: <https://www.ijcai.org/proceedings/2024/0211.pdf>
- Extended paper: <https://arxiv.org/pdf/2401.17159>
- Released artifact: <https://github.com/JohnLyu2/z3alpha>

The current repository no longer contains the IJCAI result directory. This
audit uses the 14 sample `strategy_z3alpha.txt` files present at artifact commit
`fd8329a4b058887f54444a6adc419e41ab94f786`; they were removed by later commit
`a2994566ae49266543612d338c60a3c8b81ca844`. The 14 programs include three
different time budgets for Sage2, so they are not 14 independent corpora.

### Recurring preprocessing core

The following counts are structural occurrences in those published programs,
not runtime weights. “Programs” counts a tactic once per file even when it is
repeated in several branches.

| Tactic | Occurrences | Programs containing it | Interpretation |
|---|---:|---:|---|
| `simplify` | 48 | 13 | ubiquitous configurable rewrite boundary |
| `solve-eqs` | 27 | 13 | broad cross-logic equation elimination |
| `elim-uncnstr` | 23 | 11 | broad cross-logic unconstrained-symbol elimination |
| `propagate-values` | 23 | 10 | broad cross-logic propagation |
| `ctx-simplify` | 11 | 8 | useful, but less universal and family-sensitive |

The generated programs also contain `smt` 27 times in 13 files. It is a
terminal solver wrapper, not evidence for combining the whole SMT solver into a
preprocessing tactic.

This recurring five-tactic preprocessing core is the strongest cross-paper
evidence for Primo's near-term modularization priorities. It is much stronger
than the evidence for a standalone Gaussian-elimination action: Gaussian
elimination is absent from both the Z3alpha candidate tables and its published
programs.

### Logic- and family-specific lowering

| Logic or family | Tactics and configurations that survived |
|---|---|
| QF_LIA | `propagate-ineqs`; `lia2pb(max_bits = 32)` on one medium-size branch; otherwise `solve-eqs`, `propagate-values`, `simplify`, and `qflia` or `smt` |
| QF_LRA | `solve-eqs`, `propagate-values`, `elim-uncnstr`, and `ctx-simplify`; terminal `qflra` or `smt(random_seed = 200)` |
| QF_BV, core | `bit-blast` followed by `sat`, with `propagate-values(push_ite_bv)` and aggressively configured `simplify`; no `aig` branch |
| QF_BV, Sage2 | combinations of `solve-eqs`, `elim-uncnstr`, `ackermannize_bv`, `max-bv-sharing`, `purify-arith`, `propagate-values`, `bit-blast`, and `sat` or `smt` |
| QF_BV, full logic | an `aig -> qfbv` branch for very large formulas; other branches use bit blasting or Ackermannization plus the generic preprocessing core |
| QF_NIA, leipzig | `nla2bv(max_bv_size = 4 or 16)` followed by simplification, bit blasting, and `sat` or `smt`; fallback `qfnra-nlsat(inline_vars = false, factor = false, seed = 300)` |
| QF_NIA, AProVE | `lia2card`, `card2bv`, `cofactor-term-ite`, contextual simplification, and `qfnra-nlsat`; one fallback uses `nla2bv(max_bv_size = 4)` |
| QF_NRA | portfolios of `smt` and `qfnra-nlsat` around `solve-eqs`, `propagate-values`, `elim-uncnstr`, and `ctx-simplify` |
| QF_S | string-specific lowering mixed with `simplify`, `solve-eqs`, `elim-uncnstr`, and `ctx-simplify` |

The family split is important. `aig` occurs in the published full-QF_BV
program but not the core program. The two QF_NIA families select substantially
different lowering: leipzig is centered on `nla2bv`, while AProVE uses
cardinality conversion and term-ITE cofactors. A logic label alone is therefore
too coarse for a fixed tactic recipe.

### Parameter patterns that survived

The published programs expose only a small number of parameter shapes:

- `simplify` commonly uses a bundle of Boolean options all set to `true` or all
  set to `false`. The bundle includes combinations of `elim_and`,
  `blast_distinct`, `local_ctx`, `som`, `flat`, ITE movement, and multiplication
  hoisting. These extremes reflect the candidate grid and should not be copied
  as Primo defaults.
- `propagate-values` chooses both values of `push_ite_bv`; neither value is
  uniformly preferred.
- `nla2bv_max_bv_size` survives at 4 and 16, although the offered pool also
  contained 8, 32, 64, and 128.
- `lia2pb_max_bits` survives at 32 from an offered pool of 16, 32, 64, and 128.
- `qfnra-nlsat` survives with both `inline_vars/factor = true/true` and
  `false/false`, using seeds 100, 300, or 500.
- `smt` uses random seeds 0, 100, 200, and 500.
- Bounded fallbacks use 2, 8, and, in the five-minute Sage2 strategy, 128
  seconds.

The extremes and random seeds show why tactic identity must include an immutable
parameter record. They do **not** show that seed tuning or an all-on simplifier
has a portable semantic advantage.

Some outputs contain adjacent duplicate tactics or repeat a phase several times
without a probe between them. Z3alpha's published result does not remove these
calls, so the raw occurrence counts overstate the number of demonstrated
distinct phase positions. A Primo portfolio builder should normalize obviously
idempotent duplicates and then remeasure, rather than treating generated syntax
as an architectural prescription.

## SIRISMT: candidates and parameter ranges, but no released final program

Bingzhe Zhou, Hannan Wang, Yuan Yao, Taolue Chen, Feng Xu, and Xiaoxing Ma,
*Simulate, Refine and Integrate: Strategy Synthesis for Efficient SMT Solving*,
IJCAI 2025. The nine-page paper was read in full and its released artifact was
inspected at commit `7d174d7dc16da31376a254bf8db24344135d7d8b`.

- Paper: <https://www.ijcai.org/proceedings/2025/0887.pdf>
- Released artifact: <https://github.com/SoftWiser-group/SiriSMT>

The released normal configuration offers these 18 actions:
`simplify`, `smt`, `bit-blast`, `bv1-blast`, `solve-eqs`, `aig`,
`qfnra-nlsat`, `sat`, `max-bv-sharing`, `reduce-bv-size`, `purify-arith`,
`propagate-values`, `elim-uncnstr`, `ackermannize_bv`, `ctx-simplify`,
`nla2bv`, `cofactor-term-ite`, and `lia2card`.

Simulation uses default parameters. Refinement may tune:

- eight Boolean simplifier options;
- `propagate-values.push_ite_bv`;
- `qfnra-nlsat.inline_vars`, `factor`, and a seed in the configured 50--100
  range;
- `nla2bv_max_bv_size` in the configured 0--100 range.

The paper's aggregate component ablation supports tuning and reduction as
mechanisms: removing parameter tuning produced the largest reported degradation
among its ablations, and removing sequence reduction was next. It does not say
which parameter or tactic caused the difference.

The repository does not release the evaluated final decision-tree strategies;
its SMT-COMP archive contains only a solver binary. Therefore SIRISMT adds no
auditable tactic-level survival evidence beyond its candidate set. In
particular, the four-tactic QF_BV sequence in the paper's introductory figure is
described as part of a default Z3 strategy, not a SIRISMT result.

## Cross-paper conclusions for Primo

The consistent evidence supports a deliberately small first portfolio
vocabulary:

1. keep ordinary simplification independently configurable;
2. separate equation solving from unconstrained-symbol elimination;
3. separate value propagation, and for arithmetic expose inequality
   propagation independently;
4. expose contextual simplification as a costlier optional phase;
5. keep theory lowering (`bit-blast`, `nla2bv`, cardinality conversion, and
   similar transformations) separate from terminal solvers;
6. represent terminal solvers and bounded fallback as control machinery, not as
   preprocessing phases.

This points directly at splitting Primo's current preprocessing round where
those semantic responsibilities already exist. It does not justify inventing a
standalone tactic merely because an implementation helper has a familiar name.
In particular, neither solved-constant substitution nor its internal Gaussian
candidate generation should be called Gaussian elimination unless it has a
separate contract, applicability domain, reconstruction obligation, statistics,
and evaluation arm.

The next empirical step should be a Primo ablation matrix over the QF_LRA
corpus, with each real phase independently disabled and with phase-local
statistics. Useful columns are solved count, wrong answers, PAR-2, assertion and
term-count deltas, reconstruction records added, and per-phase work. Only that
experiment can decide whether Primo's current phase boundaries are useful
portfolio arms; the published Z3 configurations identify candidates, not the
answer.

## Papers still requiring primary text

The same research line also contains *Evolving SMT Strategies* (ICTAI 2016,
DOI `10.1109/ICTAI.2016.0046`) and *Improving Complex SMT Strategies with
Learning* (ITOR 2020, DOI `10.1111/itor.12650`). The full primary texts were not
available through the authors, publisher, OpenAlex, or the local paper drive on
2026-08-30. Under Primo's paper-grounded policy they are recorded as follow-up
sources, not summarized from abstracts or secondary descriptions.
