#!/usr/bin/env python3
"""Check an unsat answer of Primo's quantified UF route with cvc5 pools.

Primo records the instantiation tuples and Skolem lemmas it used
(--quantifier-export-pools). This script adds them to the original input as
cvc5 pools and runs cvc5 with --pool-inst:

  - every command of the input is kept; set-logic becomes ALL, which tuple
    pools need, and declarations move before the assertions;
  - every quantifier whose variable sorts have a pool gets a :pool annotation;
  - Primo's Skolem and Herbrand constants are declared and its Skolem lemmas
    asserted.

Pools and annotations do not change what the problem means, and a Skolem lemma
naming a fresh constant keeps a satisfiable problem satisfiable whatever
formula it names. An unsat answer from cvc5 therefore confirms the input
without trusting Primo's instantiation or printing. As a self-check, removing
the added annotations must give back the original commands.

Verdicts: confirmed (cvc5 unsat), refuted (cvc5 sat: Primo's unsat is wrong,
exit status 3), inconclusive (anything else from cvc5), not-checked (Primo did
not answer unsat, or wrote no export), error (the input could not be rewritten).

With --force the check also runs when Primo did not answer unsat, as a
negative control on satisfiable inputs: the verdict is control, or
control-unsat (exit status 3) if cvc5 answers unsat, which would mean the
additions changed the problem.
"""

import argparse
import json
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SIMPLE_SYMBOL = re.compile(r"[A-Za-z~!@$%^&*_+=<>.?/-][0-9A-Za-z~!@$%^&*_+=<>.?/-]*\Z")
UNSUPPORTED_COMMANDS = {"push", "pop", "reset", "reset-assertions", "check-sat-assuming"}
DROPPED_COMMANDS = {"check-sat", "exit", "echo", "set-logic"}
TERM_COMMANDS = {"assert", "define-fun", "define-fun-rec", "define-funs-rec"}


def tokenize(text):
    """SMT-LIB tokens: parentheses and atoms, with comments removed."""
    tokens = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        if char.isspace():
            index += 1
        elif char == ';':
            end = text.find('\n', index)
            index = length if end < 0 else end
        elif char in '()':
            tokens.append(char)
            index += 1
        elif char == '"':
            end = index + 1
            while True:
                end = text.find('"', end)
                if end < 0:
                    raise ValueError('unterminated string literal')
                if end + 1 < length and text[end + 1] == '"':
                    end += 2
                    continue
                break
            tokens.append(text[index:end + 1])
            index = end + 1
        elif char == '|':
            end = text.find('|', index + 1)
            if end < 0:
                raise ValueError('unterminated quoted symbol')
            tokens.append(text[index:end + 1])
            index = end + 1
        else:
            end = index
            while end < length and not text[end].isspace() and text[end] not in '();"|':
                end += 1
            tokens.append(text[index:end])
            index = end
    return tokens


def parse(text):
    """Top-level s-expressions: a list node is a Python list, an atom a string."""
    stack = [[]]
    for token in tokenize(text):
        if token == '(':
            stack.append([])
        elif token == ')':
            if len(stack) == 1:
                raise ValueError("unbalanced ')'")
            node = stack.pop()
            stack[-1].append(node)
        else:
            stack[-1].append(token)
    if len(stack) != 1:
        raise ValueError("unbalanced '('")
    return stack[0]


def serialize(node):
    """Text of a node, with single spaces between elements."""
    if not isinstance(node, list):
        return node
    parts = []
    stack = [(node, 0)]
    while stack:
        current, position = stack.pop()
        if position == 0:
            parts.append('(')
        if position < len(current):
            stack.append((current, position + 1))
            if position > 0:
                parts.append(' ')
            child = current[position]
            if isinstance(child, list):
                stack.append((child, 0))
            else:
                parts.append(child)
        else:
            parts.append(')')
    return ''.join(parts)


