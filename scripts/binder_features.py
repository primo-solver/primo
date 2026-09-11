#!/usr/bin/env python3
"""Measure the binder features of SMT-LIB benchmarks that bear on term representation.

Supports the bound-variable representation decision in docs/quantifiers.md. For
every file it counts the binder situations that make one representation cheaper
or more dangerous than another, and measures how large the hash-consed term DAG
would be under each of three representations of a bound variable:

  named     the term (name, sort), as written in the input;
  debruijn  the term (index, sort); binders carry only their sorts;
  fresh     a variable no other binder occurrence shares.

`let` is expanded the way Primo's frontend expands it -- a let name denotes the
term its value built -- so the DAG sizes are of the terms the solver would
intern, not of the input text. Under debruijn, a let value used below more
quantified variables than it was built under has to be shifted; those uses are
counted. Under named, the same use is a capture whenever an intervening binder
rebinds a name free in the value; those are counted too, and the named DAG size
is then that of the (wrong) captured term a naive implementation would build.

Approximations: applications of define-fun symbols are not expanded (the
frontend expands them), `match` is flagged but not scoped, and annotations other
than quantifier attributes are dropped.

Everything is iterative: benchmark terms nest far deeper than Python's stack.
"""

import argparse
import csv
import os
import re
from collections import Counter
from concurrent.futures import ProcessPoolExecutor

TOKEN = re.compile(r'\s+|;[^\n]*|"(?:[^"]|"")*"|\|[^|]*\||[()]|[^\s()|";]+')

COUNTERS = [
    "quantifiers",
    "exists",
    "lambda",
    "match",
    "nested_quantifiers",
    "max_binder_depth",
    "duplicate_names_in_binder",
    "shadows_bound_variable",
    "shadows_let_name",
    "shadows_declared_symbol",
    "quoted_bound_names",
    "patterns",
    "multi_patterns",
    "no_patterns",
    "qids",
    "weights",
    "other_quantifier_attributes",
    "misplaced_patterns",
    "open_let_values",
    "let_shift_uses",
    "named_capture_hazards",
    "definitions_with_quantifiers",
    "open_arguments_into_quantified_definitions",
]

EMPTY = frozenset()


def sym(token):
    """SMT-LIB 2.6 section 3.1: |abc| and abc are the same symbol."""
    if len(token) >= 2 and token[0] == "|" and token[-1] == "|":
        return token[1:-1]
    return token


def parse(text):
    """Return the top-level s-expressions; atoms are str, lists are list."""
    top = []
    stack = [top]
    for match in TOKEN.finditer(text):
        token = match.group()
        if token[0].isspace() or token[0] == ";":
            continue
        if token == "(":
            node = []
            stack[-1].append(node)
            stack.append(node)
        elif token == ")":
            if len(stack) == 1:
                raise ValueError("unbalanced )")
            stack.pop()
        else:
            stack[-1].append(token)
    if len(stack) != 1:
        raise ValueError("unbalanced (")
    return top


def to_text(expr):
    if isinstance(expr, str):
        return sym(expr)
    out = []
    stack = [(expr, 0)]
    while stack:
        node, index = stack.pop()
        if index == 0:
            out.append("(")
        if index == len(node):
            out.append(")")
            continue
        stack.append((node, index + 1))
        if index > 0:
            out.append(" ")
        child = node[index]
        if isinstance(child, str):
            out.append(sym(child))
        else:
            stack.append((child, 0))
    return "".join(out)


def attributes(annotated):
    """Yield (keyword, value or None) for an (! term attribute...) list."""
    index = 2
    while index < len(annotated):
        key = annotated[index]
        following = annotated[index + 1] if index + 1 < len(annotated) else None
        if following is not None and not (isinstance(following, str) and following.startswith(":")):
            yield key, following
            index += 2
        else:
            yield key, None
            index += 1


class Fresh:
    def __init__(self):
        self.ids = {}

    def intern(self, key):
        return self.ids.setdefault(key, len(self.ids))

    def leaf(self, kind, text):
        return self.intern((kind, text))

    def var(self, uid):
        return self.intern(("v", uid))

    def app(self, head, children):
        return self.intern(("a", head, *children))

    def binder(self, kind, uids, body):
        return self.intern(("b", kind, uids, body))

    def size(self):
        return len(self.ids)


