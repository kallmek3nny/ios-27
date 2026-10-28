# ios-27 | live agent branch source diffs

![proportional API blocks with purple WIP markers](blocks.svg)

**purple outlines = changed source or test files on an unmerged branch.** these are *provisional change signals*, not implementations that passed an Apple ABI conformance test. the verified green/amber counts remain strictly controlled by reviewed main catalogs.

candidate branches with mapped changes: **0** · audited base: `760954f96010`

no unmerged branches currently have source or test changes matching the catalog's known subsystem paths. this does **not** mean older work is absent from main.

## verification policy

changed paths and added declarations are parsed from GitHub PR file patches or main-vs-branch diffs, not commit messages. CI status is a hint only. staged branches may fail integration, omit needed tests, or duplicate already merged work. only separately reviewed catalog updates from merged main can change confirmed target statuses.

Refresh: `timeout 150s python3 tools/scan_branch_activity.py`, then `timeout 15s node tools/compat_blocks.cjs`. a scheduled workflow checks for new agent work at least every 15 minutes, subject to GitHub Actions scheduling delays.

