#!/usr/bin/env python3
"""Render evidence-backed capability gates. No artificial iOS-completion percentage.

Run: timeout 15s python3 tools/progress.py
Check: timeout 15s python3 tools/progress.py --check
The checked-in catalog is curated: file existence never grants a new gate.
"""
import argparse
import html
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "docs/progress.json"
MD = ROOT / "docs/PROGRESS.md"
SVG = ROOT / "docs/progress.svg"

def percent(done, total):
    return f"{int(1000 * done / total + 0.5) / 10:.1f}%"

def load():
    data = json.loads(CATALOG.read_text(encoding="utf-8"))
    if data.get("schema") != 1 or not isinstance(data.get("groups"), list):
        raise ValueError("invalid progress catalog schema")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    ids = set()
    for group in data["groups"]:
        if group["id"] in ids or not re.fullmatch(r"[a-z0-9-]+", group["id"]):
            raise ValueError("duplicate/invalid progress group")
        ids.add(group["id"])
        if not group.get("gates"):
            raise ValueError("empty progress group")
        for gate in group["gates"]:
            if not isinstance(gate["done"], bool):
                raise ValueError("gate.done must be a boolean")
            if gate["done"]:
                evidence = gate.get("evidence")
                if not evidence or not (ROOT / evidence).is_file() or ".." in Path(evidence).parts:
                    raise ValueError(f"missing evidence for {gate['title']}")
                standalone = gate.get("standalone_test")
                test = gate.get("test")
                if bool(standalone) == bool(test):
                    raise ValueError("completed gate requires one CTest or allowlisted standalone suite: " + gate["title"])
                if standalone:
                    allowed = {
                        "tools/phase19e-standalone.py", "tools/phase20a-standalone.py",
                        "tools/phase20b-verify.sh", "tools/phase20c-build.py",
                        "tools/phase20d-standalone.py", "tools/phase20e-standalone.py",
                        "tools/phase21g-standalone.py", "tools/phase21h-standalone.py",
                    }
                    if standalone not in allowed or not (ROOT / standalone).is_file():
                        raise ValueError("invalid standalone suite for " + gate["title"])
                if test and not re.search(r"add_test\s*\(\s*NAME\s+" + re.escape(test) + r"(?:\s|\))", cmake):
                    raise ValueError(f"unregistered ctest for {gate['title']}: {test}")
    return data

def svg(data):
    width, height = 760, 105 + 66 * len(data["groups"]) + 40
    completed = sum(sum(g["done"] for g in group["gates"]) for group in data["groups"])
    declared = sum(len(group["gates"]) for group in data["groups"])
    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="ios-27 selected research milestone gate coverage">',
        '<style>@media(prefers-color-scheme:light){.gh-text{fill:#1f2328}.gh-muted{fill:#59636e}.gh-track{fill:#d1d9e0}}</style>',
        '<text x="18" y="33" fill="#e6edf3" class="gh-text" font-family="sans-serif" font-size="24" font-weight="600">capability milestones</text>',
        '<text x="18" y="55" fill="#8b949e" class="gh-muted" font-family="sans-serif" font-size="12">documented research gates · not full ios 27 compatibility</text>',
        f'<text x="742" y="32" text-anchor="end" fill="#e6edf3" class="gh-text" font-family="sans-serif" font-size="21" font-weight="700">{completed}/{declared}</text>',
        '<text x="742" y="51" text-anchor="end" fill="#8b949e" class="gh-muted" font-family="sans-serif" font-size="12">gates with evidence</text>',
    ]
    for i, group in enumerate(data["groups"]):
        done = sum(g["done"] for g in group["gates"])
        total = len(group["gates"])
        y = 92 + 66 * i
        label = html.escape(group["label"])
        shown_percent = percent(done, total) if done else "0%"
        text_class = "gh-text" if done else "gh-muted"
        color = "#e6edf3" if done else "#8b949e"
        lines.extend([
            f'<text x="18" y="{y + 7}" fill="#c9d1d9" class="gh-text" font-family="sans-serif" font-size="15" font-weight="500">{label}</text>',
            f'<text x="742" y="{y + 12}" text-anchor="end" fill="{color}" class="{text_class}" font-family="sans-serif" font-size="22" font-weight="700">{shown_percent}</text>',
            f'<rect x="18" y="{y + 22}" width="724" height="9" rx="4.5" fill="#30363d" class="gh-track"/>',
            f'<text x="18" y="{y + 49}" fill="#8b949e" class="gh-muted" font-family="sans-serif" font-size="12">{done} / {total} tested gates</text>',
        ])
        if done:
            lines.append(f'<rect x="18" y="{y + 22}" width="{int(724 * done / total)}" height="9" rx="4.5" fill="#3fb950"/>')
    lines.extend([
        f'<text x="18" y="{height - 21}" fill="#8b949e" class="gh-muted" font-family="sans-serif" font-size="12">selected gate coverage only · apple frameworks, system ui and apps remain unproven</text>',
        '</svg>',
    ])
    return "\n".join(lines) + "\n"


