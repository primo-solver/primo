#!/usr/bin/env python3
"""Generate QF_UF cases exercising `Bool` in every capacity and compare with Z3.

SMT-LIB gives `Bool` no special status: it is a sort like any other, so an
uninterpreted function may take it, return it, or both, and equality is defined
on it. It also has a property no other sort has -- it is *finite, with exactly
two elements* -- which licenses inferences that ordinary congruence closure
does not perform.

Primo already had a `bool-arguments` shape in
`scripts/qf_uflra_smoke_fuzz.py` and eight seeds under
`benchmarks/seeds/qf_uflra/`. Every one of them is QF_UFLRA carrying a Real
constraint, because the semantics was fixed for the mixed path (commit
47fd382, "Fix Boolean argument congruence in mixed EUF"). Pure QF_UF is a
different solver path, and nothing tested that it had the same behaviour.
`bugs/qf-uf-boolean-argument-congruence/` is what that gap cost: a wrong
answer on a four-assertion formula with no arithmetic in it at all.

Two filters had to fail together for that to survive, and this generator is
built to defeat both:

  1. **Logic.** Everything here is `(set-logic QF_UF)` with no arithmetic
     anywhere, so it can only exercise the pure path.
  2. **Direction.** Congruence has a forward direction (`p = q` implies
     `f(p) = f(q)`) and a converse that holds only because `Bool` is
     two-valued (`f(p) = f(q)` with `f` injective implies `p = q`). The old
     seeds assert the premise and check the forward step. The shapes below
     deliberately force the *converse*, and force it to be derived rather
     than stated.

Shapes, and the capacity each covers:

  argument        Bool-sorted arguments of unary/binary/ternary functions,
                  including the constants `true`/`false`
  result          Bool-valued applications used directly as formulas and
                  compared with `=` / `distinct`
  compound        compound formulas (`and`, `or`, `not`, `xor`, `ite`) passed
                  as arguments
  nested          applications as arguments of applications, `f(f(p))`,
                  `g(f(p), q)`
  mixed-sort      Bool travelling through an uninterpreted sort: `U -> Bool`
                  and `Bool -> U` chains
  two-valued      inferences that need `|Bool| = 2`: the converse of
                  congruence, and pigeonhole over three distinct Bool terms
  converse        the specific shape the old harness could not express -- an
                  argument equality derivable ONLY from injectivity
  random          a grammar-based generator rather than a template: random
                  signatures over Bool and two uninterpreted sorts, random
                  terms built bottom-up, and -- the part the shapes above all
                  fix rather than vary -- a RANDOM PROPOSITIONAL SKELETON over
                  those atoms. The 2026-08-26 filter bug lived on that axis:
                  every hand-written shape wrapped Bool constants in a function
                  and none ever placed one in the Boolean structure, so no
                  amount of sampling within them could reach it.

                  It does NOT subsume the templates, and that was measured
                  rather than assumed. Against the binary carrying the filter
                  bug, `pure-polarity` finds a disagreement in ~35% of cases
                  and `random` found ZERO in 1,000 -- because triggering that
                  bug needs four things at once (a Bool constant in argument
                  position, the same constant bare at a single polarity, a
                  finite-domain axiom on the same function, and a disequality
                  that makes the proof need the dropped assignment). Random
                  draws reached two of the four in 7 cases per 600 and all
                  four in none. Use both: `random` for shapes nobody wrote
                  down, the templates for shapes known to be hard.
  pure-polarity   a Bool constant occurring at a SINGLE propositional polarity
                  while also being a UF argument. Polarity is collected from
                  formula structure only, so such a term looks "pure" to the
                  pure-theory filter, which then drops the very assignment the
                  theory needs. Added 2026-08-26 after a code review found the
                  first version of the fix wrong on exactly this shape and this
                  generator reporting PASS on the build that got it wrong --
                  every Bool constant here reached an assertion wrapped in a
                  function, so none was ever propositionally pure
  model-reconstruction
                  a Bool argument that disappears through solved-constant
                  preprocessing while a finite UF table still depends on its
                  printed value. This is satisfiable answer coverage whose
                  useful oracle is model validation, not differential answers.
  all             uniform mix of the above (default)

Exit code is nonzero if any case disagrees with the reference, crashes, or is
rejected as unsupported. With `--validate-models`, it is also nonzero if
Dolmen rejects any satisfiable model. This is the contract consumed by the
official SLURM gate.

    python3 scripts/qf_uf_boolean_fuzz.py --count 400 --shape all \
        --solver ./build/primo --ref contrib/local/bin/z3
"""