class Named:
    def __init__(self):
        self.ids = {}
        self.free = []

    def intern(self, key, free):
        found = self.ids.get(key)
        if found is None:
            found = len(self.free)
            self.ids[key] = found
            self.free.append(free)
        return found

    def leaf(self, kind, text):
        return self.intern((kind, text), EMPTY)

    def var(self, name, sort):
        return self.intern(("v", name, sort), frozenset((name,)))

    def app(self, head, children):
        free = EMPTY
        for child in children:
            child_free = self.free[child]
            if child_free:
                free = child_free if not free else free | child_free
        return self.intern(("a", head, *children), free)

    def binder(self, kind, variables, body):
        free = self.free[body]
        if free:
            free = free.difference(name for name, _ in variables)
        return self.intern(("b", kind, variables, body), free)

    def size(self):
        return len(self.free)


class DeBruijn:
    def __init__(self):
        self.ids = {}
        self.keys = []
        # One past the largest index that is loose (not bound inside the term); 0 when closed.
        self.loose = []

    def intern(self, key, loose):
        found = self.ids.get(key)
        if found is None:
            found = len(self.keys)
            self.ids[key] = found
            self.keys.append(key)
            self.loose.append(loose)
        return found

    def leaf(self, kind, text):
        return self.intern((kind, text), 0)

    def var(self, index, sort):
        return self.intern(("v", index, sort), index + 1)

    def app(self, head, children):
        loose = 0
        for child in children:
            loose = max(loose, self.loose[child])
        return self.intern(("a", head, *children), loose)

    def binder(self, kind, sorts, body):
        return self.intern(("b", kind, sorts, body), max(0, self.loose[body] - len(sorts)))

    def shift(self, root, amount):
        """Raise every loose index in root by amount; closed subterms are reused as they are."""
        if amount == 0 or self.loose[root] == 0:
            return root
        memo = {}
        stack = [(root, 0, False)]
        while stack:
            node, cutoff, expanded = stack.pop()
            if (node, cutoff) in memo:
                continue
            if self.loose[node] <= cutoff:
                memo[(node, cutoff)] = node
                continue
            key = self.keys[node]
            if key[0] == "v":
                memo[(node, cutoff)] = self.var(key[1] + amount, key[2])
            elif key[0] == "b":
                inner = cutoff + len(key[2])
                if expanded:
                    memo[(node, cutoff)] = self.binder(key[1], key[2], memo[(key[3], inner)])
                else:
                    stack.append((node, cutoff, True))
                    stack.append((key[3], inner, False))
            else:
                children = key[2:]
                if expanded:
                    memo[(node, cutoff)] = self.app(key[1], [memo[(child, cutoff)] for child in children])
                else:
                    stack.append((node, cutoff, True))
                    for child in children:
                        stack.append((child, cutoff, False))
        return memo[(root, 0)]

    def size(self):
        return len(self.keys)


