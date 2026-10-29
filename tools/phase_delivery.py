#!/usr/bin/env python3
"""Bounded, evidence-linked phase delivery percentages (NOT iOS API compatibility).

This is a fixed 20-workstream engineering delivery denominator:
phase 20a-e, phase 21a-j, phase 22a-e. A workstream is delivered only
when a PR with source changes is merged into main. No unmerged branch
increases the delivery numerator. The audit-only progress.svg remains separate.
"""
import argparse
import html
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/phase-delivery.json"
SVG = ROOT / "docs/phase-delivery.svg"
MD = ROOT / "docs/DELIVERY.md"

WORKSTREAMS = [
    ("20", "Objective-C / libSystem prototypes", "abcde"),
    ("21", "Darwin pthread and synchronization", "abcdefghij"),
    ("22", "Guest VM, BSD, dyld, ObjC, Mach IPC", "abcde"),
]
PHASE = re.compile(r"\bphase\s*(20[a-e]|21[a-j]|22[a-e])\b", re.I)
SHA = re.compile(r"^[0-9a-f]{40}$")


def phase_name(title):
    match = PHASE.search(title or "")
    return match.group(1).lower() if match else None


def ledger(repo_status, branch_activity, previous=None):
    if repo_status.get("repository") != "kallmek3nny/ios-27":
        raise ValueError("unexpected repository provenance")
    if branch_activity.get("repository") != repo_status["repository"]:
        raise ValueError("repository mismatch")
    sha = repo_status.get("main_source", {}).get("sha", "")
    if not SHA.fullmatch(sha):
        raise ValueError("invalid audited main commit")
    if sha != branch_activity.get("main_source_sha"):
        raise ValueError("source scan does not match provenance")
    # File-diff results are the evidence that these PRs contain runtime source.
    with_source_merged = {}
    for record in branch_activity.get("recent_merged", []):
        n = record.get("pr_number")
        if isinstance(n, int) and n > 0 and any(
                int(g.get("source_files", 0)) > 0 for g in record.get("groups", [])):
            with_source_merged[n] = record
    with_source_pending = {}
    for record in branch_activity.get("candidates", []):
        n = record.get("pr_number")
        if isinstance(n, int) and n > 0 and any(
                int(g.get("source_files", 0)) > 0 for g in record.get("groups", [])):
            with_source_pending[n] = record

    merged = {}
    for pr in repo_status.get("recent_merged_pull_requests", []):
        phase = phase_name(pr.get("title"))
        n = pr.get("number")
        if phase and n in with_source_merged:
            candidate = {
                "state": "merged", "pr": n, "sha": pr["head_sha"],
                "url": pr["url"], "merged_at": pr["merged_at"],
                "evidence": "merged PR with source-code diff"}
            if not SHA.fullmatch(candidate["sha"]):
                raise ValueError("invalid merged PR SHA")
            merged.setdefault(phase, candidate)
    # Preserve previously observed merged source PRs after they age out of
    # the recent-PR API window. A merge is durable; open PRs are not.
    if previous:
        if previous.get("schema") != 1 or previous.get("scope") != "phase20-22":
            raise ValueError("invalid previous delivery ledger")
        for row in previous.get("workstreams", []):
            if row.get("state") == "merged" and row.get("phase") not in merged:
                if not SHA.fullmatch(str(row.get("sha", ""))) or not isinstance(row.get("pr"), int):
                    raise ValueError("invalid retained merge evidence")
                merged[row["phase"]] = {k: row[k] for k in (
                    "state", "pr", "sha", "url", "merged_at", "evidence")}

    pending = {}
    for pr in repo_status.get("open_pull_requests", []):
        phase = phase_name(pr.get("title"))
        n = pr.get("number")
        if phase and n in with_source_pending:
            p = {"state": "active", "pr": n, "sha": pr["sha"],
                 "url": pr["url"], "evidence": "unmerged PR with source-code diff"}
            if not SHA.fullmatch(p["sha"]):
                raise ValueError("invalid open PR SHA")
            pending.setdefault(phase, p)

    result = []
    groups = []
    for number, label, letters in WORKSTREAMS:
        rows = []
        for ch in letters:
            phase = number + ch
            row = {"phase": phase, "state": "not_merged"}
            row.update(merged.get(phase, pending.get(phase, {})))
            rows.append(row)
            result.append(row)
        done = sum(1 for row in rows if row["state"] == "merged")
        active = sum(1 for row in rows if row["state"] == "active")
        groups.append({"phase": number, "label": label,
                       "merged": done, "active": active, "total": len(rows),
                       "percent": round(100.0 * done / len(rows), 1)})
    done = sum(r["state"] == "merged" for r in result)
    active = sum(r["state"] == "active" for r in result)
    return {
        "schema": 1, "scope": "phase20-22",
        "method": "merged source-bearing PR workstreams / fixed 20 planned phase workstreams; not iOS compatibility",
        "repository": repo_status["repository"], "source_sha": sha,
        "observed_at": branch_activity.get("observed_at"),
        "delivered": done, "active": active, "total": len(result),
        "percent": round(100.0 * done / len(result), 1),
        "groups": groups, "workstreams": result,
    }