from __future__ import annotations

import argparse
import csv
import os
import random
import re
import subprocess
import sys
from pathlib import Path

RESULTS = {"sat", "unsat", "unknown"}

SHAPES = (
    "argument",
    "result",
    "compound",
    "nested",
    "mixed-sort",
    "two-valued",
    "converse",
    "pure-polarity",
    "model-reconstruction",
    "random",
    "all",
)

# Declarations every case shares. Kept fixed rather than randomized so a
# failing case reduces to something a human can read, and so two cases with
# the same assertions are literally the same file.
PREAMBLE = """(set-logic QF_UF)
(declare-sort U 0)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun r () Bool)
(declare-fun a () U)
(declare-fun b () U)
(declare-fun f (Bool) Bool)
(declare-fun g (Bool Bool) Bool)
(declare-fun h (Bool Bool Bool) Bool)
(declare-fun tb (Bool) U)
(declare-fun fromu (U) Bool)
(declare-fun uu (U) U)
"""

BOOL_TERMS = ["p", "q", "r", "true", "false", "(f p)", "(f q)", "(fromu a)", "(fromu b)"]

COMPOUND_TERMS = [
    "(not p)",
    "(and p q)",
    "(or p q)",
    "(xor p q)",
    "(=> p q)",
    "(ite p q r)",
    "(and (not p) (or q r))",
    "(ite (f p) q (not r))",
]


def maybe_negate(rng: random.Random, atom: str) -> str:
    return atom if rng.randrange(2) else f"(not {atom})"


def argument_atoms(rng: random.Random) -> list[str]:
    """`Bool` in argument position, including the two constants."""
    terms = rng.sample(BOOL_TERMS, k=min(4, len(BOOL_TERMS)))
    atoms = [f"(f {terms[0]})", f"(g {terms[0]} {terms[1]})"]
    atoms.append(f"(h {terms[0]} {terms[1]} {terms[2]})")
    atoms.append(f"(= (f {terms[0]}) (f {terms[1]}))")
    atoms.append(f"(= (g {terms[0]} {terms[1]}) (g {terms[1]} {terms[0]}))")
    atoms.append("(distinct (f true) (f false))")
    return [maybe_negate(rng, atom) for atom in atoms]


def result_atoms(rng: random.Random) -> list[str]:
    """Bool-valued applications used as formulas and compared to each other."""
    atoms = [
        "(f p)",
        "(g p q)",
        "(= (f p) q)",
        "(= (f p) (g p q))",
        "(distinct (f p) (f q))",
        "(= (fromu a) (f p))",
    ]
    return [maybe_negate(rng, atom) for atom in atoms]


def compound_atoms(rng: random.Random) -> list[str]:
    """Compound formulas in argument position."""
    left, right = rng.sample(COMPOUND_TERMS, k=2)
    atoms = [
        f"(f {left})",
        f"(g {left} {right})",
        f"(= (f {left}) (f {right}))",
        f"(= (f {left}) (f p))",
        "(distinct (f true) (f false))",
    ]
    return [maybe_negate(rng, atom) for atom in atoms]


def nested_atoms(rng: random.Random) -> list[str]:
    """Applications feeding applications."""
    atoms = [
        "(f (f p))",
        "(= (f (f p)) (f p))",
        "(g (f p) (f q))",
        "(= (f (g p q)) (f (g q p)))",
        "(fromu (tb (f p)))",
        "(= (fromu (uu (tb p))) (f q))",
    ]
    return [maybe_negate(rng, atom) for atom in atoms]


def mixed_sort_atoms(rng: random.Random) -> list[str]:
    """Bool travelling through an uninterpreted sort and back."""
    atoms = [
        "(= (tb p) (tb q))",
        "(= (tb p) a)",
        "(distinct (tb true) (tb false))",
        "(fromu (tb p))",
        "(= (fromu a) (fromu b))",
        "(= (uu (tb p)) (tb q))",
    ]
    return [maybe_negate(rng, atom) for atom in atoms]


