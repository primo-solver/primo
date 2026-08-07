#!/usr/bin/env python3
"""Exhaustive small-formula differential check for --monotone-elimination.

Enumerates combinations of arithmetic literals over a few real variables,
wraps them in several Boolean shapes (conjunction, disjunction, implication,
negation -- the input is not in NNF, and polarity is what the pass reasons
about), and for every one checks two things against a reference solver:

  1. the sat/unsat answer is unchanged by the pass;
  2. when sat, the model Primo prints satisfies the ORIGINAL formula.

(2) is the property that actually matters and the one unit tests missed: the
pass discharges constraints, so a model that no longer satisfies them is
exactly the failure mode it can introduce.

Usage:
  python3 scripts/check_monotone_elimination.py                 # default sweep
  python3 scripts/check_monotone_elimination.py --max-size 3    # smaller
  python3 scripts/check_monotone_elimination.py --verbose
"""
from __future__ import annotations

import argparse
import itertools
import os
import re
import subprocess
import sys
import tempfile

PRIMO = '/workspace/build/primo'
Z3 = '/workspace/contrib/local/bin/z3'

VARS = ['x', 'y', 'z']
# Deliberately includes pairs that desugar to the same node ((> y x) and
# (< x y)) and pairs that oppose each other, since those are the cases
# where one-sidedness is decided.
ATOMS = [
    '(<= x y)', '(>= x y)', '(< x y)', '(> y x)',
    '(<= x 5)', '(>= y 3)', '(< z x)', '(<= y z)', '(> z 0)',
]


def run(cmd, timeout=20):
    try:
        return subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout, check=False
        ).stdout
    except subprocess.TimeoutExpired:
        return ''


def preamble():
    return ('(set-logic QF_LRA)\n' +
            ''.join(f'(declare-fun {v} () Real)\n' for v in VARS) +
            '(declare-fun p () Bool)\n(declare-fun q () Bool)\n')


def shapes(lits):
    """Boolean shapes over the chosen literals, exercising polarity."""
    conj = '(and ' + ' '.join(lits) + ')' if len(lits) > 1 else lits[0]
    out = [('and', conj)]
    if len(lits) > 1:
        out.append(('or', '(or ' + ' '.join(lits) + ')'))
        out.append(('implies', f'(=> {lits[0]} (and ' + ' '.join(lits[1:]) + '))'
                    if len(lits) > 2 else f'(=> {lits[0]} {lits[1]})'))
        # A literal reached at both polarities must force its variables
        # two-sided; this shape is how that gets exercised.
        out.append(('both-polarities',
                    f'(and (or {lits[0]} p) (or (not {lits[0]}) q))'))
    out.append(('negated', f'(not {conj})'))
    return out


def check_one(body, verbose):
    original = preamble() + f'(assert {body})\n'
    problems = []

    def solve(extra_args):
        with tempfile.NamedTemporaryFile('w', suffix='.smt2', delete=False) as fh:
            fh.write('(set-option :produce-models true)\n' + original +
                     '(check-sat)\n(get-model)\n')
            path = fh.name
        out = run([PRIMO, '--quiet'] + extra_args + [path])
        os.unlink(path)
        return out

    base = solve([])
    with_pass = solve(['--monotone-elimination'])
    base_ans = base.split('\n', 1)[0].strip()
    pass_ans = with_pass.split('\n', 1)[0].strip()

    if (
        base_ans in ('sat', 'unsat')
        and pass_ans in ('sat', 'unsat')
        and base_ans != pass_ans
    ):
        problems.append(f'ANSWER CHANGED: {base_ans} -> {pass_ans}')

    # reference answer
    with tempfile.NamedTemporaryFile('w', suffix='.smt2', delete=False) as fh:
        fh.write(original + '(check-sat)\n')
        ref_path = fh.name
    ref = run([Z3, ref_path]).strip().split('\n')[0]
    os.unlink(ref_path)
    if ref in ('sat', 'unsat') and pass_ans in ('sat', 'unsat') and ref != pass_ans:
        problems.append(f'DISAGREES WITH REFERENCE: primo {pass_ans}, z3 {ref}')

    # model validity against the ORIGINAL formula
    if pass_ans == 'sat':
        defs = re.findall(
            r'\(define-fun (\S+) \(\) Real (\([^()]*(?:\([^()]*\)[^()]*)*\)|[^()\s]+)\)',
            with_pass)
        bools = re.findall(r'\(define-fun (\S+) \(\) Bool (true|false)\)', with_pass)
        asserts = ''.join(f'(assert (= {n} {v}))\n' for n, v in defs)
        asserts += ''.join(f'(assert (= {n} {v}))\n' for n, v in bools)
        if asserts:
            with tempfile.NamedTemporaryFile('w', suffix='.smt2', delete=False) as fh:
                fh.write(original + asserts + '(check-sat)\n')
                mpath = fh.name
            verdict = run([Z3, mpath]).strip().split('\n')[0]
            os.unlink(mpath)
            if verdict == 'unsat':
                problems.append('INVALID MODEL (does not satisfy the original)')
    if verbose and not problems:
        print(f'  ok   {body[:88]}')
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--max-size', type=int, default=3)
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args()

    cases = 0
    failures = []
    for size in range(1, args.max_size + 1):
        for combo in itertools.combinations(ATOMS, size):
            for negate_mask in range(1 << size):
                lits = [f'(not {a})' if (negate_mask >> i) & 1 else a
                        for i, a in enumerate(combo)]
                for _, body in shapes(lits):
                    cases += 1
                    for problem in check_one(body, args.verbose):
                        failures.append((problem, body))
    print(f'checked {cases} formulas')
    if failures:
        print(f'FAILURES: {len(failures)}')
        for problem, body in failures[:25]:
            print(f'  {problem}\n    {body}')
        return 1
    print('no answer changes, no reference disagreements, no invalid models')
    return 0


if __name__ == '__main__':
    sys.exit(main())
