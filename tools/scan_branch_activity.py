#!/usr/bin/env python3
"""Read PR/branch FILE DIFFS and map work-in-progress to ios-27 subsystems.

Candidate source changes are NEVER promoted to verified Apple-compatible APIs.
Only the reviewed compatibility catalog controls completed/verified squares.
"""
import argparse
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
SHA = re.compile(r"^[0-9a-f]{40}$")
REPO = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
SYMBOL = re.compile(r"\bios27_[A-Za-z0-9_]{3,80}\s*\(")
GROUPS = [
    ("objc-host-model", r"^(src/objc_|include/ios27/objc_|tests/phase19e-|tests/fixtures/phase19e-objc)"),
    ("libsystem-host-model", r"^(src/libsystem_|include/ios27/libsystem_)"),
    ("mach-ipc", r"^(src/(mach_|darwin_mach_abi)|include/ios27/(mach_|darwin_mach_abi)|tests/test_(mach_|darwin_mach_abi))"),
    ("bsd-calls", r"^(src/darwin_probe|include/ios27/darwin_probe|tests/test_darwin_probe)"),
    ("dyld-engine", r"^(src/(images|chained|binds|exports|dyld_paths)\.c|include/ios27/(images|chained|binds|exports|dyld_paths)\.h|tests/test_(images|chained|binds|exports|dyld_paths)\.c|tests/fixtures/phase19b-|tools/phase19b-)"),
    ("mach-o", r"^(src/(macho|runner|darwin_object|darwin_branch26|darwin_got|inventory)\.c|include/ios27/(macho|runner|darwin_object|darwin_branch26|darwin_got|inventory)\.h|tests/test_(macho|compiled_darwin)|tests/fixtures/(phase19a-|phase19d-)|tools/(phase19a-|phase19d-))"),
    ("swift", r"^(src/swift_|include/ios27/swift_)"),
    ("corefoundation", r"^(src/(corefoundation|cf_)|include/ios27/(corefoundation|cf_))"),
    ("foundation", r"^(src/foundation_|include/ios27/foundation_)"),
    ("libdispatch", r"^(src/(dispatch_|libdispatch_)|include/ios27/(dispatch_|libdispatch_))"),
    ("libsystem-c", r"^(src/(libc_|libsystem_c_)|include/ios27/(libc_|libsystem_c_))"),
    ("libsystem-pthread", r"^(src/(pthread_|libsystem_pthread_)|include/ios27/(pthread_|libsystem_pthread_))"),
    ("uikit", r"^(src/uikit_|include/ios27/uikit_)"),
    ("swiftui", r"^(src/swiftui_|include/ios27/swiftui_)"),
    ("coreanimation", r"^(src/(coreanimation_|ca_)|include/ios27/(coreanimation_|ca_))"),
    ("metal", r"^(src/(metal_|iosurface_|vulkan_)|include/ios27/(metal_|iosurface_|vulkan_))"),
    ("springboard", r"^(src/(springboard_|frontboard_|backboard_)|include/ios27/(springboard_|frontboard_|backboard_))"),
    ("android", r"^(src/android_|include/ios27/android_)"),
    ("security", r"^(src/security_|include/ios27/security_)"),
    ("network", r"^(src/network_|include/ios27/network_)"),
]
GROUPS = [(name, re.compile(regex)) for name, regex in GROUPS]
ALLOWED_CI = ("native tests", "capability progress", "phase19b real linked darwin dylibs")


# Phase-specific source and fixture prefixes are more reliable than agent
# branch names. Never claim that a source diff proves API compatibility.
PHASE_GROUP = {
    "22a": "sandbox", "22b": "bsd-calls", "22c": "dyld-engine",
    "22d": "objc-host-model", "22e": "mach-ipc",
    "23a": "dyld-engine", "23b": "dyld-engine", "23c": "objc-host-model",
    "23d": "bsd-calls", "23e": "mach-o",
    "24a": "dyld-engine", "24b": "dyld-engine", "24c": "objc-host-model",
    "24d": "bsd-calls", "24e": "mach-o",
    "25a": "objc-host-model", "25b": "objc-host-model",
    "25c": "objc-host-model", "25d": "objc-host-model", "25e": "objc-host-model",
    # Phase 26 source is still WIP, not validated ABI/API functionality.
    "26a": "libsystem-c", "26b": "bsd-calls", "26c": "metal",
    "26d": "coreanimation", "26e": "android",
}
PHASE_NAME = re.compile(r"^phase(21[a-j]|22[a-e]|23[a-e]|24[a-e]|25[a-e]|26[a-e])[-_.]")