def two_valued_atoms(rng: random.Random) -> list[str]:
    """Inferences that need |Bool| = 2, not just congruence.

    Three pairwise-distinct Bool terms cannot exist; neither can an injective
    f together with three distinct images. A solver reasoning about Bool as an
    arbitrary uninterpreted sort accepts both.
    """
    x, y, z = rng.sample(["p", "q", "r", "(f p)", "(f q)", "(fromu a)"], k=3)
    atoms = [
        f"(distinct {x} {y})",
        f"(distinct {y} {z})",
        f"(distinct {x} {z})",
        "(distinct (f true) (f false))",
        f"(= (f {x}) (f {y}))",
    ]
    return atoms


# --- grammar-based generation -------------------------------------------
#
# Everything above is a template: the term shape varies, the propositional
# context does not. This builds both at random, which is the only way to reach
# a shape nobody thought to write down.
#
# Terms are built BOTTOM-UP by level rather than by recursive descent: a level
# applies random signatures to terms already built, so depth is a loop bound
# and there is no call stack to overflow on a deep signature chain.

RANDOM_SORTS = ("Bool", "U", "V")


def random_signature(rng: random.Random, index: int) -> tuple[str, list[str], str]:
    """One uninterpreted function: (name, argument sorts, result sort).

    Bool is over-weighted in argument position on purpose -- it is the sort
    with a fixed finite domain, so it is where congruence closure needs help
    that no other sort requires.
    """
    arity = rng.choice([1, 1, 2, 2, 3])
    args = [rng.choice(("Bool", "Bool", "U", "V")) for _ in range(arity)]
    result = rng.choice(("Bool", "Bool", "U", "V"))
    return (f"fn{index}", args, result)


def compound_terms(rng: random.Random, pool) -> list[tuple[str, str]]:
    """Non-application terms: Boolean connectives, ite, and (dis)equality.

    These matter to Primo specifically because a compound Bool term in
    ARGUMENT position is not passed through -- the frontend replaces it with a
    fresh Bool constant plus a defining propositional equivalence
    (`bind_bool_argument_if_needed`). That auxiliary is then a UF argument
    whose polarity comes from the equivalence rather than from anything the
    user wrote, which is the same interaction that produced the 2026-08-26
    pure-theory-filter bug. Generating them only at the top level, as the
    `compound` template does, never exercises that path under a random
    skeleton.
    """
    additions = []
    bools = pool.get("Bool", [])
    for _ in range(rng.randint(1, 3)):
        kind = rng.random()
        if kind < 0.3 and len(bools) >= 2:
            a, b = rng.sample(bools, 2)
            connective = rng.choice(["and", "or", "=>", "xor"])
            additions.append(("Bool", f"({connective} {a} {b})"))
        elif kind < 0.45 and bools:
            additions.append(("Bool", f"(not {rng.choice(bools)})"))
        elif kind < 0.6 and len(bools) >= 3:
            c, a, b = rng.sample(bools, 3)
            additions.append(("Bool", f"(ite {c} {a} {b})"))
        elif kind < 0.8:
            # (dis)equality over any sort is itself a Bool TERM, usable as an
            # argument, not only as a top-level atom.
            usable = [s for s in RANDOM_SORTS if len(pool.get(s, [])) >= 2]
            if usable:
                sort = rng.choice(usable)
                a, b = rng.sample(pool[sort], 2)
                head = rng.choice(["=", "distinct"])
                additions.append(("Bool", f"({head} {a} {b})"))
        elif bools:
            # ite over an uninterpreted sort, which Primo accepts and which
            # puts a Bool term in the condition of a non-Bool term.
            usable = [s for s in ("U", "V") if len(pool.get(s, [])) >= 2]
            if usable:
                sort = rng.choice(usable)
                a, b = rng.sample(pool[sort], 2)
                additions.append((sort, f"(ite {rng.choice(bools)} {a} {b})"))
    return additions


def build_term_pool(rng: random.Random, signatures, constants, depth: int):
    """Terms grouped by sort, built level by level."""
    pool = {sort: list(names) for sort, names in constants.items()}
    pool["Bool"] = pool.get("Bool", []) + ["true", "false"]
    for _ in range(depth):
        additions = []
        for name, args, result in signatures:
            # Skip a signature whose arguments cannot be supplied yet.
            if any(not pool.get(sort) for sort in args):
                continue
            for _ in range(rng.randint(0, 2)):
                chosen = [rng.choice(pool[sort]) for sort in args]
                additions.append((result, f"({name} {' '.join(chosen)})"))
        additions.extend(compound_terms(rng, pool))
        for result, term in additions:
            pool.setdefault(result, []).append(term)
        for sort in pool:
            # Keep the pool from exploding; retain a random slice so later
            # levels do not always build on the same few terms.
            if len(pool[sort]) > 24:
                pool[sort] = rng.sample(pool[sort], 24)
    return pool


