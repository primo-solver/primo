# LLM2SMT retrospective

## Status

LLM2SMT, branch `lra`, is the predecessor of Primo.

Repository:

```text
https://github.com/MikolasJanota/llm2smt/tree/lra
```

## How it may be used

Allowed:

- compare high-level architecture
- inspect old benchmark workflow
- identify missing regression tests
- understand previous failure modes
- compare command-line behavior
- document lessons learned

Not allowed:

- copying code
- copying tests verbatim
- copying grammar files
- copying documentation text
- porting modules mechanically

## Questions to answer later

- Which components were useful?
- Which parts became hard to maintain?
- Which invariants were missing?
- Which tests were absent?
- Which prompts led to unsystematic development?
- Which scripts should be redesigned?
- Which algorithms need stronger paper-level documentation?
