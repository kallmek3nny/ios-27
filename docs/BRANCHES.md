# ios-27 | live agent branch source diffs

![proportional API blocks with purple WIP markers](blocks.svg)

**purple outlines = changed source or test files on an unmerged branch.** these are *provisional change signals*, not implementations that passed an Apple ABI conformance test. the verified green/amber counts remain strictly controlled by reviewed main catalogs.

candidate branches with mapped changes: **1** · audited base: `d8c08c2ebb80`

### [phase20d-darwin-threading](https://github.com/kallmek3nny/ios-27/tree/phase20d-darwin-threading)

head: `39658bc7098a` · ci: **not_checked** · unmerged, not credited.

- **unclassified**: 1 source files, 0 test/fixture files, +54/-0 lines
  - candidate declarations: `ios27_20d_cancel_point`, `ios27_20d_cancel_request`, `ios27_20d_cond_signal`, `ios27_20d_cond_wait`, `ios27_20d_create`, `ios27_20d_destroy`, `ios27_20d_getspecific`, `ios27_20d_key_create`, `ios27_20d_key_delete`, `ios27_20d_mutex_lock`, `ios27_20d_mutex_trylock`, `ios27_20d_mutex_unlock`

## verification policy

changed paths and added declarations are parsed from GitHub PR file patches or main-vs-branch diffs, not commit messages. CI status is a hint only. staged branches may fail integration, omit needed tests, or duplicate already merged work. only separately reviewed catalog updates from merged main can change confirmed target statuses.

Refresh: `timeout 150s python3 tools/scan_branch_activity.py`, then `timeout 15s node tools/compat_blocks.cjs`. a scheduled workflow checks for new agent work at least every 15 minutes, subject to GitHub Actions scheduling delays.