class FileAnalysis:
    def __init__(self):
        self.named = Named()
        self.debruijn = DeBruijn()
        self.fresh = Fresh()
        self.counts = dict.fromkeys(COUNTERS, 0)
        # name -> innermost-last stack of ('q', position, sort, uid) | ('let', n, d, f, depth) | ('param',)
        self.scopes = {}
        self.qvars = []
        self.binder_depth = 0
        self.next_uid = 0
        self.declared = set()
        self.quantified_definitions = set()
        self.quantifier_terms = (set(), set(), set())
        self.logic = ""
        self.status = ""
        self.commands = set()

    def command(self, command):
        if not isinstance(command, list) or not command or not isinstance(command[0], str):
            return
        head = command[0]
        self.commands.add(head)
        if head == "set-logic" and len(command) > 1:
            self.logic = sym(command[1])
        elif head == "set-info" and len(command) > 2 and command[1] == ":status":
            self.status = sym(command[2])
        elif head in ("declare-fun", "declare-const") and len(command) > 1:
            self.declared.add(sym(command[1]))
        elif head == "define-fun" and len(command) == 5:
            self.define_fun(command)
        elif head == "assert" and len(command) == 2:
            self.term(command[1])

    def define_fun(self, command):
        name = sym(command[1])
        parameters = [sym(parameter[0]) for parameter in command[2]]
        for parameter in parameters:
            self.scopes.setdefault(parameter, []).append(("param",))
        before = self.counts["quantifiers"]
        self.term(command[4])
        for parameter in parameters:
            self.scopes[parameter].pop()
        if self.counts["quantifiers"] > before:
            self.counts["definitions_with_quantifiers"] += 1
            self.quantified_definitions.add(name)
        self.declared.add(name)

    def leaf(self, kind, text):
        return (self.named.leaf(kind, text), self.debruijn.leaf(kind, text), self.fresh.leaf(kind, text))

    def atom(self, token):
        name = sym(token)
        stack = self.scopes.get(name)
        if not stack:
            return self.leaf("s", name)
        entry = stack[-1]
        if entry[0] == "q":
            _, position, sort, uid = entry
            index = len(self.qvars) - 1 - position
            return (self.named.var(name, sort), self.debruijn.var(index, sort), self.fresh.var(uid))
        if entry[0] == "let":
            _, named, debruijn, fresh, depth = entry
            crossed = self.qvars[depth:]
            if crossed:
                shifted = self.debruijn.shift(debruijn, len(crossed))
                if shifted != debruijn:
                    self.counts["let_shift_uses"] += 1
                    debruijn = shifted
                free = self.named.free[named]
                if free and not free.isdisjoint(variable[0] for variable in crossed):
                    self.counts["named_capture_hazards"] += 1
            return (named, debruijn, fresh)
        return self.leaf("p", name)

    def enter_binder(self, head, expr, work):
        counts = self.counts
        if head == "lambda":
            counts["lambda"] += 1
        else:
            counts["quantifiers"] += 1
            if head == "exists":
                counts["exists"] += 1
            if self.binder_depth:
                counts["nested_quantifiers"] += 1
        body = expr[2]
        if isinstance(body, list) and len(body) >= 2 and body[0] == "!":
            for key, value in attributes(body):
                if key == ":pattern":
                    counts["patterns"] += 1
                    if isinstance(value, list) and len(value) > 1:
                        counts["multi_patterns"] += 1
                elif key == ":no-pattern":
                    counts["no_patterns"] += 1
                elif key == ":qid":
                    counts["qids"] += 1
                elif key == ":weight":
                    counts["weights"] += 1
                else:
                    counts["other_quantifier_attributes"] += 1
            body = body[1]
        variables = []
        seen = set()
        for declaration in expr[1]:
            raw = declaration[0]
            name = sym(raw)
            sort = to_text(declaration[1])
            if raw.startswith("|"):
                counts["quoted_bound_names"] += 1
            stack = self.scopes.setdefault(name, [])
            if name in seen:
                counts["duplicate_names_in_binder"] += 1
            elif stack and stack[-1][0] == "q":
                counts["shadows_bound_variable"] += 1
            elif stack and stack[-1][0] == "let":
                counts["shadows_let_name"] += 1
            elif not stack and name in self.declared:
                counts["shadows_declared_symbol"] += 1
            seen.add(name)
            uid = self.next_uid
            self.next_uid += 1
            stack.append(("q", len(self.qvars), sort, uid))
            self.qvars.append((name, sort, uid))
            variables.append((name, sort, uid))
        self.binder_depth += 1
        counts["max_binder_depth"] = max(counts["max_binder_depth"], self.binder_depth)
        work.append(("binder", head, variables))
        work.append(("eval", body))

    def evaluate(self, expr, work, results):
        if isinstance(expr, str):
            results.append(self.atom(expr))
            return
        if not expr:
            results.append(self.leaf("s", "()"))
            return
        head = expr[0]
        if head in ("forall", "exists", "lambda") and len(expr) == 3 and isinstance(expr[1], list):
            self.enter_binder(head, expr, work)
            return
        if head == "let" and len(expr) == 3 and isinstance(expr[1], list):
            work.append(("let_bind", [sym(binding[0]) for binding in expr[1]], expr[2]))
            for binding in reversed(expr[1]):
                work.append(("eval", binding[1]))
            return
        if head == "!" and len(expr) >= 2:
            for key, _ in attributes(expr):
                if key == ":pattern":
                    self.counts["misplaced_patterns"] += 1
            work.append(("eval", expr[1]))
            return
        if head == "match":
            self.counts["match"] += 1
        head_text = sym(head) if isinstance(head, str) else to_text(head)
        work.append(("app", head_text, len(expr) - 1))
        for argument in reversed(expr[1:]):
            work.append(("eval", argument))

    def term(self, root):
        results = []
        work = [("eval", root)]
        while work:
            item = work.pop()
            tag = item[0]
            if tag == "eval":
                self.evaluate(item[1], work, results)
            elif tag == "app":
                _, head, arity = item
                arguments = results[len(results) - arity :]
                del results[len(results) - arity :]
                if (
                    head in self.quantified_definitions
                    and self.qvars
                    and any(self.debruijn.loose[argument[1]] for argument in arguments)
                ):
                    self.counts["open_arguments_into_quantified_definitions"] += 1
                results.append(
                    (
                        self.named.app(head, [argument[0] for argument in arguments]),
                        self.debruijn.app(head, [argument[1] for argument in arguments]),
                        self.fresh.app(head, [argument[2] for argument in arguments]),
                    )
                )
            elif tag == "binder":
                _, head, variables = item
                named, debruijn, fresh = results.pop()
                built = (
                    self.named.binder(head, tuple((name, sort) for name, sort, _ in variables), named),
                    self.debruijn.binder(head, tuple(sort for _, sort, _ in variables), debruijn),
                    self.fresh.binder(head, tuple(uid for _, _, uid in variables), fresh),
                )
                if head != "lambda":
                    for representation, term_id in zip(self.quantifier_terms, built):
                        representation.add(term_id)
                for name, _, _ in reversed(variables):
                    self.scopes[name].pop()
                    self.qvars.pop()
                self.binder_depth -= 1
                results.append(built)
            elif tag == "let_bind":
                _, names, body = item
                values = results[len(results) - len(names) :]
                del results[len(results) - len(names) :]
                if any(self.debruijn.loose[value[1]] for value in values):
                    self.counts["open_let_values"] += 1
                depth = len(self.qvars)
                for name, (named, debruijn, fresh) in zip(names, values):
                    self.scopes.setdefault(name, []).append(("let", named, debruijn, fresh, depth))
                work.append(("let_end", names))
                work.append(("eval", body))
            elif tag == "let_end":
                for name in item[1]:
                    self.scopes[name].pop()

    def row(self):
        row = {"logic": self.logic, "status": self.status, "commands": ";".join(sorted(self.commands))}
        row.update(self.counts)
        row["dag_named"] = self.named.size()
        row["dag_debruijn"] = self.debruijn.size()
        row["dag_fresh"] = self.fresh.size()
        row["quantifier_terms_named"] = len(self.quantifier_terms[0])
        row["quantifier_terms_debruijn"] = len(self.quantifier_terms[1])
        row["quantifier_terms_fresh"] = len(self.quantifier_terms[2])
        return row


