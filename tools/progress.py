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
                test = gate.get("test")
                if test and not re.search(r"add_test\s*\(\s*NAME\s+" + re.escape(test) + r"(?:\s|\))", cmake):
                    raise ValueError(f"unregistered ctest for {gate['title']}: {test}")
    return data

def svg(data):
    width, height = 930, 115 + 50 * len(data["groups"])
    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="ios-27 evidence-backed milestone gate progress">',
        '<rect width="100%" height="100%" fill="#101827"/>',
        '<text x="28" y="38" fill="#f9fafb" font-family="sans-serif" font-size="23" font-weight="bold">ios-27 | capability gates</text>',
        '<text x="28" y="62" fill="#a5b4c7" font-family="sans-serif" font-size="13">measured research milestones, NOT percentage of iOS 27 compatibility</text>',
    ]
    for i, group in enumerate(data["groups"]):
        done = sum(g["done"] for g in group["gates"])
        total = len(group["gates"])
        y = 104 + i * 50
        label = html.escape(group["label"])
        lines.extend([
            f'<text x="28" y="{y}" fill="#e5e7eb" font-family="sans-serif" font-size="15">{label}</text>',
            f'<rect x="375" y="{y-15}" width="405" height="17" rx="5" fill="#374151"/>',
            f'<rect x="375" y="{y-15}" width="{int(405 * done / total)}" height="17" rx="5" fill="#3dbba4"/>',
            f'<text x="804" y="{y}" fill="#f3f4f6" font-family="sans-serif" font-size="14">{done}/{total} ({percent(done, total)})</text>',
        ])
    lines.extend([
        f'<text x="28" y="{height-21}" fill="#a5b4c7" font-family="sans-serif" font-size="12">0 official Apple UI / app gates demonstrated; no Android device run confirmed</text>',
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
