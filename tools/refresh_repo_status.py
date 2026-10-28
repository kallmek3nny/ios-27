#!/usr/bin/env python3
"""Track repository evidence without promoting unmerged branch work.

Main is authoritative. Open PRs and ahead branches are candidates, never
implemented claims. Bounded GitHub API calls run under an outer CLI timeout.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import re
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import quote
from urllib.request import Request, urlopen


def newer_than_main(branch_date, main_date):
    return (isinstance(branch_date, str) and isinstance(main_date, str)
            and branch_date.endswith("Z") and main_date.endswith("Z")
            and branch_date > main_date)

def recent_active_branch(branch_date, now=None):
    """Branches must stay visible when another agent advances main.
    A bounded activity window plus merged-PR exclusion prevents stale ref spam.
    """
    if not isinstance(branch_date, str) or not branch_date.endswith("Z"):
        return False
    try:
        updated = datetime.fromisoformat(branch_date.replace("Z", "+00:00"))
    except ValueError:
        return False
    observed = now or datetime.now(timezone.utc)
    return observed - timedelta(hours=4) <= updated <= observed + timedelta(minutes=5)


API = "https://api.github.com"
BOT_PREFIX = "chore(progress):"
SHA_RE = re.compile(r"^[a-f0-9]{40}$")
REPO_RE = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")


def request_json(path, token):
    if not path.startswith("/") or path.startswith("//"):
        raise ValueError("invalid API path")
    headers = {"Accept": "application/vnd.github+json",
               "X-GitHub-Api-Version": "2022-11-28",
               "User-Agent": "ios27-compat-progress"}
    if token:
        headers["Authorization"] = "Bearer " + token
    with urlopen(Request(API + path, headers=headers), timeout=6) as response:
        return json.load(response)


def source_commit(commits):
    for item in commits:
        if not item["commit"]["message"].startswith(BOT_PREFIX):
            sha = item["sha"]
            if not SHA_RE.fullmatch(sha):
                raise ValueError("invalid main source commit")
            return {"sha": sha, "url": item["html_url"],
                    "committed_at": item["commit"]["committer"]["date"]}
    raise ValueError("no non-dashboard main commit in history")


def latest_source(repo_base, get, previous=None):
    """Page through bot commits, without treating generated docs as source work.

    GitHub often has dozens of dashboard-only commits between real merges.
    The old 30-commit lookup broke all refreshes after enough bot pushes.
    """
    for page in range(1, 9):
        commits = get(repo_base + "/commits?sha=main&per_page=100&page=" + str(page))
        if not isinstance(commits, list):
            raise ValueError("invalid commit history response")
        for item in commits:
            if not item.get("commit", {}).get("message", "").startswith(BOT_PREFIX):
                return source_commit([item])
        if len(commits) < 100:
            break
    if (isinstance(previous, dict) and
            SHA_RE.fullmatch(previous.get("sha", "")) and
            isinstance(previous.get("committed_at"), str) and
            isinstance(previous.get("url"), str)):
        # Clearly mark that the source audit is older, rather than misreporting
        # a bot-generated dashboard commit as new implementation code.
        return dict(previous, provenance_stale=True)
    raise ValueError("no non-dashboard commit in bounded main history")


def snapshot(repo, get, previous=None):
    if not REPO_RE.fullmatch(repo):
        raise ValueError("invalid repo")
    base = "/repos/" + repo
    source = latest_source(base, get, previous.get("main_source") if previous else None)
    open_prs = get(base + "/pulls?state=open&sort=updated&direction=desc&per_page=25")
    closed = get(base + "/pulls?state=closed&sort=updated&direction=desc&per_page=40")
    branches = get(base + "/branches?per_page=100")
    if any(not isinstance(value, list) for value in (open_prs, closed, branches)):
        raise ValueError("bad repository response")
    recently_merged = [
        {"number": p["number"], "title": p["title"][:120],
         "url": p["html_url"], "head_sha": p["head"]["sha"],
         "merged_at": p["merged_at"]}
        for p in closed if p.get("merged_at") and p["base"]["ref"] == "main"
    ][:8]
    pending = [
        {"number": p["number"], "branch": p["head"]["ref"],
         "sha": p["head"]["sha"], "title": p["title"][:120],
         "url": p["html_url"], "updated_at": p["updated_at"],
         "status": "unmerged: not counted"}
        for p in open_prs if p["base"]["ref"] == "main"
    ][:8]
    excluded_sha = {p["head"]["sha"] for p in closed if p.get("merged_at")} | {p["sha"] for p in pending}
    candidates = [
        (p["name"], p["commit"]["sha"]) for p in branches
        if p["name"] != "main" and p["commit"]["sha"] not in excluded_sha
        and p["commit"]["sha"] != source["sha"]
        and SHA_RE.fullmatch(p["commit"]["sha"])
    ]
    # The API branch page is alphabetically ordered. Scan up to 100 and sort
    # confirmed-ahead candidates by actual head commit date, not branch name.
    def compare(pair):
        name, sha = pair
        try:
            c = get(base + "/compare/" + source["sha"] + "..." + quote(name, safe=""))
        except HTTPError as exc:
            if exc.code in (404, 409):
                return None
            raise
        if c.get("status") not in ("ahead", "diverged") or c.get("ahead_by", 0) < 1:
            return None
        history = c.get("commits", [])
        # Squash-merged refs can look ahead. Exclude their merged PR heads
        # first, then require a recent push regardless of concurrent main advances.
        last_date = history[-1]["commit"]["committer"]["date"] if history else ""
        if not recent_active_branch(last_date):
            return None
        return {"branch": name, "sha": sha,
                "ahead_by": c["ahead_by"], "behind_by": c.get("behind_by", 0),
                "last_commit_at": history[-1]["commit"]["committer"]["date"] if history else "",
                "url": "https://github.com/" + repo + "/tree/" + quote(name, safe=""),
                "status": "unmerged: not counted"}
    ahead = []
    with ThreadPoolExecutor(max_workers=6) as pool:
        futures = [pool.submit(compare, x) for x in candidates]
        for future in as_completed(futures):
            item = future.result()
            if item:
                ahead.append(item)
    ahead.sort(key=lambda entry: (entry["last_commit_at"], entry["branch"]), reverse=True)
    return {
        "schema": 1,
        "repository": repo,
        "main_source": source,
        "method": "only reviewed evidence merged into main can change confirmed target statuses",
        "open_pull_requests": pending,
        "ahead_branches": ahead[:8],
        "recent_merged_pull_requests": recently_merged,
        "scan_limits": {"branch_page": 100, "branches_scanned": len(candidates),
                        "open_pr_page": 25, "merged_pr_page": 40},
    }


def unchanged(previous, fresh):
    return previous is not None and {
        k: v for k, v in previous.items() if k != "observed_at"
    } == fresh


def self_test():
    real = {"sha": "a" * 40, "html_url": "https://example.com/a",
            "commit": {"message": "phase18", "committer": {"date": "2026-10-28"}}}
    bot = {"sha": "b" * 40, "commit": {"message": BOT_PREFIX + " refresh"}}
    assert source_commit([bot, real])["sha"] == "a" * 40
    calls = []
    def page_request(url):
        calls.append(url)
        if "&page=1" in url:
            return [bot] * 100
        if "&page=2" in url:
            return [bot, real]
        raise AssertionError(url)
    assert latest_source("/repos/test/repo", page_request)["sha"] == "a" * 40
    assert len(calls) == 2
    assert latest_source("/repos/test/repo", lambda url: [bot],
                         {"sha": "a" * 40, "url": "https://example.com/a",
                          "committed_at": "2026-10-28"})["provenance_stale"]
    sample = {"schema": 1, "main_source": {"sha": "a" * 40}}
    assert unchanged(dict(sample, observed_at="2026-10-28"), sample)
    assert not unchanged(dict(sample, main_source={"sha": "b" * 40}), sample)
    assert not newer_than_main("2026-10-28T16:45:00Z", "2026-10-28T17:47:44Z")
    assert newer_than_main("2026-10-28T18:45:00Z", "2026-10-28T17:47:44Z")
    fixed = datetime(2026, 10, 28, 19, 0, tzinfo=timezone.utc)
    assert recent_active_branch("2026-10-28T17:00:00Z", fixed)
    assert not recent_active_branch("2026-10-28T11:00:00Z", fixed)
    assert not recent_active_branch("2026-10-29T11:00:00Z", fixed)
    print("repository provenance self-test passed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default="kallmek3nny/ios-27")
    parser.add_argument("--output", default="docs/repo-status.json")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    output = Path(args.output)
    previous = json.loads(output.read_text(encoding="utf-8")) if output.is_file() else None
    token = os.environ.get("GITHUB_TOKEN", "")
    fresh = snapshot(args.repo, lambda path: request_json(path, token), previous)
    if unchanged(previous, fresh):
        print("no new source or candidate branches")
        return
    fresh["observed_at"] = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    output.write_text(json.dumps(fresh, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("main source", fresh["main_source"]["sha"], "ahead branches", len(fresh["ahead_branches"]))


if __name__ == "__main__":
    try:
        main()
    except (HTTPError, URLError, OSError, ValueError, KeyError, TypeError) as exc:
        sys.exit("provenance refresh failed closed: " + str(exc))