FIELDS = (
    ["file", "bytes", "error", "logic", "status"]
    + COUNTERS
    + ["dag_named", "dag_debruijn", "dag_fresh"]
    + ["quantifier_terms_named", "quantifier_terms_debruijn", "quantifier_terms_fresh", "commands"]
)


def analyze_file(path):
    row = {"file": path, "bytes": os.path.getsize(path), "error": ""}
    analysis = FileAnalysis()
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            commands = parse(handle.read())
        for command in commands:
            analysis.command(command)
    except Exception as error:  # a malformed benchmark is a data point, not a crash
        row["error"] = f"{type(error).__name__}: {error}"[:200]
    row.update(analysis.row())
    return row


def summarize(rows):
    print(f"files: {len(rows)}  (parse errors: {sum(1 for row in rows if row['error'])})")
    for key in ("logic", "status"):
        tally = Counter(row[key] or "-" for row in rows)
        print(f"{key}: " + ", ".join(f"{name} {count}" for name, count in tally.most_common()))
    commands = Counter(command for row in rows for command in row["commands"].split(";") if command)
    print("commands (files using each): " + ", ".join(f"{name} {count}" for name, count in commands.most_common()))
    print(f"\n{'feature':45} {'files':>7} {'total':>12}")
    for key in COUNTERS:
        if key == "max_binder_depth":
            continue
        files = sum(1 for row in rows if row[key])
        print(f"{key:45} {files:>7} {sum(row[key] for row in rows):>12}")
    depths = Counter(row["max_binder_depth"] for row in rows)
    print("max quantifier nesting depth (files): " + ", ".join(f"{depth}: {count}" for depth, count in sorted(depths.items())))
    for kind in ("dag", "quantifier_terms"):
        named = sum(row[f"{kind}_named"] for row in rows)
        debruijn = sum(row[f"{kind}_debruijn"] for row in rows)
        fresh = sum(row[f"{kind}_fresh"] for row in rows)
        print(f"\n{kind} (sum over files): named {named}, debruijn {debruijn}, fresh {fresh}")
        if named:
            print(f"  debruijn/named {debruijn / named:.4f}, fresh/named {fresh / named:.4f}")
        print(
            f"  files with debruijn < named: {sum(1 for row in rows if row[f'{kind}_debruijn'] < row[f'{kind}_named'])}, "
            f"debruijn > named: {sum(1 for row in rows if row[f'{kind}_debruijn'] > row[f'{kind}_named'])}, "
            f"debruijn < fresh: {sum(1 for row in rows if row[f'{kind}_debruijn'] < row[f'{kind}_fresh'])}"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", help="directory searched for .smt2 files")
    parser.add_argument("--csv", required=True, help="per-file output")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    paths = sorted(
        os.path.join(directory, name)
        for directory, _, names in os.walk(args.root)
        for name in names
        if name.endswith(".smt2")
    )
    with ProcessPoolExecutor(args.jobs) as pool:
        rows = list(pool.map(analyze_file, paths, chunksize=8))
    with open(args.csv, "w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    summarize(rows)


if __name__ == "__main__":
    main()
