# ios-27 | live agent branch source diffs

![proportional API blocks with purple WIP markers](blocks.svg)

**purple outlines = changed source or test files on an unmerged branch.** these are *provisional change signals*, not implementations that passed an Apple ABI conformance test. the verified green/amber counts remain strictly controlled by reviewed main catalogs.

candidate branches with mapped changes: **1** · audited base: `b83af49cfca4`

### [phase20e-libsystem-userspace](https://github.com/kallmek3nny/ios-27/pull/62)

head: `5b41efa301cc` · ci: **running** · unmerged, not credited.

- **libSystem guest model (not dylib)**: 2 source files, 0 test/fixture files, +360/-0 lines
  - candidate declarations: `ios27_20e_advance_time`, `ios27_20e_calloc`, `ios27_20e_close`, `ios27_20e_free`, `ios27_20e_getenv`, `ios27_20e_gettimeofday`, `ios27_20e_guest_ptr`, `ios27_20e_init`, `ios27_20e_lseek`, `ios27_20e_malloc`, `ios27_20e_mmap`, `ios27_20e_mprotect`
- **unclassified**: 0 source files, 3 test/fixture files, +198/-0 lines

## verification policy

changed paths and added declarations are parsed from GitHub PR file patches or main-vs-branch diffs, not commit messages. CI status is a hint only. staged branches may fail integration, omit needed tests, or duplicate already merged work. only separately reviewed catalog updates from merged main can change confirmed target statuses.

Refresh: `timeout 150s python3 tools/scan_branch_activity.py`, then `timeout 15s node tools/compat_blocks.cjs`. a scheduled workflow checks for new agent work at least every 15 minutes, subject to GitHub Actions scheduling delays.