def get_group(filename):
    matched = next((name for name, regex in GROUPS if regex.search(filename)), None)
    if matched:
        return matched
    # Agents frequently create brand-new phase-numbered source/header paths.
    # Classify them by explicit subsystem vocabulary rather than requiring
    # every filename to be whitelisted before the branch becomes visible.
    # Standalone Android host and graphics bridges live outside src/.
    # Only explicitly scoped paths are mapped, never arbitrary Android files.
    if filename.startswith("android/phase26c/"):
        return "metal"
    if filename.startswith("android/phase26e/"):
        return "android"
    if not filename.startswith(("src/", "include/ios27/", "tests/", "tools/phase")):
        return None
    name = filename.rsplit("/", 1)[-1].lower()
    phase = PHASE_NAME.match(name)
    if phase:
        return "libsystem-pthread" if phase.group(1).startswith("21") else PHASE_GROUP[phase.group(1)]
    if ("pthread" in name or ("libsystem" in name and "thread" in name)
            or ("phase20d" in name and "thread" in name)):
        return "libsystem-pthread"
    if "objc" in name or "objective_c" in name:
        return "objc-host-model"
    if re.match(r"libsystem21[a-z]_", name):
        return "libsystem-pthread"
    if "libsystem" in name:
        return "libsystem-host-model"
    if "mach" in name and "macho" not in name:
        return "mach-ipc"
    if "dyld" in name or "chained" in name or "exports" in name:
        return "dyld-engine"
    if "metal" in name or "iosurface" in name or "vulkan" in name:
        return "metal"
    if "springboard" in name or "frontboard" in name or "backboard" in name:
        return "springboard"
    if "swiftui" in name:
        return "swiftui"
    if "coreanimation" in name:
        return "coreanimation"
    if "foundation" in name:
        return "foundation"
    return None


def is_source(filename):
    return (filename.startswith(("src/", "include/")) and
            filename.endswith((".c", ".h", ".m", ".mm", ".cc", ".cpp", ".hpp", ".S", ".s"))) or (filename.startswith("src/") and filename.endswith(".py")) or (
            filename.startswith(("android/phase26c/", "android/phase26e/")) and
            filename.endswith((".c", ".h", ".cc", ".cpp", ".hpp", ".java", ".kt")))


def is_fixture(filename):
    return filename.startswith(("tests/", "tools/phase")) and filename.endswith((".c", ".h", ".m", ".py", ".S", ".cc", ".cpp"))


def extract_symbols(patch):
    found = set()
    if not isinstance(patch, str):
        return found
    for line in patch.splitlines():
        if not line.startswith("+") or line.startswith("+++"):
            continue
        line = line[1:].strip()
        if line.startswith(("//", "*", "#", "return ", "if ", "for ", "assert(", "while ")):
            continue
        if re.match(r"^(static |extern |inline |const |void |int |char |size_t |uint\d+_t |int\d+_t |ios27_[A-Za-z_0-9]+ )", line):
            match = SYMBOL.search(line)
            if match:
                found.add(match.group(0)[:-1].strip())
    return found


def http_get(repo, endpoint, token):
    if not REPO.fullmatch(repo) or not endpoint.startswith("/") or endpoint.startswith("//"):
        raise ValueError("invalid GitHub API path")
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "ios27-progress",
               "X-GitHub-Api-Version": "2022-11-28"}
    if token:
        headers["Authorization"] = "Bearer " + token
    with urlopen(Request("https://api.github.com/repos/" + repo + endpoint, headers=headers),
                 timeout=8) as response:
        return json.load(response)