def random_atoms(rng: random.Random, pool) -> list[str]:
    atoms = []
    for _ in range(rng.randint(3, 9)):
        kind = rng.random()
        usable = [s for s in RANDOM_SORTS if len(pool.get(s, [])) >= 2]
        if not usable:
            break
        sort = rng.choice(usable)
        terms = pool[sort]
        if kind < 0.35:
            a, b = rng.sample(terms, 2)
            atoms.append(f"(= {a} {b})")
        elif kind < 0.6:
            a, b = rng.sample(terms, 2)
            atoms.append(f"(distinct {a} {b})")
        elif kind < 0.7 and len(terms) >= 3:
            a, b, c = rng.sample(terms, 3)
            atoms.append(f"(distinct {a} {b} {c})")
        elif pool.get("Bool"):
            # A Bool term used directly as a formula -- including a bare
            # constant, which is what puts it in the propositional skeleton.
            atoms.append(rng.choice(pool["Bool"]))
    return [a for a in atoms if a not in ("true", "false")]


def random_skeleton(rng: random.Random, atoms: list[str], polarity_bias=None) -> list[str]:
    """Fold atoms into a random propositional structure, iteratively.

    Negation is NOT an independent coin per occurrence. Doing that was the
    first version and it measured badly: a constant appearing more than once
    almost always ends up at both polarities, so nothing is ever pure, and the
    shape has zero probability of exercising the pure-theory filter -- 1,000
    cases found nothing on a binary a targeted shape breaks 35% of the time.
    Real formulas are not like that; single-polarity variables are ordinary.
    So each Bool constant gets a polarity bias for the whole case, and only
    unbiased atoms flip per occurrence.
    """
    polarity_bias = polarity_bias or {}
    formulas = []
    for atom in atoms:
        bias = polarity_bias.get(atom)
        if bias is True:
            formulas.append(atom)
        elif bias is False:
            formulas.append(f"(not {atom})")
        else:
            formulas.append(atom if rng.randrange(2) else f"(not {atom})")
    for _ in range(rng.randint(1, 3)):
        if len(formulas) < 2:
            break
        picked = rng.sample(formulas, min(len(formulas), rng.choice([2, 2, 3])))
        connective = rng.choice(["or", "and", "or", "=>", "xor"])
        if connective == "=>" and len(picked) > 2:
            picked = picked[:2]
        formulas.append(f"({connective} {' '.join(picked)})")
        if rng.randrange(4) == 0 and len(formulas) >= 3:
            cond, then, other = rng.sample(formulas, 3)
            formulas.append(f"(ite {cond} {then} {other})")
    if not formulas:
        return []
    keep = rng.randint(1, min(len(formulas), 7))
    return rng.sample(formulas, keep)


