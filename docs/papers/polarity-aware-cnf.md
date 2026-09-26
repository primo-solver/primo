# Polarity-aware CNF in SMT

**Status: Open lead.** The literature has been reviewed; general
Plaisted–Greenbaum encoding is not implemented in Primo. The clausifier emits
full definitions for nested Boolean connectives. Syntactic theory-atom polarity
and shortcuts for asserted roots are different mechanisms.

## Sources and reading scope

- Gabriele Masina, Giuseppe Spallitta, and Roberto Sebastiani,
  *On CNF Conversion for SAT and SMT Enumeration*, JAIR 83 (2025),
  [DOI](https://doi.org/10.1613/jair.1.16870),
  [primary text, arXiv v7](https://arxiv.org/pdf/2303.14971v7).
  Sections used: §2.1 (polarity and projected equivalence), §3.2
  (directional definitions), §4 through Theorem 1 and Remark 1 (NNF and
  enumeration), and §5.3 plus the experiment description at the start of §5.4.
  This is direct SAT/SMT research, including AllSMT experiments with MathSAT
  and TabularAllSMT. Its enumeration results do not establish a speedup for
  Primo's decision-solving workload.
- Jochen Hoenicke and Tanja Schindler, *SMTInterpol with resolution proofs*,
  version 2.5-1272-g2d6d356c, 2023
  [system description](https://ultimate.informatik.uni-freiburg.de/smtinterpol/sysdesc2023.pdf),
  p. 1, explicitly identifies Plaisted–Greenbaum-based CNF conversion.
  The authors' [proof-format explanation](https://ultimate.informatik.uni-freiburg.de/smtinterpol/proof-format.html#cnf-conversion),
  **CNF conversion** and **SMT solving**, exhibits directional clauses followed
  by equality reasoning.
- Original attribution: David A. Plaisted and Steven Greenbaum,
  *A Structure-preserving Clause Form Translation*, Journal of Symbolic
  Computation 2(3), 293–304 (1986),
  [DOI](https://doi.org/10.1016/S0747-7171(86)80028-1).
  **Bibliographic attribution:** the original full text was unavailable during
  review. The algorithmic account below uses the 2025 primary SMT paper.
  Reviewing the original first-order translation remains an open task.

## Which implication is needed

Let a fresh auxiliary `v` replace subformula `F` in the asserted Boolean
structure. For one shared auxiliary per subformula, the required definition is:

| Occurrences of `F` | Sufficient constraint |
| --- | --- |
| Positive only | `v → F` |
| Negative only | `F → v` |
| Both polarities | `v ↔ F` |

The root requirement must still be asserted. Negation reverses polarity;
`And` and `Or` preserve it; implication reverses it for the antecedent.
Polarity is a property of all uses of the subformula, not its current SAT value.
The guarantee is equivalence after existentially eliminating auxiliaries:
`original(atoms) ↔ ∃aux. encoding(atoms, aux)`. A directional auxiliary need
not equal its subformula on every satisfying assignment.

For Primo's chapter example, assert `p ∨ (q ∧ ¬r)` and name `q ∧ ¬r` by `v`.
Its only occurrence is positive, so the following clauses suffice:

```text
p ∨ v
¬v ∨ q
¬v ∨ ¬r
```

Primo currently also emits `v ∨ ¬q ∨ r`, the reverse implication.
In the directional encoding, `p=true, q=true, r=false, v=false` is allowed:
the original assertion holds although `v` differs from `q ∧ ¬r`.
For `p ∨ ¬(q ∧ ¬r)`, the corresponding negative-only encoding instead needs
`p ∨ ¬v` and `v ∨ ¬q ∨ r`.

The 2025 paper's NNF variant separately names positive and negative expansions,
allowing one direction for each. Its linear-size argument relies on DAG
sharing, not tree expansion. Keep that enumeration-oriented variant separate
from ordinary PG and from Primo's earlier root-only experiment.

## Current representation and implementation status

| Mechanism | Primo owner and status |
| --- | --- |
| Cached term-to-literal association | {source}`BooleanClausifier::encode <src/smt/clausifier.cpp#L322-L432>` skips a term once its literal exists. **Implemented**, assuming its full definition is already available. |
| Both directions for nested connectives | {source}`encode_and/or/implies <src/smt/clausifier.cpp#L476-L500>`. **Implemented** ordinary definitional templates. |
| Atom occurrence signs | {source}`collect_theory_atom_polarities <src/smt/clausifier.cpp#L158-L234>`. **Implemented** metadata for theory policies; not a record of emitted definition directions. |
| Positive-root shortcuts | {source}`assert_positive <src/smt/clausifier.cpp#L248-L320>`. **Implemented** root rules, without general PG conversion. |
| Required-value root worklist | {source}`historical prototype <docs/root-polarity-clausification.md>`. **Not adopted**; it retained full nested definitions, so its rejection is not an evaluation of general PG. |
| Directional definition tracking | **Open lead**; the current {source}`term-indexed state <src/smt/clausifier.cpp#L512-L524>` has no emitted-direction mask. |

The clausifier owns the maps and borrows the term manager/backend. A future
direction mask belongs to this same lifetime, indexed by `TermId`; it is not
the theory-atom registry's polarity flag. A signed worklist with at most one
visit per term/direction can keep traversal and clause output linear in the
visited DAG nodes and edges. That is a design expectation, not a measured
bound for an implemented Primo PG encoder.

## Open lead: obligations before implementation

Primo's incremental integration requires the following design work in addition
to the translation described by the papers.

1. Track requested and emitted directions separately from literal allocation.
   If another assertion or assumption later uses a term in the opposite
   polarity, emit the missing direction and recursively satisfy child
   requirements before that use. A cached literal alone is insufficient.
2. Audit {source}`guarded assertions <src/smt/clausifier.cpp#L44-L51>` and pop.
   Retained definition clauses may remain unconditional, but every active
   root must have its required directions. Do not clear a direction mask
   independently of the clauses it describes. Test a shared subformula across
   assertion boundaries, popped scopes, and later opposite-polarity queries.
3. Preserve exact semantic values wherever the SMT layer needs them. A
   Boolean expression passed as an uninterpreted-function argument cannot
   silently acquire the value of an inexact directional proxy. Audit
   {source}`Boolean argument registration <src/smt/clausifier.cpp#L104-L156>`,
   model reconstruction, and externally reused literals; require full
   definitions at such boundaries or prove a separate reconstruction rule.
   SAT observation alone does not confer Boolean equivalence.
4. Keep theory atoms and their interpretations intact. PG reduces Boolean
   definition clauses; it does not authorize omitting theory consistency
   checks or changing the meaning of theory explanations. State the supported
   lowered operators and handle both polarities of shared subterms explicitly.
5. Derive truth-table and projected-model tests for both directions, shared
   DAGs, incremental upgrades, guards, and Bool-valued arguments. Count clauses
   and literal occurrences saved, terms needing both directions, and upgrades.
   Run the applicable fresh solver fuzz gate and independent model validation
   before acceptance.
6. Compare against current full definitions with identical solver settings,
   recording decisions, propagations, theory calls, runtime, memory, and PAR2.
   Fewer clauses can change propagation and search; clause count alone is not
   a performance result. NNF conversion is a separate design/evaluation choice.

Owner unassigned; integration design, implementation, and evaluation have not started.