def check_runs(sha, get):
    runs = get("/actions/runs?head_sha=" + sha + "&per_page=20").get("workflow_runs", [])
    latest = {}
    for r in sorted(runs, key=lambda x: (x.get("created_at", ""), x.get("id", 0)), reverse=True):
        if r.get("head_sha") == sha and r.get("name") in ALLOWED_CI:
            latest.setdefault(r["name"], r)
    if not latest:
        return "not_checked"
    if any(r.get("conclusion") == "failure" for r in latest.values()):
        return "failing"
    if any(r.get("status") != "completed" for r in latest.values()):
        return "running"
    return "passing_checks" if all(r.get("conclusion") == "success" for r in latest.values()) else "not_checked"


def summarize(candidate, files, ci, allowed):
    groups = defaultdict(lambda: {"source_files": 0, "fixture_files": 0,
                                   "additions": 0, "deletions": 0, "symbols": set(), "paths": []})
    for item in files[:100]:
        path = item.get("filename", "")
        if not isinstance(path, str) or len(path) > 255:
            continue
        group = get_group(path)
        source, fixture = is_source(path), is_fixture(path)
        if group is None and (source or fixture):
            group = "unclassified"
        if group not in allowed:
            continue
        if not (source or fixture):
            continue
        g = groups[group]
        g["source_files"] += int(source)
        g["fixture_files"] += int(fixture)
        g["additions"] += max(0, min(100000, int(item.get("additions") or 0)))
        g["deletions"] += max(0, min(100000, int(item.get("deletions") or 0)))
        if len(g["paths"]) < 4:
            g["paths"].append(path)
        if source:
            g["symbols"].update(extract_symbols(item.get("patch")))
    # An agent may name tests only by phase (e.g. phase20d-edge.c).
    # Associate unmapped TEST fixtures with the branch's single clearly
    # identified source subsystem. Never guess the owner of unknown source.
    known = [name for name, g in groups.items()
             if name != "unclassified" and g["source_files"] > 0]
    unknown = groups.get("unclassified")
    if len(known) == 1 and unknown and unknown["source_files"] == 0:
        dest = groups[known[0]]
        for key in ("fixture_files", "additions", "deletions"):
            dest[key] += unknown[key]
        dest["paths"] = (dest["paths"] + unknown["paths"])[:4]
        del groups["unclassified"]
    mapped = []
    for name, g in sorted(groups.items()):
        if g["additions"] + g["deletions"] <= 0:
            continue
        mapped.append({"group_id": name, "source_files": g["source_files"],
                       "fixture_files": g["fixture_files"], "additions": g["additions"],
                       "deletions": g["deletions"], "symbols_preview": sorted(g["symbols"])[:12],
                       "example_paths": g["paths"]})
    return {"branch": candidate["branch"], "sha": candidate["sha"], "url": candidate["url"],
            "pr_number": candidate.get("number"), "ci": ci,
            "status": "candidate changes, NOT verified API support",
            "files_checked": min(100, len(files)),
            "possibly_truncated": len(files) >= 100, "groups": mapped}