def markdown(data):
    lines = [
        "# ios-27 | capability progress",
        "",
        f"snapshot: {data['as_of']} · baseline: `{data['baseline_commit'][:12]}`",
        "",
        "![capability gate dashboard](progress.svg)",
        "",
        "**counting rule:** these are individually declared, curated research milestones, "
        "**not percentages of full iOS compatibility**. A green gate means there is "
        "documented source/test-fixture evidence in this repository, not that Apple code "
        "works or that the Galaxy S25+ has been tested. The numerator can change only "
        "after evidence-backed review; adding code files alone never completes a gate.",
        "",
        "| capability | documented gates | gate coverage |",
        "| --- | ---: | ---: |",
    ]
    for group in data["groups"]:
        done = sum(g["done"] for g in group["gates"])
        total = len(group["gates"])
        lines.append(f"| {group['label']} | {done}/{total} | {percent(done, total)} |")
    lines.extend([
        "",
        "## realistic technical ceiling",
        "",
        "- **demonstrated:** SDK-free, reviewed, linked ARM64 Darwin executable and dylib "
        "fixtures can be parsed, mapped and executed under narrow native ABI conditions; "
        "synthetic Mach IPC works in bounded in-process tests.",
        "- **plausible research direction, not delivered:** a progressively wider compatibility "
        "environment for selected native Darwin software, requiring a real process boundary, "
        "more dyld and ABI support, trustworthy isolation and on-device validation.",
        "- **unproven full target:** Apple's genuine iOS 27 SpringBoard, UIKit/SwiftUI, "
        "Liquid Glass, frameworks and apps over Android. No complete Apple system UI, "
        "Apple services, arm64e environment or graphics pipeline has been shown to run. "
        "No honest overall percentage or guaranteed upper limit can be inferred.",
        "",
        "## milestone ledger",
        "",
    ])
    for group in data["groups"]:
        lines.extend([f"### {group['label']}", ""])
        for gate in group["gates"]:
            proof = f" ([evidence]({gate['evidence']}))" if gate.get("evidence") else ""
            if proof:
                proof = f" ([evidence](../{gate['evidence']}))"
            lines.append(f"- [{'x' if gate['done'] else ' '}] {gate['title']}{proof}")
        lines.append("")
    lines.extend([
        "## maintenance and verification",
        "",
        "Edit `docs/progress.json` only when adding/removing a specifically defined gate "
        "or when new evidence establishes completion. Completed gates need a real "
        "repository evidence path; registered CTest names are checked where specified. "
        "Run `timeout 15s python3 tools/progress.py` to regenerate this page and the SVG. "
        "Run `timeout 15s python3 tools/progress.py --check` to reject stale results. "
        "A passing generator check does not execute native code; the existing x86_64 "
        "and ARM64 CTest workflows remain necessary.",
        "",
        "The baseline starts on 2026-10-28; no earlier percentages have been "
        "invented. The status table is a declared-gate ledger, not an extrapolation "
        "of how long the final project will take.",
        "",
    ])
    return "\n".join(lines)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    catalog = load()
    outputs = [(MD, markdown(catalog)), (SVG, svg(catalog))]
    if args.check:
        stale = [str(path.relative_to(ROOT)) for path, body in outputs
                 if not path.exists() or path.read_text(encoding="utf-8") != body]
        if stale:
            parser.error("stale progress outputs: " + ", ".join(stale))
    else:
        for path, body in outputs:
            path.write_text(body, encoding="utf-8")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError, ZeroDivisionError) as exc:
        sys.exit(f"progress: {exc}")