def render_svg(data):
    esc = lambda s: html.escape(str(s), quote=True)
    pct = data["percent"]
    parts = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="780" height="370" viewBox="0 0 780 370" role="img" aria-label="phase 20-22 engineering PR delivery percentage, separate from iOS compatibility">',
        '<style>@media(prefers-color-scheme:light){.txt{fill:#1f2328}.muted{fill:#59636e}.track{fill:#d1d9e0}}</style>',
        '<text x="26" y="33" font-size="22" font-family="sans-serif" font-weight="650" fill="#e6edf3" class="txt">Agent engineering delivery</text>',
        '<text x="26" y="54" font-size="12" font-family="sans-serif" fill="#8b949e" class="muted">Merged workstreams / fixed phases 20a–22e · not percent of iOS compatibility</text>',
        '<text x="26" y="120" font-size="55" font-family="sans-serif" font-weight="750" fill="#58a6ff">' + esc(f"{pct:.1f}%") + '</text>',
        '<text x="260" y="109" font-size="15" font-family="sans-serif" fill="#e6edf3" class="txt">' + esc(f"{data['delivered']} / {data['total']} workstreams merged") + '</text>',
        '<text x="260" y="133" font-size="13" font-family="sans-serif" fill="#a371f7">' + esc(f"{data['active']} open source-bearing PRs") + '</text>',
        '<rect x="26" y="154" width="724" height="16" rx="7" fill="#30363d" class="track"/>',
        '<rect x="26" y="154" width="' + format(724 * data["delivered"]/data["total"], ".2f") + '" height="16" rx="7" fill="#58a6ff"/>',
    ]
    if data["active"]:
        x = 26 + 724 * data["delivered"]/data["total"]
        w = 724 * data["active"]/data["total"]
        parts.append('<rect x="' + format(x, ".2f") + '" y="154" width="' + format(w, ".2f") +
                     '" height="16" fill="#a371f7"/>')
    for i, g in enumerate(data["groups"]):
        y = 214 + i*47
        parts += [
            '<text x="26" y="' + str(y) + '" font-size="14" font-family="sans-serif" fill="#e6edf3" class="txt">' + esc("Phase " + g["phase"] + " · " + g["label"]) + '</text>',
            '<text x="746" y="' + str(y) + '" text-anchor="end" font-size="14" font-family="sans-serif" font-weight="600" fill="#58a6ff">' + esc(f"{g['percent']:.1f}%") + '</text>',
            '<rect x="26" y="' + str(y+8) + '" width="724" height="9" rx="4" fill="#30363d" class="track"/>',
            '<rect x="26" y="' + str(y+8) + '" width="' + format(724*g["merged"]/g["total"], ".2f") + '" height="9" rx="4" fill="#58a6ff"/>'
        ]
        if g["active"]:
            parts.append('<rect x="' + format(26+724*g["merged"]/g["total"], ".2f") +
                         '" y="' + str(y+8) + '" width="' + format(724*g["active"]/g["total"], ".2f") +
                         '" height="9" fill="#a371f7"/>')
    parts.append('<text x="26" y="361" font-size="11" font-family="sans-serif" fill="#8b949e" class="muted">Blue = merged source PR · purple = open source PR · gray = not merged</text>')
    parts.append('</svg>')
    return "\n".join(parts) + "\n"


