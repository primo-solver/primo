# Fast and Flexible Difference Constraint Propagation for DPLL(T)

## Citation

Scott Cotton, Oded Maler:  
Fast and Flexible Difference Constraint Propagation for DPLL(T).

## Role in Primo

This paper is the later basis for difference-logic propagation.

## What we use later

- constraints of form `x - y <= c`
- weighted graph representation
- negative-cycle conflict detection
- propagation of implied bounds

## Initial representation

```text
x - y <= c
means
x <= y + c

Graph edge:
y -> x with weight c
```

## Not implemented initially

The initial Primo scaffold does not need a full difference-logic solver.

## Future milestones

- DL0: parse difference constraints
- DL1: graph construction
- DL2: negative-cycle detection
- DL3: conflict explanation from cycle edges
- DL4: model via potentials
- DL5: flexible propagation
