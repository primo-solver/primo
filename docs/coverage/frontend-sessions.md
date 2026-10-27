# Frontend and session preservation map

Checked against Primo's command parser, frontend, session dispatcher, retained
engines, and existing tests on 2026-10-01. Current explanations are in
[frontend](../implementation/frontend.md) and [sessions](../implementation/sessions.md).
The historical measurements below are retained at their original revision;
the documentation build does not requalify them.

| Original material | Disposition and destination |
| --- | --- |
| {source}`Streaming checklist <docs/incremental-smt.md#L110-L168>` | Frontend representation and live-command parsing; source/specification links replace the implementation checklist as current exposition. |
| {source}`Rebuild ownership and operations <docs/incremental-smt.md#L169-L226>` | Session representation, journal replay, and scoped trace. |
| {source}`Append/rebuild-on-pop proposal <docs/incremental-smt.md#L227-L277>` | Retained design history. Current retained engines use selector retirement instead; the proposal is not described as shipped behavior. |
| {source}`Supported boundary <docs/incremental-smt.md#L278-L304>` | Session command table, admission restrictions, responses and tests. The old statement that persistent solving is unimplemented is superseded by opt-in assumptions mode. |
| {source}`Session validation procedures <docs/incremental-smt.md#L305-L408>` | Contributor experiment/validation extraction; native-prefix and stream tests must remain separate from whole-session timing. |
| {source}`Definition binding <docs/incremental-smt.md#L409-L422>` | Frontend definition expansion and worked capture example. |
| {source}`Native and rebuild qualifications <docs/incremental-smt.md#L15-L109>`; {source}`later gate <docs/incremental-smt.md#L423-L488>` | Retained dated evidence, including incomplete sessions and limitations of per-query model coverage. |
| {source}`Opt-in assumptions overview <docs/incremental-smt.md#L489>` | Session retained-engine explanation. |
| {source}`Lexical repair and compatibility policy <docs/lexical-conformance.md#L3-L163>` | Frontend token categories, canonical identity, attributes, binding policy, ownership and limitations. Old wrong answers are dated repaired incidents. |
| {source}`External lexical coverage <docs/lexical-conformance.md#L164-L223>` | Retained coverage provenance; external solver tests are not Primo implementation or copied fixtures. |
| {source}`Lexical procedure and qualification <docs/lexical-conformance.md#L224>` | Contributor validation procedure and historical evidence. Character extensions remain distinct from normative conformance. |
| {source}`LRA engine design <docs/assumption-incremental-lra.md#L6-L131>` | Session selectors, preparation, callback lifetime, model visibility, safe preprocessing and costs. MathSAT deletion and preprocessing replay remain unimplemented. |
| {source}`LRA comparisons <docs/assumption-incremental-lra.md#L132>` | Retained revision-specific correctness, native-session and OpenSMT comparisons. |
| {source}`UF engine design <docs/assumption-incremental-uf.md#L14-L100>` | Session retained identities, option restrictions, active roots, and model snapshots. |
| {source}`UF acceptance and evaluation <docs/assumption-incremental-uf.md#L101-L251>` | Retained dated comparison and correctness limits; not current performance evidence. |
| {source}`UF model replay and preparation <docs/assumption-incremental-uf.md#L252>` | Session active lowered roots and new-atom suffix preparation; profiling and earlier checklists remain history. |

## Primary sections inspected

SMT-LIB 2.7 §§3.1–3.6, 4.1.4–4.1.5 and 4.2.2–4.2.3;
MathSAT5 §§4.2–4.3; Bjørner–Fazekas §3.2 and §5. The session chapter records
the implemented subset and the departures from these sources. No external
solver implementation was used as an implementation source.
