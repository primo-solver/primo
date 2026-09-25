# Clause forgetting: correctness and termination

Theory lemmas can be forgotten under an appropriate search strategy. The
obligation is more specific than remembering that the lemma can be derived
again: forgetting and restarting must still permit progress. This note gives
the literature basis for an open investigation; Primo's retention policy is
unchanged. The [Yices survey](yices.md) explains the related caching mechanism.

## Reading guide and sources

### Nieuwenhuis, Oliveras, and Tinelli (2006)

Robert Nieuwenhuis, Albert Oliveras, and Cesare Tinelli, *Solving SAT and SAT
Modulo Theories: From an Abstract Davis–Putnam–Logemann–Loveland Procedure to
DPLL(T)*, Journal of the ACM, 2006.
[Author PDF](https://homepage.cs.uiowa.edu/~tinelli/papers/NieOT-JACM-06.pdf).
Read §3.3, especially Definition 3.2, Theorem 3.7, and Property 3.9;
§§4.2 and 4.4; and §5.2 for explanation ordering.

The state contains an assignment trail and a clause database. `T-Forget`
permits deleting `C` when the remaining formula entails `C` **modulo the
theory**. This preserves theory equivalence. Theorem 3.7 separately establishes
termination under its finite-atom setting, excluding infinite learn/forget-only
activity and constraining successive restart intervals: later intervals must
either contain more basic search steps or learn a new clause never subsequently
forgotten. These are sufficient conditions, not a characterization of every
terminating solver.

Section 4.4 directly discusses forgetting theory lemmas. It distinguishes the
original theory-conflict explanation from the backjump clause learned after
conflict analysis. Retaining the original explanation as well is optional in
that strategy. Section 5.2 gives dependence on earlier trail literals as a
sufficient condition for terminating backwards resolution, avoiding cycles.

**Primo status: deferred policy audit.** This is the most direct SMT source;
it does not certify Primo's particular backend, atom-generation, or restart policy.

### Blanchette, Fleury, Lammich, and Weidenbach (2018)

Jasmin Christian Blanchette, Mathias Fleury, Peter Lammich, and Christoph
Weidenbach, *A Verified SAT Solver Framework with Learn, Forget, Restart,
and Incrementality*, Journal of Automated Reasoning 61, 333–365, 2018.
[DOI](https://doi.org/10.1007/s10817-018-9455-7),
[journal PDF](https://pure.mpg.de/rest/items/item_2602035_3/component/file_2602034/content).
Read the definitions and results in §§3.4–3.5, 4.2–4.3 (including Theorem 10),
and 4.5–4.6.
Section numbers here refer to the journal version, not the shorter three-author
conference paper with the same title.

This Isabelle/HOL formalization separates the trail, initial clauses, learned
clauses, and current conflict. Its concrete `Forget` rule protects clauses
serving as trail reasons. Section 4.5 gives a proved strategy that permits
deleting any subset of learned clauses at a restart, forbids forgetting between
restarts, and requires an unbounded lower bound on newly learned clauses
between successive restarts. Finitely many possible duplicate-free clauses,
together with the underlying strategy's no-relearning result, establish
termination. Section 3.5 also studies restart bounds measured in transitions.

**Primo status: reference for proof obligations; not adopted.** The theorem
concerns its specified propositional calculus. It is not a verification of
CaDiCaL or a license to delete SMT-generated clauses under arbitrary schedules.
Its incremental extension does not by itself establish scoped SMT deletion.

### Gstrein et al. (2025)

Bernhard Gstrein, Florian Pollitt, André Schidler, Mathias Fleury, and Armin
Biere, *Learn to Unlearn*, SAT 2025, LIPIcs 341, 14:1–14:12.
[Paper and bibliography](https://doi.org/10.4230/LIPIcs.SAT.2025.14),
[PDF](https://drops.dagstuhl.de/storage/00lipics/lipics-vol341-sat2025/LIPIcs.SAT.2025.14/LIPIcs.SAT.2025.14.pdf).
Read §§1–8 and Appendix A; the additional plots in Appendix B were not used
to select a Primo policy.

The experiments vary clause retention in Kissat on the 400 SAT Competition
2024 instances. Keeping everything and periodically removing everything both
lose performance overall; the latter particularly hurts UNSAT solving.
Size/LBD thresholds and recent use during conflict analysis are evaluated
as selection criteria. LBD measures the distinct decision levels represented
in a clause, with scoring conventions discussed in §5. The required metadata
includes clause scores, use markers, and a reduction schedule.

Appendix A explains why clauses currently used as reasons need protection;
the experimental baseline backtracks to level zero before reduction to remove
that source of bias. These are empirical results, not a termination theorem
or measurements of Primo. **Primo status: deferred experimental motivation.**
Do not transfer a numerical threshold or implement a SAT clause database in
Primo on the strength of these results.

## Why the original conflict clause need not survive

As an illustrative derivation, suppose the Boolean input contains
`p ⇒ a` and `p ⇒ b`, while the theory rules out `a ∧ b`:

```text
input clauses:         ¬p ∨ a       ¬p ∨ b
theory explanation:    ¬a ∨ ¬b
resolve on a:          ¬p ∨ ¬b
resolve on b:          ¬p
```

Keeping `¬p` prevents repeating that decision. Keeping `¬a ∨ ¬b` additionally
exposes the incompatible theory combination when reached another way. The
backjump clause need not be the same clause or block every assignment blocked
by the original explanation. This example explains local progress; it is not
a proof that arbitrary subsequent deletion and restarts terminate.

For a simple outer loop over a fixed finite set of atoms, retaining a clause
that excludes each failed candidate assignment gives a direct finite-search
argument. Erasing all such progress before restarting invalidates that argument.
The theory's ability to rediscover an inconsistency preserves a checking
capability; it does not stop the loop from rediscovering it forever.

## Representation and current Primo boundary

| State or operation | Owner and inspected behavior |
| --- | --- |
| Boolean trail, learned clauses, watches, and reduction | CaDiCaL behind {source}`CadicalBackend <src/sat/cadical_backend.cpp#L35-L85>`. Primo does not implement CDCL or expose arbitrary clause deletion through {source}`IpasirUpBackend <src/sat/ipasir_up.hpp#L81-L107>`. |
| Explicit theory-conflict clauses | The {source}`EUF callback <src/smt/solver_engines.cpp#L1892-L1916>` and {source}`LRA callback <src/smt/solver_engines.cpp#L7227-L7237>` request non-forgettable external clauses in the API variant supporting the flag. This is not a switch applied to every route or every clause in the solver. |
| Propagation explanations | {source}`EUF reasons <src/smt/solver_engines.cpp#L1863-L1890>` and {source}`LRA reasons <src/smt/solver_engines.cpp#L7147-L7179>` are streamed on request; their data must survive every legal callback request. The [Yices note](yices.md#what-primo-and-its-backend-already-do) records the installed backend's reason-retention contract and the unresolved default documentation discrepancy. |
| Clause retention control | {source}`ExternalPropagator <src/sat/ipasir_up.hpp#L40-L73>` carries a forgettable flag for external clauses. The adapter does not expose a corresponding Primo-level per-reason retention selector. Eligibility for backend deletion would not establish actual deletion timing. |
| Component routing | The {source}`component bridge <src/smt/solver_engines.cpp#L9935-L9955>` consumes the callback result into `TheoryCheckResult`; it does not preserve the local `forgettable` value in that result. Audit the consuming route before proposing a common policy. |
| Component external-clause policy | {source}`ComponentDispatcher::cb_has_external_clause <src/smt/component_dispatcher.cpp#L240-L248>` sets `is_forgettable = true`. This differs from the dedicated EUF/LRA callbacks. It establishes eligibility, not measured deletion or a proved progress policy. |

Deletion of a SAT clause, release of a C++ explanation vector, and reclamation
of core terms are distinct lifetime operations. A clause can be logically
redundant and still be required as a live explanation. Conversely, protecting
live explanations says nothing about global progress after repeated restarts.

## Design obligations for the open lead

The following are proposed Primo checks, not claims that these papers prove
the existing implementation correct:

1. **Classify eligible clauses.** Separate input assertions, Boolean definitions,
   scope-selector clauses, theory-valid lemmas, and consequences depending on
   assertions. Preserve original constraints and every necessary antecedent or
   scope guard. An explanation is not an unconditional assertion of its conclusion.
2. **Preserve reason lifetime.** Map creation, materialization, backend ownership,
   live use, backtracking, reconnect, and destruction. Deletion must respect the
   installed IPASIR-UP contract and leave requested reasons available and valid.
3. **State a progress argument.** Choose a theorem's conditions and map actual
   restart/reduction/learning operations to them. A terminating SAT call alone
   does not prove termination of repeated SMT refinement calls. Start with a
   fixed-atom QF_UF case; fresh atoms, arithmetic search, quantifiers, and growing
   incremental sessions need separate bounds or an explicit limitation.
4. **Measure before changing defaults.** Attribute retained memory and repeated
   theory conflicts to clause classes. Compare retention policies with the same
   backend and workload; report memory, runtime/PAR2, rederived lemmas, and
   repeated conflicts. A lower clause count alone is not an improvement.

There is no selected deletion algorithm or complexity improvement claim.
Candidate tests must exercise repeated conflict/restart patterns, live reason
requests, deletion followed by rederivation, and `push`/`pop`/reconnect. Such
tests can expose cycles; passing a finite test cannot replace the progress
argument. Any implementation also needs the applicable fuzz, model-validation,
sanitizer, documentation, and reproducible evaluation gates.
