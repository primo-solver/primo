"""Read-only GDB callbacks for the qualified 8faa775 SOI binary.

Loaded inside GDB, not imported by the solver. Record exact small-rational
assignments without calling inferior methods; large rationals remain unknown.
"""

import hashlib
import json
import os

import gdb

active = False
events = []


def rational(value):
    pointer = value["big_"].address.cast(gdb.lookup_type("void").pointer().pointer())
    if int(pointer.dereference()):
        return None
    return [int(value["num_"]), int(value["den_"])]


def delta(value):
    return [rational(value["rational"]), rational(value["infinitesimal"])]


class Observe(gdb.Breakpoint):
    def __init__(self, method):
        # Stop after the Debug/ASAN prologue has initialized argument homes.
        # A function-entry breakpoint can expose stale stack argument slots.
        lines = {"update": 215, "pivot": 266}
        location = (
            f"src/theory/arithmetic/lra_tableau.cpp:{lines[method]}"
            if method in lines
            else "primo::theory::arithmetic::LraTableau::" + method
        )
        super().__init__(location)
        self.method = method

    def stop(self):
        global active
        frame = gdb.newest_frame()
        if self.method == "check":
            active = True
        if not active:
            return False
        event = {"event": self.method}
        try:
            owner = frame.read_var("this").dereference()
            work = owner["soi_work_"]
            storage = owner["vars_"]["_M_impl"]
            size = int(storage["_M_finish"] - storage["_M_start"])
            for field in ("selected_steps", "prefix_pivots", "zero_progress_steps"):
                event[field] = int(work[field])
            if self.method == "update":
                var = int(frame.read_var("nonbasic_var"))
                if not 0 <= var < size:
                    raise ValueError("invalid update argument at breakpoint")
                state = (storage["_M_start"] + var).dereference()
                event.update(
                    var=var,
                    old=delta(state["assignment"]),
                    new=delta(frame.read_var("new_value")),
                )
                basis = [
                    index
                    for index in range(size)
                    if bool((storage["_M_start"] + index).dereference()["is_basic"])
                ]
                event["basis_sha256"] = hashlib.sha256(
                    json.dumps(basis).encode()
                ).hexdigest()
                old, new = event["old"], event["new"]
                event["unchanged"] = (
                    old == new if None not in old and None not in new else None
                )
                selected = frame.older().read_var("selection")
                chosen = selected["_M_payload"]["_M_payload"]["_M_value"]
                event["selection"] = {
                    "entering": int(chosen["entering"]),
                    "leaving": int(chosen["leaving"]),
                    "delta": delta(chosen["delta"]),
                    "objective_delta": delta(chosen["objective_delta"]),
                }
            elif self.method == "pivot":
                leaving = int(frame.read_var("basic_var"))
                entering = int(frame.read_var("entering_var"))
                if not (0 <= leaving < size and 0 <= entering < size):
                    raise ValueError("invalid pivot arguments at breakpoint")
                event.update(
                    leaving=leaving,
                    entering=entering,
                )
            elif self.method == "suspend_soi_for_fallback":
                active = False
        except (gdb.error, RuntimeError, ValueError, KeyError, TypeError) as error:
            event["error"] = str(error)
        events.append(event)
        if len(events) >= 2000:
            raise gdb.GdbError("trace event limit reached")
        return False


for method in ("check", "pivot", "update", "suspend_soi_for_fallback"):
    Observe(method)


def finished(event):
    with open(os.environ["PRIMO_TRACE_JSON"], "w") as output:
        json.dump(
            {"exit_code": getattr(event, "exit_code", None), "events": events},
            output,
            indent=2,
        )
        output.write("\n")


gdb.events.exited.connect(finished)