def audit(repo, provenance, get, groups):
    sha = provenance.get("main_source", {}).get("sha", "")
    if not SHA.fullmatch(sha):
        raise ValueError("invalid main source hash")
    candidates = []
    seen = set()
    for item in provenance.get("open_pull_requests", []):
        cand = {"branch": item["branch"], "sha": item["sha"], "url": item["url"],
                "number": item["number"]}
        if cand["sha"] not in seen:
            candidates.append(cand)
            seen.add(cand["sha"])
    for item in provenance.get("ahead_branches", []):
        cand = {"branch": item["branch"], "sha": item["sha"], "url": item["url"]}
        if cand["sha"] not in seen:
            candidates.append(cand)
            seen.add(cand["sha"])
    candidates = candidates[:36]
    mapped = []
    by_group = defaultdict(list)
    def scan_candidate(cand):
        if not SHA.fullmatch(cand["sha"]):
            raise ValueError("invalid candidate hash")
        if cand.get("number"):
            number = int(cand["number"])
            if number <= 0:
                raise ValueError("invalid PR ID")
            files = get("/pulls/" + str(number) + "/files?per_page=100")
        else:
            result = get("/compare/" + sha + "..." + quote(cand["branch"], safe=""))
            if result.get("status") not in ("ahead", "diverged"):
                return None
            files = result.get("files", [])
        if not isinstance(files, list):
            raise ValueError("bad diff response")
        return summarize(cand, files, check_runs(cand["sha"], get),
                         set(groups) | {"unclassified"})

    # Run bounded independent GitHub reads concurrently. Keep output in
    # the original recent-first order, regardless of completion order.
    with ThreadPoolExecutor(max_workers=8) as pool:
        for cand, result in zip(candidates, pool.map(scan_candidate, candidates)):
            if result is None or not result["groups"]:
                continue
            mapped.append(result)
            for entry in result["groups"]:
                by_group[entry["group_id"]].append(
                    {"branch": cand["branch"], "url": cand["url"],
                     "source_files": entry["source_files"],
                     "fixture_files": entry["fixture_files"], "ci": result["ci"]})
    # Source changes do not vanish from the dashboard at merge time. Keep a
    # separate ledger of RECENTLY MERGED code that still needs a compatibility
    # audit. This never turns green/amber API tiles on its own.
    merged = []
    merged_groups = defaultdict(list)
    def scan_merged(item):
        number = int(item["number"])
        merge_sha = item.get("head_sha", "")
        if number < 1 or not SHA.fullmatch(merge_sha):
            raise ValueError("invalid merged PR record")
        changed = get("/pulls/" + str(number) + "/files?per_page=100")
        if not isinstance(changed, list):
            raise ValueError("invalid merged PR file diff")
        merged_cand = {"branch": "merged-pr-" + str(number), "sha": merge_sha,
                       "url": item["url"], "number": number}
        summary = summarize(merged_cand, changed, "merged_not_compat_verified",
                            set(groups) | {"unclassified"})
        summary["groups"] = [g for g in summary["groups"] if g["source_files"]]
        if not summary["groups"]:
            return None
        summary["status"] = "merged source; compatibility review pending"
        summary["merged_at"] = item.get("merged_at", "")
        return summary

    merged_items = provenance.get("recent_merged_pull_requests", [])[:28]
    with ThreadPoolExecutor(max_workers=8) as pool:
        for summary in pool.map(scan_merged, merged_items):
            if summary is None:
                continue
            merged.append(summary)
            for g in summary["groups"]:
                merged_groups[g["group_id"]].append(
                    {"pr_number": summary["pr_number"], "url": summary["url"],
                     "source_files": g["source_files"],
                     "fixture_files": g["fixture_files"],
                     "status": "merged source, not necessarily compatible"})
    return {"schema": 1, "classification_version": 2,
            "repository": repo, "main_source_sha": sha,
            "method": "unmerged source diffs and recently merged source evidence; neither auto-promotes compatibility",
            "candidates_checked": len(candidates), "candidate_count": len(mapped),
            "candidates": mapped, "groups": dict(sorted(by_group.items())),
            "recent_merged_count": len(merged), "recent_merged": merged,
            "merged_groups": dict(sorted(merged_groups.items()))}


def same(previous, current):
    return isinstance(previous, dict) and {k: v for k, v in previous.items()
                                            if k != "observed_at"} == current


