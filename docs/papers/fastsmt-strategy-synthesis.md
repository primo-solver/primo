# FastSMT strategy synthesis

## Source and scope

Mislav Balunović, Pavol Bielik, and Martin Vechev, *Learning to Solve SMT
Formulas*, NeurIPS 2018. The 12-page proceedings paper and the authors'
17-page extended version were read in full on 2026-08-30. The extended version
contains the experimental tactic tables and additional synthesis details.

- Proceedings paper: <https://papers.nips.cc/paper_files/paper/2018/file/68331ff0427b551b68e911eebe35233b-Paper.pdf>
- Authors' extended version: <https://pavol-bielik.github.io/data/papers/fastsmt-2018.pdf>
- Released artifact: <https://github.com/eth-sri/fastsmt>, inspected at commit
  `27e39866db0863ff38b718c211e0c9ec29d36ce1`

This is a literature and artifact survey, not an implementation source. No
FastSMT or Z3 code is incorporated into Primo.

## What FastSMT searches

FastSMT treats strategy construction as formula-transforming tree search. A
state contains the current formula and the tactic sequence used to reach it. An
action is one tactic plus its parameter values. A branch-free sequence is a
candidate strategy; terminal states are solved formulas and, operationally,
tactic timeouts.

The objective charges solver work for a solved formula and a fixed timeout
penalty otherwise. Although the objective is presented using runtime, training
uses Z3's deterministic `rlimit` work counter to reduce wall-clock noise. The
learned policy predicts the next tactic and parameter values from the tactic
history, Z3 probes, and a formula representation. Its best formula
representation was SMT-LIB token bag-of-words. Skip-gram lost relevant detail,
while shallow AST features were too sparse for these datasets. The tactic
sequence already applied was the most important part of the state representation.

Training alternates exploration with retraining in a DAgger-style loop. Search
prunes two partial strategies when their transformed formula ASTs are identical,
keeping the one that reached the formula with less work. It first learns many
branch-free candidates, then greedily retains candidates that trade off
individual coverage and coverage of formulas not solved by previously selected
strategies. The final synthesis step builds a decision tree over simple formula
probes or bounded `try` tests. The published final trees use only
`num-consts`, `num-exprs`, and `size` comparisons.

## Experimental tactic vocabulary

FastSMT did not search all of Z3's tactics. It used small logic-specific action
sets with tactic parameters captured as part of an action.

| Logic and datasets | Candidate preprocessing or lowering tactics | Terminal or theory tactics |
|---|---|---|
| QF_BV: Sage2, core | `simplify`, `bit-blast`, `bv1-blast`, `solve-eqs`, `aig`, `max-bv-sharing`, `reduce-bv-size`, `purify-arith`, `propagate-values`, `elim-uncnstr`, `ackermannize_bv` | `smt`, `qfnra-nlsat`, `sat` |
| QF_NRA: hycomp | `simplify`, `bit-blast`, `propagate-values`, `ctx-simplify`, `elim-uncnstr`, `solve-eqs`, `max-bv-sharing`, `nla2bv` | `smt`, `qfnra-nlsat` |
| QF_NIA: leipzig, AProVE | `simplify`, `bit-blast`, `propagate-values`, `ctx-simplify`, `elim-uncnstr`, `solve-eqs`, `max-bv-sharing`, `nla2bv` | `smt`, `qfnra-nlsat` |

The released QF_NIA configuration additionally lists `lia2card` and
`cofactor-term-ite`, although Table 10 of the extended paper omits both.
`lia2card` occurs in some released AProVE candidate strategies;
`cofactor-term-ite` does not. This discrepancy means the artifact, not Table 10
alone, is needed to reproduce the stated search space.

The paper uses `gaussian_elim` and `elim_term_ite` as introductory examples of
what a tactic can be. Neither is an experimental action in Tables 8--10 or the
released configurations. FastSMT therefore provides no direct evidence that a
standalone Gaussian-elimination tactic was useful in these experiments.

## Which tactics survived synthesis

The paper has no individual-tactic ablation. The statements below come from a
fresh structural audit of the released `final_strategies/all` candidate files
and `final_strategies/smt2` master strategies. Occurrence in a successful
learned strategy is evidence that the search retained the tactic, not a causal
measurement of its marginal contribution.

`simplify` occurs in every released learned candidate and every final pipeline
for all five datasets. Its Boolean parameters vary substantially, so FastSMT's
action boundary is "configured simplifier," not a fixed monolithic pass.

The master strategies reduce to the following distinct tactic pipelines after
ignoring repeated decision-tree predicates:

| Dataset | Logic | Distinct final pipelines and recurring structure |
|---|---|---|
| core | QF_BV | One pipeline: `simplify -> bv1-blast -> elim-uncnstr -> smt` |
| Sage2 | QF_BV | Seven pipelines; all use `simplify -> ... -> bit-blast -> ... -> sat`; five use `aig`, five use `max-bv-sharing`, and two use `purify-arith` |
| hycomp | QF_NRA | Seven pipelines; the two major tree leaves are `simplify -> solve-eqs -> elim-uncnstr -> smt` and `simplify -> qfnra-nlsat` |
| leipzig | QF_NIA | Four pipelines; all use `simplify -> ... -> nla2bv -> ... -> smt`, with optional `elim-uncnstr` and a second configured `simplify` |
| AProVE | QF_NIA | Four pipelines; the dominant tree leaf is `simplify -> ctx-simplify -> qfnra-nlsat`; alternatives use `nla2bv` before `smt` or a mixed path |

The branch-leaf counts are not benchmark weights: duplicated leaves arise from
the learned threshold tree. They nevertheless expose the selected action
vocabulary. For example, the hycomp master contains 554 syntactic leaves but
only seven unique pipelines; Sage2 contains 1,237 leaves and seven unique
pipelines. This also qualifies the paper's interpretability claim: the final
program executes without an ML runtime, but the generated decision tree can be
too large for a human to audit comfortably.

Two comparisons are especially relevant to Primo:

- The same logic does not imply the same useful pipeline. QF_BV `core` selected
  a single `bv1-blast/elim-uncnstr/smt` path, whereas QF_BV Sage2 selected a
  portfolio centered on `bit-blast/aig/sat`. QF_NIA leipzig centered on
  `nla2bv/smt`, whereas AProVE mostly selected contextual simplification and
  `qfnra-nlsat`.
- Candidate frequency is not a substitute for final portfolio evidence.
  `solve-eqs` appears in 47 of 62 released leipzig candidates but in none of
  its four final master pipelines. In contrast it is central to one of the two
  major hycomp paths.

## Results and limitations

Against Z3 4.6.2, the best per-formula learned candidate solved 16.8% of its
reported test formulas that Z3 did not, while Z3 alone solved 0.3%. On the
separate, larger evaluation used for the single combined decision-tree
strategy, the corresponding counts were 8.9% learned only and 3.6% Z3 only.
The authors attribute the smaller advantage of the combined strategy to the
limited expressiveness of its branch predicates. Because Tables 2 and 3 cover
different formula populations, their percentages are not a controlled
head-to-head measurement of that compression loss.

The evaluation covers five benchmark families in only QF_BV, QF_NRA, and
QF_NIA. Its train/test claims are transfer within each named dataset, not
family-disjoint generalization. There is no per-tactic ablation, no proof that
an absent tactic is intrinsically useless, and no measurement separating the
effect of a tactic from its parameters, position, and neighboring tactics.
Results depend on Z3 4.6.2 and should not be transferred numerically to Primo.

Formula-AST equivalence is sufficient for FastSMT's search pruning only under
its operational setup. In a formula-valued solver that must preserve models,
proofs, or unsat cores, equal transformed assertions reached by different paths
may carry different converters. Primo must never discard reconstruction state
merely because assertion ASTs match. The paper itself notes that repeatedly
invoking a solver on intermediate formulas loses the connection needed to
recover a model; it avoids this by compiling the policy into one in-solver Z3
strategy.

## Checklist for future Primo portfolio machinery

This paper motivates the following design requirements; it does not implement
them for Primo.

Later and earlier generated configurations are compared, with a stricter focus
on which tactics and parameter values survived selection, in
[](tactic-configuration-evidence.md).

- Define a small per-logic registry of independently configurable,
  formula-valued tactic actions. Make the immutable parameter record part of
  the action identity.
- Represent a branch-free pipeline as a stable serializable value. Record the
  pipeline prefix as strategy-visible state, because FastSMT found tactic
  history more predictive than its formula encodings.
- Export cheap probes over every intermediate goal, beginning with assertion,
  expression, and relevant constant counts. Keep probe cost measurable.
- Record deterministic work counters as well as wall time so strategy search
  is not dominated by measurement noise.
- Preserve the full `Goal`, including reconstruction, when caching or pruning
  equivalent intermediate formulas. Any dominance relation must state which
  converters and diagnostics remain valid.
- Learn or select complete pipelines first, then reduce them to a complementary
  portfolio using both individual performance and union coverage. Add branching
  only after the action set has a demonstrated virtual-best gap.
- Cap and simplify generated decision trees. A selector with seven unique
  pipelines should not require hundreds of duplicated threshold leaves when a
  smaller auditable dispatcher gives comparable validation performance.
- Evaluate both within-family and family-held-out splits. Do not infer that a
  tactic is generally useful from one family-specific master strategy.