def random_case(rng: random.Random) -> tuple[str, list[str]]:
    """Returns (declarations, assertions)."""
    signatures = [random_signature(rng, i) for i in range(rng.randint(2, 4))]
    constants = {
        "Bool": [f"b{i}" for i in range(rng.randint(2, 4))],
        "U": [f"u{i}" for i in range(rng.randint(1, 3))],
        "V": [f"v{i}" for i in range(rng.randint(1, 2))],
    }
    decls = ["(declare-sort U 0)", "(declare-sort V 0)"]
    for sort, names in constants.items():
        decls += [f"(declare-fun {n} () {sort})" for n in names]
    for name, args, result in signatures:
        decls.append(f"(declare-fun {name} ({' '.join(args)}) {result})")

    pool = build_term_pool(rng, signatures, constants, rng.randint(1, 3))
    atoms = random_atoms(rng, pool)
    if not atoms:
        atoms = [rng.choice(constants["Bool"])]
    # Give each Bool constant a fixed polarity for this case, some of the time.
    # A constant that also appears as a function argument is then exactly the
    # shape whose theory meaning does not follow from its propositional
    # occurrences.
    polarity_bias = {}
    for name in constants["Bool"]:
        roll = rng.random()
        if roll < 0.35:
            polarity_bias[name] = True
        elif roll < 0.7:
            polarity_bias[name] = False
    assertions = random_skeleton(rng, atoms, polarity_bias)

    # Two nudges toward the interesting region. Neither is required for
    # validity; both raise the rate at which a case is unsat for a reason that
    # needs |Bool| = 2 rather than for a propositional reason.
    # Relate the finite-domain axiom to a function the case actually uses on a
    # Bool constant. Picking an unrelated function was measured and does
    # nothing: 28 of 1,000 cases had a pure bare constant, an argument
    # occurrence AND an axiom, yet none connected them, so the shape found
    # zero bugs on a binary the targeted shape breaks 35% of the time. An
    # incoherent random case is a case about nothing.
    body_so_far = " ".join(assertions)
    bool_unary = [n for n, a, r in signatures if a == ["Bool"]]
    connected = [n for n in bool_unary if re.search(r"\(%s b\d+" % n, body_so_far)]
    candidates = connected or bool_unary
    if candidates and rng.randrange(2):
        f = rng.choice(candidates)
        assertions.append(rng.choice([f"(distinct ({f} true) ({f} false))",
                                      f"(= ({f} true) ({f} false))"]))
        # And give the axiom something to bite on: compare that function at a
        # Bool constant against its value at a constant.
        applied = re.findall(r"\(%s (b\d+)\)" % f, body_so_far)
        if applied and rng.randrange(2):
            arg = rng.choice(applied)
            other = rng.choice(["true", "false"])
            assertions.append(rng.choice([f"(distinct ({f} {arg}) ({f} {other}))",
                                          f"(= ({f} {arg}) ({f} {other}))"]))
    if rng.randrange(2):
        # A bare Bool constant, so the term is in the Boolean skeleton as well
        # as in argument position -- at its biased polarity, so the occurrence
        # does not accidentally cancel the bias set above.
        name = rng.choice(constants["Bool"])
        bias = polarity_bias.get(name)
        assertions.append(f"(not {name})" if bias is False else name)
    return "\n".join(decls), assertions


def pure_polarity_case(rng: random.Random) -> list[str]:
    """A Bool constant that is propositionally pure AND a UF argument.

    `collect_theory_atom_polarities` records only propositional occurrences and
    never descends into arguments, so `p` in `(or p q)` is classified
    pure-positive. The pure-theory filter is then entitled to drop the `p =
    false` assignment -- except that congruence over `tb(p)` needs it whichever
    way `p` is assigned. Emitted verbatim: the bare literal is the whole point,
    and sampling a subset would drop it.
    """
    x, y = rng.sample(["p", "q", "r"], k=2)
    bare = rng.choice([f"(or {x} {y})", f"(or (not {x}) {y})", x, f"(not {x})"])
    return rng.choice(
        [
            # Constant image: tb collapses both elements of Bool.
            ["(= (tb true) (tb false))", f"(not (= (tb {x}) (tb true)))", bare],
            # Injective f, contradiction through the argument's own value.
            ["(distinct (f true) (f false))",
             f"(= (f {x}) (f true))", f"(= (f {x}) (f false))", bare],
            # Through an uninterpreted sort.
            [f"(= (tb {x}) a)", "(= (tb true) (tb false))",
             "(not (= a (tb false)))", bare],
        ]
    )


def converse_case(rng: random.Random) -> list[str]:
    """The shape the QF_UFLRA seeds could not express.

    `f` is injective on Bool, so `f(x) = f(y)` forces `x = y`. The point is
    that the argument equality is never asserted and is not derivable
    propositionally: the only route to it is two-valuedness plus injectivity.
    A second function then turns that equality into a contradiction through
    ordinary congruence.
    """
    x, y = rng.sample(["p", "q", "r"], k=2)
    witness = rng.choice(
        [
            # Contradiction through a Bool-valued binary application.
            [f"(= (f {x}) (f {y}))",
             "(distinct (f true) (f false))",
             f"(g {x} {y})",
             f"(not (g {y} {y}))"],
            # Contradiction through an uninterpreted-sort image.
            [f"(= (f {x}) (f {y}))",
             "(distinct (f true) (f false))",
             f"(distinct (tb {x}) (tb {y}))"],
            # Contradiction through a nested application.
            [f"(= (f {x}) (f {y}))",
             "(distinct (f true) (f false))",
             f"(distinct (f (f {x})) (f (f {y})))"],
            # Three-argument version: the equality must reach h's congruence.
            [f"(= (f {x}) (f {y}))",
             "(distinct (f true) (f false))",
             f"(h {x} {y} {x})",
             f"(not (h {y} {y} {y}))"],
        ]
    )
    return witness