def copy_tree(node, atom=lambda token: token):
    """A copy of `node`, with every atom mapped through `atom`."""
    if not isinstance(node, list):
        return atom(node)
    root = []
    stack = [(node, root)]
    while stack:
        source, target = stack.pop()
        for child in source:
            if isinstance(child, list):
                copy = []
                target.append(copy)
                stack.append((child, copy))
            else:
                target.append(atom(child))
    return root


def symbol_key(token):
    """One key for a symbol written plainly or between bars."""
    if len(token) >= 2 and token[0] == '|' and token[-1] == '|' and SIMPLE_SYMBOL.match(token[1:-1]):
        return token[1:-1]
    return token


def read_pools(export_text):
    """Pool names by variable-sort signature, from `; primo-pool NAME SORT...` lines."""
    pools = {}
    prefix = '; primo-pool '
    for line in export_text.splitlines():
        if line.startswith(prefix):
            tokens = tokenize(line[len(prefix):])
            pools[tuple(symbol_key(token) for token in tokens[1:])] = tokens[0]
    return pools


def is_quantifier(node):
    return (isinstance(node, list) and len(node) == 3 and node[0] in ('forall', 'exists')
            and isinstance(node[1], list) and node[1]
            and all(isinstance(binding, list) and len(binding) == 2 for binding in node[1]))


def annotate(node, pools):
    """A copy of `node` with a :pool on every quantifier whose sorts have a pool.

    Returns the copy, the number of quantifiers annotated and the number left
    without a pool."""
    root = copy_tree(node)
    annotated = 0
    missing = 0
    stack = [root] if isinstance(root, list) else []
    while stack:
        current = stack.pop()
        if is_quantifier(current):
            signature = tuple(serialize(copy_tree(binding[1], symbol_key)) for binding in current[1])
            name = pools.get(signature)
            if name is None:
                missing += 1
            else:
                annotated += 1
                body = current[2]
                if isinstance(body, list) and body and body[0] == '!':
                    body.extend([':pool', [name]])
                else:
                    current[2] = ['!', body, ':pool', [name]]
        stack.extend(child for child in current if isinstance(child, list))
    return root, annotated, missing


def strip_pools(node, names):
    """A copy of `node` without :pool attributes over `names`; an annotation left
    without attributes is replaced by its term."""
    holder = [copy_tree(node)]
    stack = [(holder, 0)]
    while stack:
        parent, position = stack.pop()
        current = parent[position]
        if not isinstance(current, list):
            continue
        if current and current[0] == '!':
            kept = current[:2]
            index = 2
            while index < len(current):
                value = current[index + 1] if index + 1 < len(current) else None
                if (current[index] == ':pool' and isinstance(value, list) and value
                        and all(item in names for item in value)):
                    index += 2
                else:
                    kept.append(current[index])
                    index += 1
            current[:] = kept
            if len(current) == 2:
                parent[position] = current[1]
                stack.append((parent, position))
                continue
        stack.extend((current, child) for child in range(len(current)))
    return holder[0]


def build_check(input_text, export_text):
    """The cvc5 input for a pool check, and counts of what was added."""
    pools = read_pools(export_text)
    names = set(pools.values())
    options = []
    declarations = []
    assertions = []
    counts = {'pools': len(pools), 'annotated': 0, 'unpooled': 0, 'assertions': 0}
    for command in parse(input_text):
        if not isinstance(command, list) or not command:
            raise ValueError('unexpected top-level atom')
        head = command[0]
        if head in UNSUPPORTED_COMMANDS:
            raise ValueError(f'incremental command {head} is not supported')
        if head in DROPPED_COMMANDS or head.startswith('get-'):
            continue
        if head in TERM_COMMANDS:
            annotated, added, missing = annotate(command, pools)
            if serialize(strip_pools(annotated, names)) != serialize(command):
                raise ValueError('removing the added pools does not restore the input')
            counts['annotated'] += added
            counts['unpooled'] += missing
            if head == 'assert':
                counts['assertions'] += 1
                assertions.append(annotated)
            else:
                declarations.append(annotated)
        elif head == 'set-option':
            options.append(command)
        else:
            declarations.append(command)
    lines = [serialize(command) for command in options]
    lines.append('(set-logic ALL)')
    lines.extend(serialize(command) for command in declarations)
    lines.append(export_text.rstrip('\n'))
    lines.extend(serialize(command) for command in assertions)
    lines.append('(check-sat)')
    return '\n'.join(lines) + '\n', counts