def render_md(data):
    rows = ["# Phase 20–22 engineering delivery", "",
            "![Agent engineering delivery percentage](phase-delivery.svg)", "",
            "**Meaning:** percentage of 20 explicitly scoped engineering workstreams with "
            "source-bearing PRs **merged into main**. This is not a measurement of iOS, "
            "Apple ABI, or real-device compatibility. A merged PR is not guaranteed "
            "to implement its entire original requirement.", "",
            f"**{data['percent']:.1f}% delivered** · {data['delivered']}/{data['total']} merged"
            f" · {data['active']} open source-code PRs · {data['total']-data['delivered']-data['active']} without a tracked PR", "",
            "| phase | merged | active | delivered |", "| --- | ---: | ---: | ---: |"]
    for g in data["groups"]:
        rows.append(f"| {g['phase']} {g['label']} | {g['merged']}/{g['total']} | "
                    f"{g['active']} | {g['percent']:.1f}% |")
    rows += ["", "## Evidence by workstream", "",
             "| scope | state | source PR |", "| --- | --- | --- |"]
    for r in data["workstreams"]:
        link = f"[#{r['pr']}]({r['url']})" if "pr" in r else "—"
        rows.append(f"| {r['phase']} | {r['state']} | {link} |")
    rows += ["", "**Source:** [machine-readable phase ledger](phase-delivery.json), "
             "[GitHub source-diff scan](branch-activity.json) and "
             "[merged PR provenance](repo-status.json).", "",
             "The older [compatibility gate percentages](PROGRESS.md) are a separate, "
             "manually reviewed fixture inventory and do not automatically advance on PR merges.", ""]
    return "\n".join(rows) + "\n"


def self_test():
    assert phase_name("phase 20e: guest services") == "20e"
    assert phase_name("phase21g: Darwin condition variables") == "21g"
    assert phase_name("phase 22d runtime") == "22d"
    assert phase_name("refresh 22e") is None
    src = {"schema": 1, "repository": "kallmek3nny/ios-27",
           "main_source": {"sha": "a"*40},
           "recent_merged_pull_requests": [
               {"number": 65, "title": "phase20a metadata", "head_sha": "b"*40,
                "url": "https://github.com/kallmek3nny/ios-27/pull/65",
                "merged_at": "2026-10-28T19:00:00Z"}],
           "open_pull_requests": [{"number": 87, "title": "phase22e IPC",
                                  "sha": "c"*40, "url": "https://github.com/kallmek3nny/ios-27/pull/87"}]}
    diff = {"repository": "kallmek3nny/ios-27", "main_source_sha": "a"*40,
            "recent_merged": [{"pr_number": 65, "groups": [{"source_files": 2}]}],
            "candidates": [{"pr_number": 87, "groups": [{"source_files": 1}]}],
            "observed_at": "2026-10-28T19:00:00Z"}
    r = ledger(src, diff)
    assert (r["delivered"], r["active"], r["total"], r["percent"]) == (1, 1, 20, 5.0)
    assert r["groups"][0]["percent"] == 20.0
    assert r["workstreams"][-1]["phase"] == "22e"
    assert r["workstreams"][-1]["state"] == "active"
    assert ledger(src, dict(diff, recent_merged=[]), r)["delivered"] == 1
    assert render_svg(r).count("<svg") == 1
    assert "5.0%" in render_md(r)
    try:
        ledger(src, dict(diff, main_source_sha="d"*40))
        raise AssertionError("mismatched provenance was accepted")
    except ValueError:
        pass
    print("phase delivery tests passed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    previous = json.loads(REGISTRY.read_text(encoding="utf-8")) if REGISTRY.exists() else None
    status = json.loads((ROOT / "docs/repo-status.json").read_text(encoding="utf-8"))
    activity = json.loads((ROOT / "docs/branch-activity.json").read_text(encoding="utf-8"))
    data = ledger(status, activity, previous)
    generated = [(REGISTRY, json.dumps(data, indent=2) + "\n"),
                 (SVG, render_svg(data)), (MD, render_md(data))]
    if args.check:
        stale = [str(p) for p, value in generated
                 if not p.exists() or p.read_text(encoding="utf-8") != value]
        if stale:
            raise ValueError("stale phase delivery outputs: " + ", ".join(stale))
    else:
        for path, value in generated:
            path.write_text(value, encoding="utf-8")
    print(f"phase 20-22 delivery: {data['delivered']}/{data['total']}"
          f" merged ({data['percent']:.1f}%), {data['active']} active")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, ZeroDivisionError) as err:
        sys.exit("phase delivery failed closed: " + str(err))
