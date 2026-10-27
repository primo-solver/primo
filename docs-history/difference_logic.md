# Difference logic

Difference logic is a future target.

```text
x - y <= c
means
x <= y + c

Graph encoding:
edge y -> x with weight c
```

Unsatisfiability is detected by a negative cycle.

Example future test:

```lisp
(set-logic QF_IDL)
(declare-fun x () Int)
(declare-fun y () Int)
(assert (<= (- x y) 3))
(assert (<= (- y x) -4))
(check-sat)
```

Expected:

```text
unsat
```

Reason:

```text
x - y <= 3
y - x <= -4
sum gives 0 <= -1
```