def model_reconstruction_case(rng: random.Random) -> list[str]:
    """Exercise Boolean completion after solved-constant preprocessing.

    The first equality lets preprocessing eliminate the uninterpreted-sort
    constant. The Bool argument then has no remaining SAT occurrence, but its
    printed default still has to agree with the argument used to build `tb`'s
    model table. The second equality makes that disagreement observable to a
    model validator while leaving the solver answer satisfiable either way.

    Both assertions are load-bearing and are emitted verbatim. Differential
    answer checking cannot detect this class; the official SLURM gate reruns
    every generated case through Dolmen.
    """
    argument = rng.choice(["p", "q", "r"])
    value = rng.choice(["a", "b"])
    solved = rng.choice([f"(= (tb {argument}) {value})", f"(= {value} (tb {argument}))"])
    collapsed_table = rng.choice(
        ["(= (tb true) (tb false))", "(not (distinct (tb true) (tb false)))"]
    )
    return [solved, collapsed_table]


ATOM_BUILDERS = {
    "argument": argument_atoms,
    "result": result_atoms,
    "compound": compound_atoms,
    "nested": nested_atoms,
    "mixed-sort": mixed_sort_atoms,
    "two-valued": two_valued_atoms,
}


def generate_case(rng: random.Random, index: int, shape: str) -> str:
    if shape == "all":
        shape = rng.choice([name for name in SHAPES if name != "all"])

    if shape == "random":
        decls, assertions = random_case(rng)
        body = "\n".join(f"(assert {a})" for a in assertions)
        return (f"; qf_uf boolean fuzz case {index}, shape random\n"
                f"(set-logic QF_UF)\n{decls}\n{body}\n(check-sat)\n")

    if shape == "pure-polarity":
        assertions = pure_polarity_case(rng)
    elif shape == "converse":
        # Emitted verbatim: every assertion is load-bearing, and dropping or
        # negating one turns the intended unsat into a satisfiable formula
        # that proves nothing.
        assertions = converse_case(rng)
    elif shape == "model-reconstruction":
        assertions = model_reconstruction_case(rng)
    else:
        atoms = ATOM_BUILDERS[shape](rng)
        keep = rng.randint(2, len(atoms))
        assertions = rng.sample(atoms, k=keep)
        # A bare propositional literal over a Bool constant that is also used
        # as an argument elsewhere: this is what makes a term look "pure" to
        # the pure-theory filter, and its absence is why this generator missed
        # the 2026-08-26 filter bug entirely.
        if rng.randrange(2):
            bare = rng.choice(["p", "q", "r"])
            assertions.append(bare if rng.randrange(2) else f"(not {bare})")
        # Mix in a clause so the SAT solver has real case analysis to do
        # rather than a conjunction of units.
        if rng.randrange(2) and len(atoms) >= 2:
            left, right = rng.sample(atoms, k=2)
            assertions.append(f"(or {left} {right})")

    body = "\n".join(f"(assert {atom})" for atom in assertions)
    return f"; qf_uf boolean fuzz case {index}, shape {shape}\n{PREAMBLE}{body}\n(check-sat)\n"