def verdict(primo_answer, cvc5_answer):
    """What cvc5's answer on the pooled input says about Primo's answer."""
    if primo_answer != 'unsat':
        return 'control-unsat' if cvc5_answer == 'unsat' else 'control'
    return {'unsat': 'confirmed', 'sat': 'refuted'}.get(cvc5_answer, 'inconclusive')


def run(command, timeout):
    """First output word, time and exit status of `command`."""
    start = time.monotonic()
    try:
        completed = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {'answer': 'timeout', 'seconds': round(time.monotonic() - start, 3), 'exit': None}
    words = completed.stdout.split()
    return {
        'answer': words[0] if words else '',
        'seconds': round(time.monotonic() - start, 3),
        'exit': completed.returncode,
        'stderr': completed.stderr.strip()[:300],
    }


def check(input_path, args, work):
    export = work / 'pools.smt2'
    primo_command = [args.primo, '--quiet', '--quantifier-export-pools', str(export),
                     *args.primo_arg, str(input_path)]
    result = {'file': str(input_path), 'primo': run(primo_command, args.primo_timeout)}
    if (result['primo']['answer'] != 'unsat' and not args.force) or not export.exists():
        result['verdict'] = 'not-checked'
        return result
    export_text = export.read_text()
    result['export'] = {key: sum(1 for line in export_text.splitlines() if line.startswith(prefix))
                        for key, prefix in (('declarations', '(declare-fun '),
                                            ('skolem_lemmas', '(assert '),
                                            ('pools', '(declare-pool '))}
    try:
        text, counts = build_check(input_path.read_text(), export_text)
    except ValueError as error:
        result['verdict'] = 'error'
        result['error'] = str(error)
        return result
    result['added'] = counts
    target = work / 'check.smt2'
    target.write_text(text)
    result['cvc5'] = run([args.cvc5, *args.cvc5_arg, str(target)], args.cvc5_timeout)
    result['verdict'] = verdict(result['primo']['answer'], result['cvc5']['answer'])
    if args.plain:
        result['cvc5_plain'] = run([args.cvc5, str(input_path)], args.cvc5_timeout)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('input', type=Path)
    parser.add_argument('--primo', default='build/primo')
    parser.add_argument('--primo-arg', action='append', default=[],
                        help='extra Primo argument; repeat for several')
    parser.add_argument('--primo-timeout', type=float, default=60.0)
    parser.add_argument('--cvc5', default='contrib/local/bin/cvc5')
    parser.add_argument('--cvc5-arg', action='append', default=None,
                        help='cvc5 argument; repeat for several (default --pool-inst)')
    parser.add_argument('--cvc5-timeout', type=float, default=60.0)
    parser.add_argument('--plain', action='store_true',
                        help='also run cvc5 on the unmodified input, for comparison')
    parser.add_argument('--force', action='store_true',
                        help='check even when Primo did not answer unsat (negative control)')
    parser.add_argument('--work-dir', type=Path,
                        help='keep the export and the cvc5 input here instead of a temporary directory')
    args = parser.parse_args(argv)
    if args.cvc5_arg is None:
        args.cvc5_arg = ['--pool-inst']
    if args.work_dir is not None:
        args.work_dir.mkdir(parents=True, exist_ok=True)
        result = check(args.input, args, args.work_dir)
    else:
        with tempfile.TemporaryDirectory(prefix='primo-pool-check-') as directory:
            result = check(args.input, args, Path(directory))
    print(json.dumps(result, sort_keys=True))
    return 3 if result['verdict'] in ('refuted', 'control-unsat') else 0


if __name__ == '__main__':
    sys.exit(main())
