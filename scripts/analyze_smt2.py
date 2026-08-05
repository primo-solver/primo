#!/usr/bin/env python3
"""Structural analyzer for SMT-LIB 2 benchmark files.

Why this exists
---------------
Benchmark-shape questions ("is this family difference logic?", "are these
atoms pure-positive?", "can this variable be eliminated?") kept getting
answered with ad-hoc `grep`, and grep kept getting them wrong. Two concrete
failures motivated this script, and both are covered by `--selftest`:

  * `grep -o "(*" file` reports thousands of multiplications on a file with
    none: in a basic regular expression `*` is a quantifier, so the pattern
    matches "zero or more open parens" at every position.
  * A regex that matched only *flat* atoms (`\\([<>]=? [^()]*\\)`) silently
    skipped every nested constraint such as `(<= (+ (* 2 x) y) 5)`. On
    families whose constraints are nested it therefore saw a fraction of each
    variable's occurrences and declared variables one-sided that are not.

The lesson driving the design: **an analyzer must never silently ignore what
it cannot parse.** Anything this script does not understand is reported as
`opaque` and its variables are treated conservatively (two-sided, mixed
polarity), so an unsupported construct produces a weaker claim rather than a
wrong one.

Traversal is iterative throughout -- explicit stacks, no recursion. These
terms nest deeply enough that recursion is a practical stack-overflow risk,
not only a style preference.

Usage
-----
    python3 scripts/analyze_smt2.py FILE...        # per-file report
    python3 scripts/analyze_smt2.py --json FILE    # machine-readable
    python3 scripts/analyze_smt2.py --selftest     # verify the analyzer

What it reports
---------------
declarations by sort; assertion and let-sharing counts; an atom census
classified by shape (unary bound / difference / two-variable / general
linear / opaque); per-atom Boolean polarity (pure-positive, pure-negative,
mixed) computed through not/=>/ite/xor rather than assumed; per-variable
one-sidedness, which is the sound precondition for monotone elimination; and
guard protection, which detects the "value is a don't-care under a flag"
shape (see docs/tropical-matrix-structure.md).
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import sys

# ---------------------------------------------------------------- parsing


def tokenize(src: str):
    """Strip comments and split into parens/atoms. Handles |quoted| symbols."""
    src = re.sub(r';[^\n]*', '', src)
    return re.findall(r'\(|\)|\|[^|]*\||"[^"]*"|[^\s()]+', src)


def parse(tokens):
    """Iterative S-expression parser. Returns a list of top-level forms."""
    top: list = []
    stack: list = []
    cur = top
    for tok in tokens:
        if tok == '(':
            new: list = []
            cur.append(new)
            stack.append(cur)
            cur = new
        elif tok == ')':
            if not stack:
                continue  # unbalanced; tolerate rather than crash
            cur = stack.pop()
        else:
            cur.append(tok)
    return top


def inline_lets(term):
    """Return `term` with all `let` bindings substituted, iteratively.

    Bindings are expanded in dependency order by repeated substitution, which
    terminates because SMT-LIB `let` cannot be recursive.
    """
    # Collect the chain of lets wrapping the body.
    env: dict = {}
    cur = term
    while isinstance(cur, list) and len(cur) == 3 and cur[0] == 'let':
        for binding in cur[1]:
            if isinstance(binding, list) and len(binding) == 2:
                env[binding[0]] = binding[1]
        cur = cur[2]
    body = cur
    if not env:
        return body
    # Expand env entries that refer to other env entries.
    for _ in range(len(env) + 1):
        changed = False
        for name, value in list(env.items()):
            refs = False
            stack = [value]
            while stack:
                node = stack.pop()
                if isinstance(node, str):
                    if node in env and node != name:
                        refs = True
                        break
                else:
                    stack.extend(node)
            if refs:
                env[name] = substitute(value, env, skip=name)
                changed = True
        if not changed:
            break
    return substitute(body, env)


def substitute(term, env, skip=None):
    """Iteratively replace names bound in `env`. Returns a fresh tree."""
    if isinstance(term, str):
        repl = env.get(term)
        if repl is None or term == skip:
            return term
        return copy_tree(repl)
    root: list = [None] * len(term)
    work = [(term, root)]
    while work:
        src, dst = work.pop()
        for i, child in enumerate(src):
            if isinstance(child, str):
                repl = env.get(child)
                if repl is None or child == skip:
                    dst[i] = child
                elif isinstance(repl, str):
                    dst[i] = repl
                else:
                    sub: list = [None] * len(repl)
                    dst[i] = sub
                    work.append((repl, sub))
            else:
                sub = [None] * len(child)
                dst[i] = sub
                work.append((child, sub))
    return root


def copy_tree(term):
    if isinstance(term, str):
        return term
    root: list = [None] * len(term)
    work = [(term, root)]
    while work:
        src, dst = work.pop()
        for i, child in enumerate(src):
            if isinstance(child, str):
                dst[i] = child
            else:
                sub: list = [None] * len(child)
                dst[i] = sub
                work.append((child, sub))
    return root


# ------------------------------------------------------- linear normal form

NUM = re.compile(r'^-?\d+(\.\d+)?$')
RELS = {'<=', '<', '>=', '>', '=', 'distinct'}
OPAQUE = object()


def linear_form(term, reals):
    """Return {var: coeff} plus a constant, or OPAQUE if not linear.

    OPAQUE is returned for anything unsupported. Callers must treat OPAQUE
    conservatively; that contract is the point of this analyzer.
    """
    coeffs: dict = collections.defaultdict(float)
    const = 0.0
    # stack of (node, sign multiplier)
    stack = [(term, 1.0)]
    while stack:
        node, mul = stack.pop()
        if isinstance(node, str):
            if NUM.match(node):
                const += mul * float(node)
            elif node in reals:
                coeffs[node] += mul
            else:
                return OPAQUE, 0.0
            continue
        if not node:
            return OPAQUE, 0.0
        head = node[0]
        if head == '+':
            for arg in node[1:]:
                stack.append((arg, mul))
        elif head == '-':
            if len(node) == 2:
                stack.append((node[1], -mul))
            else:
                stack.append((node[1], mul))
                for arg in node[2:]:
                    stack.append((arg, -mul))
        elif head == '*':
            nums = [a for a in node[1:] if isinstance(a, str) and NUM.match(a)]
            rest = [a for a in node[1:] if not (isinstance(a, str) and NUM.match(a))]
            if len(rest) > 1:
                return OPAQUE, 0.0   # nonlinear
            factor = 1.0
            for n in nums:
                factor *= float(n)
            if not rest:
                const += mul * factor
            else:
                stack.append((rest[0], mul * factor))
        elif head == '/':
            if len(node) == 3 and isinstance(node[2], str) and NUM.match(node[2]):
                d = float(node[2])
                if d == 0:
                    return OPAQUE, 0.0
                stack.append((node[1], mul / d))
            else:
                return OPAQUE, 0.0
        else:
            return OPAQUE, 0.0
    return dict(coeffs), const


def is_boolean_term(eq_node, bools):
    """True if an `=` node is a Boolean equivalence rather than an arithmetic one.

    Decided by looking for a Bool-sorted variable or a Boolean connective on
    either side. Conservative: when unsure this returns False, so the node is
    handled by the arithmetic path, which is itself conservative.
    """
    connectives = {'and', 'or', 'not', '=>', 'xor', 'true', 'false', 'ite'}
    stack = list(eq_node[1:])
    while stack:
        node = stack.pop()
        if isinstance(node, str):
            if node in bools or node in ('true', 'false'):
                return True
        else:
            if node and isinstance(node[0], str) and node[0] in connectives:
                return True
            stack.extend(node)
    return False


def classify_atom(term, reals):
    """Classify a relational atom by shape."""
    if not isinstance(term, list) or not term or term[0] not in RELS:
        return 'opaque', None
    if term[0] == 'distinct' or len(term) != 3:
        return 'opaque', None
    lhs, _ = linear_form(term[1], reals)
    rhs, _ = linear_form(term[2], reals)
    if lhs is OPAQUE or rhs is OPAQUE:
        return 'opaque', None
    net: dict = collections.defaultdict(float)
    for v, c in lhs.items():
        net[v] += c
    for v, c in rhs.items():
        net[v] -= c
    net = {v: c for v, c in net.items() if abs(c) > 1e-12}
    n = len(net)
    if n == 0:
        return 'ground', net
    if n == 1:
        return 'unary-bound', net
    if n == 2:
        a, b = list(net.values())
        if abs(a + b) < 1e-12 and abs(abs(a) - 1.0) < 1e-12:
            return 'difference', net
        return 'two-variable', net
    return 'general-linear', net


# ------------------------------------------------------------- the analysis

BOOL_OPS = {'and', 'or', 'not', '=>', 'ite', 'xor', '=>', 'let'}


def analyze(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    forms = parse(tokenize(src))

    decls = collections.Counter()
    reals, bools = set(), set()
    for form in forms:
        if isinstance(form, list) and form and form[0] in ('declare-fun', 'declare-const'):
            name = form[1]
            sort = form[-1] if isinstance(form[-1], str) else 'other'
            arity0 = (form[0] == 'declare-const') or (isinstance(form[2], list) and not form[2])
            decls[sort] += 1
            if arity0 and sort == 'Real':
                reals.add(name)
            elif arity0 and sort == 'Bool':
                bools.add(name)

    asserts = [f[1] for f in forms
               if isinstance(f, list) and len(f) > 1 and f[0] == 'assert']
    let_defs = len(re.findall(r'\(let\s*\(', src))

    atom_shapes = collections.Counter()
    atom_polarity: dict = collections.defaultdict(set)
    var_sides: dict = collections.defaultdict(set)
    var_occurrences = collections.Counter()
    opaque_atoms = 0
    max_depth = 0
    ite_count = 0

    # occurrence -> is it protected by a guard literal? (don't-care shape)
    guard_prot = collections.Counter()
    guard_total = collections.Counter()

    for body in asserts:
        body = inline_lets(body)
        # (node, polarity, guards) ; polarity 1 positive, -1 negative, 0 mixed
        stack = [(body, 1, frozenset(), 0)]
        while stack:
            node, pol, guards, depth = stack.pop()
            max_depth = max(max_depth, depth)
            if isinstance(node, str):
                if node in reals:
                    var_occurrences[node] += 1
                continue
            if not node:
                continue
            head = node[0] if isinstance(node[0], str) else None
            if head == 'not':
                for arg in node[1:]:
                    stack.append((arg, -pol if pol else 0, guards, depth + 1))
                continue
            if head == 'ite':
                ite_count += 1
                stack.append((node[1], 0, guards, depth + 1))
                for arg in node[2:]:
                    stack.append((arg, pol, guards, depth + 1))
                continue
            if head in ('xor', 'distinct'):
                for arg in node[1:]:
                    stack.append((arg, 0, guards, depth + 1))
                continue
            if head == '=>':
                for arg in node[1:-1]:
                    stack.append((arg, -pol if pol else 0, guards, depth + 1))
                stack.append((node[-1], pol, guards, depth + 1))
                continue
            if head == 'or':
                kids = node[1:]
                lits = {k for k in kids if isinstance(k, str) and k in bools}
                for k in kids:
                    sib = lits - ({k} if isinstance(k, str) else set())
                    stack.append((k, pol, guards | sib, depth + 1))
                continue
            if head == 'and':
                kids = node[1:]
                lits = {k[1] for k in kids
                        if isinstance(k, list) and len(k) == 2 and k[0] == 'not'
                        and isinstance(k[1], str) and k[1] in bools}
                for k in kids:
                    stack.append((k, pol, guards | lits, depth + 1))
                continue
            if head == '=' and len(node) == 3 and is_boolean_term(node, bools):
                # Boolean equivalence, not an arithmetic atom. Both sides sit
                # under an iff, so neither has a definite polarity.
                for arg in node[1:]:
                    stack.append((arg, 0, guards, depth + 1))
                continue
            if head in RELS:
                shape, net = classify_atom(node, reals)
                atom_shapes[shape] += 1
                key = sexpr(node)
                atom_polarity[key].add(pol)
                if shape == 'opaque' or net is None:
                    opaque_atoms += 1
                    # conservative: every real under here is two-sided
                    sub = [node]
                    while sub:
                        n2 = sub.pop()
                        if isinstance(n2, str):
                            if n2 in reals:
                                var_sides[n2].update({'hi', 'lo'})
                                var_occurrences[n2] += 1
                        else:
                            sub.extend(n2)
                    continue
                is_eq = (node[0] == '=')
                for v, c in net.items():
                    var_occurrences[v] += 1
                    guard_total[v] += 1
                    if v[:-1] + 'm' in guards or v in guards:
                        guard_prot[v] += 1
                    if is_eq or pol == 0:
                        var_sides[v].update({'hi', 'lo'})
                        continue
                    # net ⋈ 0 with ⋈ in {<=,<} means larger positive-coeff
                    # vars are harder to satisfy -> bounded above.
                    up = node[0] in ('<=', '<')
                    if pol < 0:
                        up = not up
                    if (c > 0) == up:
                        var_sides[v].add('hi')
                    else:
                        var_sides[v].add('lo')
                continue
            for arg in (node[1:] if head else node):
                stack.append((arg, pol, guards, depth + 1))

    pure_pos = sum(1 for k, v in atom_polarity.items() if v == {1})
    pure_neg = sum(1 for k, v in atom_polarity.items() if v == {-1})
    mixed = len(atom_polarity) - pure_pos - pure_neg
    one_sided = [v for v in reals if len(var_sides.get(v, set())) == 1]
    two_sided = [v for v in reals if len(var_sides.get(v, set())) == 2]
    unconstrained = [v for v in reals if not var_sides.get(v)]
    fully_guarded = [v for v in guard_total if guard_total[v] and guard_prot[v] == guard_total[v]]

    return {
        'file': path,
        'declarations': dict(decls),
        'reals': len(reals),
        'bools': len(bools),
        'assertions': len(asserts),
        'let_bindings': let_defs,
        'max_term_depth': max_depth,
        'ite_count': ite_count,
        'atom_shapes': dict(atom_shapes),
        'distinct_atoms': len(atom_polarity),
        'atoms_pure_positive': pure_pos,
        'atoms_pure_negative': pure_neg,
        'atoms_mixed_polarity': mixed,
        'opaque_atoms': opaque_atoms,
        'reals_one_sided': len(one_sided),
        'reals_two_sided': len(two_sided),
        'reals_unconstrained': len(unconstrained),
        'reals_fully_guard_protected': len(fully_guarded),
        'one_sided_examples': sorted(one_sided)[:8],
    }


def sexpr(term):
    if isinstance(term, str):
        return term
    out = []
    stack = [(term, 0)]
    while stack:
        node, state = stack.pop()
        if isinstance(node, str):
            out.append(node)
            continue
        out.append('(')
        parts = list(node)
        stack.append((')', 0))
        for p in reversed(parts):
            stack.append((p, 0))
    return ' '.join(out).replace('( ', '(').replace(' )', ')')


def report(info):
    print(f"=== {info['file']}")
    print(f"  declarations      : {info['declarations']}  "
          f"(Real 0-ary {info['reals']}, Bool 0-ary {info['bools']})")
    print(f"  assertions        : {info['assertions']}   let bindings: {info['let_bindings']}"
          f"   max depth: {info['max_term_depth']}   ite: {info['ite_count']}")
    print(f"  atom shapes       : {info['atom_shapes']}")
    print(f"  distinct atoms    : {info['distinct_atoms']}  "
          f"(pure+ {info['atoms_pure_positive']}, pure- {info['atoms_pure_negative']}, "
          f"mixed {info['atoms_mixed_polarity']})")
    if info['opaque_atoms']:
        print(f"  OPAQUE atoms      : {info['opaque_atoms']} "
              f"(not understood; their variables forced two-sided)")
    print(f"  real variables    : one-sided {info['reals_one_sided']}, "
          f"two-sided {info['reals_two_sided']}, unconstrained {info['reals_unconstrained']}")
    print(f"  guard-protected   : {info['reals_fully_guard_protected']} reals have every "
          f"arithmetic occurrence under a guard flag")
    if info['one_sided_examples']:
        print(f"  one-sided e.g.    : {info['one_sided_examples']}")


# ------------------------------------------------------------------ selftest

def selftest():
    import tempfile, os
    failures = []

    def check(name, smt, **expect):
        fd, path = tempfile.mkstemp(suffix='.smt2')
        os.write(fd, smt.encode())
        os.close(fd)
        try:
            got = analyze(path)
        finally:
            os.unlink(path)
        for key, want in expect.items():
            if got.get(key) != want:
                failures.append(f"{name}: {key} = {got.get(key)!r}, expected {want!r}")

    # Regression for the grep bug: nested arithmetic must be seen, and x must
    # come out two-sided because it is pushed both ways.
    check('nested-arithmetic',
          '(set-logic QF_LRA)(declare-fun x () Real)(declare-fun y () Real)'
          '(assert (<= (+ (* 2 x) y) 5))(assert (>= x 1))',
          reals=2, reals_one_sided=1, reals_two_sided=1)

    # The pure-literal counterexample: x is two-sided, so monotone elimination
    # correctly does NOT apply.
    check('pure-literal-counterexample',
          '(set-logic QF_LRA)(declare-fun x () Real)(declare-fun d () Bool)'
          '(assert (or (>= x 10) d))(assert (<= x 0))',
          reals=1, reals_two_sided=1, reals_one_sided=0)

    # Polarity through `not`: the atom occurs only negatively.
    check('polarity-negative',
          '(set-logic QF_LRA)(declare-fun x () Real)'
          '(assert (not (<= x 3)))',
          atoms_pure_negative=1, atoms_pure_positive=0)

    # Guard protection, both polarities: positive disjunct sibling and
    # negative conjunct sibling must both count.
    check('guard-both-polarities',
          '(set-logic QF_LRA)(declare-fun am () Bool)(declare-fun ac () Real)'
          '(declare-fun bm () Bool)(declare-fun bc () Real)'
          '(assert (or am (and (not bm) (>= ac bc))))',
          reals_fully_guard_protected=2)

    # Nonlinear must be OPAQUE, not silently dropped.
    check('nonlinear-is-opaque',
          '(set-logic QF_NRA)(declare-fun x () Real)(declare-fun y () Real)'
          '(assert (<= (* x y) 4))',
          opaque_atoms=1, reals_two_sided=2)

    # Shape classification.
    check('difference-shape',
          '(set-logic QF_LRA)(declare-fun x () Real)(declare-fun y () Real)'
          '(assert (<= (- x y) 3))',
          atom_shapes={'difference': 1})

    # A Boolean equivalence must not be mistaken for an arithmetic atom.
    check('boolean-equivalence-not-arithmetic',
          '(set-logic QF_LRA)(declare-fun am () Bool)(declare-fun bm () Bool)'
          '(declare-fun cm () Bool)(declare-fun x () Real)'
          '(assert (= am (or bm cm)))(assert (<= x 1))',
          opaque_atoms=0, atom_shapes={'unary-bound': 1})

    # let must be inlined, not counted as an opaque head.
    check('let-inlining',
          '(set-logic QF_LRA)(declare-fun x () Real)'
          '(assert (let ((a (<= x 1))) a))',
          atom_shapes={'unary-bound': 1}, opaque_atoms=0)

    if failures:
        print("SELFTEST FAILURES:")
        for f in failures:
            print("  -", f)
        return 1
    print("selftest: all checks passed")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='*')
    ap.add_argument('--json', action='store_true', help='machine-readable output')
    ap.add_argument('--selftest', action='store_true', help='verify the analyzer itself')
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.files:
        ap.error('give at least one file, or --selftest')
    out = []
    for path in args.files:
        info = analyze(path)
        out.append(info)
        if not args.json:
            report(info)
    if args.json:
        print(json.dumps(out, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