def run_solver(command: list[str], timeout: float) -> tuple[str, int]:
    try:
        proc = subprocess.run(
            command, check=False, text=True, capture_output=True, timeout=timeout
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT", 124
    except FileNotFoundError:
        return "MISSING", 127

    first = proc.stdout.strip().splitlines()[0] if proc.stdout.strip() else ""
    text = (proc.stdout + "\n" + proc.stderr).lower()
    if "unsupported" in text:
        return "UNSUPPORTED_INPUT", proc.returncode
    if first in RESULTS:
        return first, proc.returncode
    if proc.returncode != 0:
        return "CRASH", proc.returncode
    return "UNKNOWN_OUTPUT", proc.returncode


def classify(ours: str, ref: str) -> str:
    if ours in {"UNSUPPORTED_INPUT", "CRASH", "TIMEOUT"}:
        return ours
    if ref in {"MISSING", "TIMEOUT", "CRASH", "UNKNOWN_OUTPUT", "UNSUPPORTED_INPUT"}:
        return "REFERENCE_UNUSABLE"
    if ours == "unknown" or ref == "unknown":
        return "UNKNOWN_INVOLVED"
    if ours == ref:
        return "PASS"
    if ours == "sat" and ref == "unsat":
        return "OUR_SAT_REF_UNSAT"
    if ours == "unsat" and ref == "sat":
        return "OUR_UNSAT_REF_SAT"
    return "UNKNOWN_INVOLVED"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", default="./build/primo")
    parser.add_argument("--ref", default="contrib/local/bin/z3")
    parser.add_argument("--count", type=int, default=200)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--out", default="fuzz-results/qf-uf-boolean")
    parser.add_argument("--shape", choices=SHAPES, default="all")
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument(
        "--validate-models",
        action="store_true",
        help="retain generated cases and require Dolmen to validate every satisfiable model",
    )
    parser.add_argument("--dolmen", default="dolmen", help="Dolmen executable for model validation")
    parser.add_argument(
        "--model-timeout",
        type=float,
        default=20.0,
        help="per-case Dolmen timeout in seconds",
    )
    parser.add_argument(
        "--keep-passing",
        action="store_true",
        help="keep every generated case; by default only disagreements are kept",
    )
    args = parser.parse_args()

    out_dir = Path(args.out)
    cases_dir = out_dir / "cases"
    cases_dir.mkdir(parents=True, exist_ok=True)
    summary_path = out_dir / "summary.csv"

    rng = random.Random(args.seed)
    counts: dict[str, int] = {}
    with summary_path.open("w", newline="", encoding="utf-8") as summary:
        writer = csv.DictWriter(
            summary,
            fieldnames=["case", "shape", "ours", "ours_code", "reference", "reference_code", "outcome"],
        )
        writer.writeheader()
        for index in range(args.count):
            shape = args.shape
            case_path = cases_dir / f"case-{index:04d}.smt2"
            text = generate_case(rng, index, shape)
            case_path.write_text(text, encoding="utf-8")
            recorded_shape = text.splitlines()[0].rsplit(" ", 1)[-1]
            ours, ours_code = run_solver(
                [args.solver, *args.solver_arg, str(case_path)], args.timeout
            )
            reference, ref_code = run_solver([args.ref, str(case_path)], args.timeout)
            outcome = classify(ours, reference)
            counts[outcome] = counts.get(outcome, 0) + 1
            writer.writerow(
                {
                    "case": str(case_path),
                    "shape": recorded_shape,
                    "ours": ours,
                    "ours_code": ours_code,
                    "reference": reference,
                    "reference_code": ref_code,
                    "outcome": outcome,
                }
            )
            summary.flush()
            os.fsync(summary.fileno())
            if outcome == "PASS" and not (args.keep_passing or args.validate_models):
                case_path.unlink()
            elif outcome != "PASS" or args.keep_passing:
                print(f"{case_path}: {outcome} ours={ours} ref={reference}", flush=True)

    print(f"summary: {summary_path}", flush=True)
    for outcome in sorted(counts):
        print(f"{outcome}: {counts[outcome]}", flush=True)

    bad = {
        "UNSUPPORTED_INPUT",
        "CRASH",
        "OUR_SAT_REF_UNSAT",
        "OUR_UNSAT_REF_SAT",
        "UNKNOWN_OUTPUT",
        "REFERENCE_UNUSABLE",
    }
    answer_status = 1 if any(counts.get(outcome, 0) for outcome in bad) else 0
    if not args.validate_models:
        return answer_status

    manifest_path = out_dir / "model-cases.txt"
    manifest_path.write_text(
        "".join(f"{path}\n" for path in sorted(cases_dir.glob("*.smt2"))), encoding="utf-8"
    )
    validator = Path(__file__).with_name("validate_qfuf_models_with_dolmen.py")
    model_command = [
        sys.executable,
        str(validator),
        "--solver",
        args.solver,
        "--dolmen",
        args.dolmen,
        "--z3",
        args.ref,
        "--require-dolmen-pass",
        "--timeout",
        str(args.timeout),
        "--dolmen-timeout",
        str(args.model_timeout),
        "--out",
        str(out_dir / "model-validation"),
        "--manifest",
        str(manifest_path),
    ]
    for solver_arg in args.solver_arg:
        model_command.append(f"--solver-arg={solver_arg}")
    model_status = subprocess.run(model_command, check=False).returncode
    return answer_status or model_status


if __name__ == "__main__":
    raise SystemExit(main())