def self_test():
    assert get_group("src/objc_runtime.c") == "objc-host-model"
    assert get_group("src/libsystem_user.c") == "libsystem-host-model"
    assert get_group("src/mach_v2.c") == "mach-ipc"
    assert get_group("src/runner.c") == "mach-o"
    assert get_group("README.md") is None
    assert get_group("src/metal_bridge.c") == "metal"
    assert get_group("src/brand_new_subsystem.c") is None
    assert get_group("include/ios27/libsystem20d_threads.h") == "libsystem-pthread"
    assert get_group("src/libsystem20d_threads.c") == "libsystem-pthread"
    assert get_group("tests/phase20d_threads.c") == "libsystem-pthread"
    assert get_group("src/objc20e_metadata.c") == "objc-host-model"
    assert get_group("src/libsystem21b_lifecycle.c") == "libsystem-pthread"
    assert get_group("include/ios27/libsystem21g_cond.h") == "libsystem-pthread"
    assert get_group("src/libsystem21h_lifecycle.c") == "libsystem-pthread"
    assert get_group("src/libsystem21e_tls.c") == "libsystem-pthread"
    assert get_group("src/phase22a_vm.c") == "sandbox"
    assert get_group("tests/phase22a-stress.c") == "sandbox"
    assert get_group("src/phase22b_dispatch.c") == "bsd-calls"
    assert get_group("src/phase22c_graph.c") == "dyld-engine"
    assert get_group("src/phase22d_runtime.c") == "objc-host-model"
    assert get_group("src/phase22e_mig.c") == "mach-ipc"
    assert get_group("tests/phase21j-sync.c") == "libsystem-pthread"
    for filename, group in [
        ("src/phase23a_graph.c", "dyld-engine"),
        ("tests/phase23b-lifecycle.c", "dyld-engine"),
        ("src/phase23c_metadata.c", "objc-host-model"),
        ("src/phase23d_services.c", "bsd-calls"),
        ("tests/phase23e-app.c", "mach-o"),
        ("src/phase24a_registry.c", "dyld-engine"),
        ("src/phase24b_lifecycle.c", "dyld-engine"),
        ("src/phase24c_registry.c", "objc-host-model"),
        ("src/phase24d_services.c", "bsd-calls"),
        ("tests/phase24e-app.m", "mach-o"),
        ("src/phase25a_bind.py", "objc-host-model"),
        ("src/phase25b_entry.S", "objc-host-model"),
        ("src/phase25c_lifetime.c", "objc-host-model"),
        ("src/phase25d_lifecycle.cpp", "objc-host-model"),
        ("tests/phase25e-fixtures.m", "objc-host-model"),
    ]:
        assert get_group(filename) == group, filename
    assert is_source("src/phase25a_bind.py")
    assert get_group("src/phase26_unknown.c") is None
    for filename, group in [
        ("src/phase26a_libc.c", "libsystem-c"),
        ("include/ios27/phase26a_libc.h", "libsystem-c"),
        ("tests/phase26a-client.c", "libsystem-c"),
        ("src/phase26b_bsd.c", "bsd-calls"),
        ("src/phase26c_graphics.c", "metal"),
        ("android/phase26c/ahb_bridge.c", "metal"),
        ("src/phase26d_compositor.c", "coreanimation"),
        ("tests/phase26d-objc-fixture.m", "coreanimation"),
        ("src/phase26e_host.c", "android"),
        ("android/phase26e/app/src/main/cpp/phase26e_jni.c", "android"),
        ("android/phase26e/app/src/main/java/dev/ios27/host/MainActivity.java", "android"),
    ]:
        assert get_group(filename) == group, filename
    assert is_source("android/phase26c/ahb_bridge.c")
    assert is_source("android/phase26e/app/src/main/java/dev/ios27/host/MainActivity.java")
    assert not is_source("android/phase26e/app/build.gradle")
    assert get_group("android/unrelated/MainActivity.java") is None
    phase26d = summarize(
        {"branch": "phase26d", "sha": "b" * 40,
         "url": "https://github.com/example/repo/tree/phase26d"},
        [{"filename": "src/phase26d_compositor.c", "additions": 20, "deletions": 0},
         {"filename": "tests/phase26d-objc-fixture.m", "additions": 6, "deletions": 0}],
        "not_checked", {"coreanimation", "unclassified", "objc-host-model"})
    assert len(phase26d["groups"]) == 1
    assert phase26d["groups"][0]["group_id"] == "coreanimation"
    phase20d = summarize(
        {"branch": "phase20d-darwin-threading", "sha": "b" * 40,
         "url": "https://github.com/example/repo/tree/phase20d"},
        [{"filename": "include/ios27/libsystem20d_threads.h",
          "additions": 10, "deletions": 0},
         {"filename": "tests/phase20d-arm64-abi.c",
          "additions": 15, "deletions": 0},
         {"filename": "tests/phase20d-edge.c",
          "additions": 12, "deletions": 0}],
        "not_checked", {"libsystem-pthread", "unclassified"})
    assert len(phase20d["groups"]) == 1
    assert phase20d["groups"][0]["group_id"] == "libsystem-pthread"
    assert phase20d["groups"][0]["fixture_files"] == 2
    # An unknown NEW source file is never silently assigned to an existing block.
    unknown = summarize(
        {"branch": "other", "sha": "b" * 40,
         "url": "https://github.com/example/repo/tree/other"},
        [{"filename": "src/unknown20f.c", "additions": 2, "deletions": 0},
         {"filename": "src/objc20e_metadata.c", "additions": 2, "deletions": 0}],
        "not_checked", {"objc-host-model", "unclassified"})
    assert len(unknown["groups"]) == 2

    assert extract_symbols("+int ios27_objc_create(void) {\n+return ios27_objc_fake();\n") == {"ios27_objc_create"}
    provenance = {"main_source": {"sha": "a" * 40},
                  "open_pull_requests": [{"branch": "phase19e", "sha": "b" * 40,
                                          "url": "https://github.com/example/repo/pull/1", "number": 1}],
                  "ahead_branches": [{"branch": "phase19e", "sha": "b" * 40,
                                      "url": "https://github.com/example/repo/tree/phase19e"}]}
    def fake(endpoint):
        if endpoint.startswith("/pulls/"):
            return [{"filename": "src/objc_runtime.c", "additions": 9, "deletions": 0,
                     "patch": "+int ios27_objc_create(void) {"},
                    {"filename": "tests/phase19e-standalone.c", "additions": 8, "deletions": 0}]
        if endpoint.startswith("/actions/runs"):
            return {"workflow_runs": [{"head_sha": "b" * 40, "name": "native tests",
                                        "status": "completed", "conclusion": "success",
                                        "created_at": "2026-10-28", "id": 1}]}
        raise AssertionError(endpoint)
    r = audit("example/repo", provenance, fake, ["objc-host-model"])
    assert r["candidate_count"] == 1 and r["candidates_checked"] == 1
    assert r["candidates"][0]["groups"][0]["symbols_preview"] == ["ios27_objc_create"]
    assert r["candidates"][0]["ci"] == "passing_checks"
    assert len(r["groups"]["objc-host-model"]) == 1
    merged_provenance = {"main_source": {"sha": "a" * 40},
                         "recent_merged_pull_requests": [
                             {"number": 4, "head_sha": "c" * 40,
                              "url": "https://github.com/example/repo/pull/4",
                              "merged_at": "2026-10-28T19:00:00Z"}]}
    merged = audit("example/repo", merged_provenance, fake, ["objc-host-model"])
    assert merged["candidate_count"] == 0 and merged["recent_merged_count"] == 1
    assert merged["merged_groups"]["objc-host-model"][0]["pr_number"] == 4
    assert merged["recent_merged"][0]["status"] == "merged source; compatibility review pending"

    assert same(dict(r, observed_at="previous"), r)
    assert audit("example/repo", {"main_source": {"sha": "a" * 40}}, fake, ["objc-host-model"])["candidate_count"] == 0
    print("branch diff parser: fixture tests passed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default="kallmek3nny/ios-27")
    parser.add_argument("--output", default="docs/branch-activity.json")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    root = Path(__file__).resolve().parents[1]
    provenance = json.loads((root / "docs/repo-status.json").read_text())
    catalog = json.loads((root / "docs/compat-targets.json").read_text())
    if provenance.get("repository") != args.repo:
        raise ValueError("wrong repository snapshot")
    target = root / args.output
    previous = json.loads(target.read_text()) if target.is_file() else None
    info = audit(args.repo, provenance, lambda endpoint: http_get(args.repo, endpoint,
                os.environ.get("GITHUB_TOKEN")), [g["id"] for g in catalog["groups"]])
    if same(previous, info):
        print("no new branch source activity")
        return
    info["observed_at"] = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    target.write_text(json.dumps(info, indent=2) + "\n")
    print("branch activity:", info["candidate_count"], "candidates,",
          len(info["groups"]), "affected subsystems")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, HTTPError, URLError, TypeError) as exc:
        sys.exit("branch diff audit failed closed: " + str(exc))
