#!/usr/bin/env python3
"""Verify a bounded phase21j native JSONL trace against its keyed event schedule."""
import errno
import json
import sys

MASK = (1 << 64) - 1
TRACE_CAPACITY = 1024
MAX_BYTES = 2 * 1024 * 1024
SUMMARY_KEYS = {
    "seed", "actors", "steps", "first", "last",
    "saved", "dropped", "injections", "digest", "plan",
}
EVENT_KEYS = {"seq", "actor", "kind", "result", "detail"}


def splitmix(value):
    value = (value + 0x9E3779B97F4A7C15) & MASK
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK
    return value ^ (value >> 31)


def integer(mapping, key):
    value = mapping[key]
    if type(value) is not int or value < 0 or value > MASK:
        raise ValueError("invalid integer: " + key)
    return value


def digest(events):
    value = 1469598103934665603
    for event in events:
        words = (
            event["seq"],
            (event["actor"] << 32) | event["kind"],
            ((event["result"] & 0xFFFFFFFF) << 32) | event["detail"],
        )
        for word in words:
            value = ((value ^ word) * 1099511628211) & MASK
    return "0x{:016x}".format(value)


def verify(lines):
    if not 1 <= len(lines) <= TRACE_CAPACITY + 1:
        raise ValueError("empty or oversized event stream")
    header = json.loads(lines[0])
    if type(header) is not dict or set(header) != SUMMARY_KEYS:
        raise ValueError("bad summary schema")
    if type(header["seed"]) is not str or type(header["digest"]) is not str:
        raise ValueError("bad seed/digest types")
    seed = int(header["seed"], 0)
    if seed < 0 or seed > MASK:
        raise ValueError("seed out of range")
    actors = integer(header, "actors")
    steps = integer(header, "steps")
    if not 1 <= actors <= 8 or not 1 <= steps <= TRACE_CAPACITY:
        raise ValueError("actor or step count out of bounds")
    plan = header["plan"]
    if plan is not None:
        if type(plan) is not str or len(plan) != steps or any(
            character < "0" or character >= chr(ord("0") + actors)
            for character in plan
        ):
            raise ValueError("invalid scripted actor plan")
    events = []
    for line in lines[1:]:
        item = json.loads(line)
        if type(item) is not dict or set(item) != EVENT_KEYS:
            raise ValueError("bad event schema")
        for key in EVENT_KEYS:
            integer(item, key)
        events.append(item)
    expected = []
    for step in range(steps):
        actor = int(plan[step]) if plan is not None else splitmix(seed ^ step) % actors
        selection = splitmix(
            seed ^ splitmix((19 << 32) | actor) ^ splitmix(step)
        )
        injected = selection % 10000 < 1750
        if injected:
            expected.append({
                "seq": len(expected) + 1, "actor": actor, "kind": 7,
                "result": errno.ENOMEM, "detail": 19,
            })
        expected.append({
            "seq": len(expected) + 1, "actor": actor, "kind": 6,
            "result": errno.ENOMEM if injected else 0, "detail": step,
        })
    retained = expected[-TRACE_CAPACITY:]
    first = retained[0]["seq"]
    last = retained[-1]["seq"]
    fields = {
        "first": first,
        "last": last,
        "saved": len(retained),
        "dropped": len(expected) - len(retained),
        "injections": len(expected) - steps,
    }
    for key, correct in fields.items():
        if integer(header, key) != correct:
            raise ValueError("incorrect summary field " + key)
    if events != retained:
        raise ValueError("event stream differs from deterministic replay")
    if header["digest"] != digest(retained):
        raise ValueError("trace hash differs from observed records")
    return "verified {} events, {} injected faults, seed {}".format(
        len(events), fields["injections"], header["seed"])


def main():
    if sys.argv[1:]:
        raise ValueError("usage: phase21j-verify-trace.py < captured.jsonl")
    data = sys.stdin.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError("trace exceeds 2 MiB limit")
    lines = data.splitlines()
    print("phase21j trace: " + verify(lines) + "; PASS")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        sys.exit("phase21j trace: rejected: {}".format(error))
