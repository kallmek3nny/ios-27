# ios-27 | live agent branch source diffs

![proportional API blocks with purple WIP markers](blocks.svg)

**purple outlines = changed source or test files on an unmerged branch.** these are *provisional change signals*, not implementations that passed an Apple ABI conformance test. the verified green/amber counts remain strictly controlled by reviewed main catalogs.

candidate branches with mapped changes: **3** · audited base: `6fc645a38528`

### [phase20d-darwin-threading](https://github.com/kallmek3nny/ios-27/pull/64)

head: `3252d111e12d` · ci: **running** · unmerged, not credited.

- **libsystem_pthread**: 2 source files, 0 test/fixture files, +553/-0 lines
  - candidate declarations: `ios27_20d_cancel_point`, `ios27_20d_cancel_request`, `ios27_20d_cond_signal`, `ios27_20d_cond_wait`, `ios27_20d_create`, `ios27_20d_destroy`, `ios27_20d_getspecific`, `ios27_20d_key_create`, `ios27_20d_key_delete`, `ios27_20d_mutex_lock`, `ios27_20d_mutex_trylock`, `ios27_20d_mutex_unlock`
- **unclassified**: 0 source files, 4 test/fixture files, +430/-0 lines

### [phase20b-native-objc-dispatch-20261028](https://github.com/kallmek3nny/ios-27/pull/63)

head: `809381d1f0e3` · ci: **running** · unmerged, not credited.

- **ObjC host prototype (not Apple libobjc)**: 2 source files, 0 test/fixture files, +384/-0 lines
  - candidate declarations: `ios27_objc20b_alloc`, `ios27_objc20b_bind`, `ios27_objc20b_create`, `ios27_objc20b_destroy`, `ios27_objc20b_register`, `ios27_objc20b_release`, `ios27_objc20b_replace`, `ios27_objc20b_resolve`, `ios27_objc20b_resolve_global`, `ios27_objc20b_resolve_super_global`, `ios27_objc20b_selector`, `ios27_objc20b_send_words`
- **unclassified**: 0 source files, 4 test/fixture files, +212/-0 lines

### [phase20e-libsystem-userspace](https://github.com/kallmek3nny/ios-27/pull/62)

head: `b9fa9469596c` · ci: **passing_checks** · unmerged, not credited.

- **libSystem guest model (not dylib)**: 2 source files, 0 test/fixture files, +360/-0 lines
  - candidate declarations: `ios27_20e_advance_time`, `ios27_20e_calloc`, `ios27_20e_close`, `ios27_20e_free`, `ios27_20e_getenv`, `ios27_20e_gettimeofday`, `ios27_20e_guest_ptr`, `ios27_20e_init`, `ios27_20e_lseek`, `ios27_20e_malloc`, `ios27_20e_mmap`, `ios27_20e_mprotect`
- **unclassified**: 0 source files, 3 test/fixture files, +198/-0 lines

## verification policy

changed paths and added declarations are parsed from GitHub PR file patches or main-vs-branch diffs, not commit messages. CI status is a hint only. staged branches may fail integration, omit needed tests, or duplicate already merged work. only separately reviewed catalog updates from merged main can change confirmed target statuses.

Refresh: `timeout 150s python3 tools/scan_branch_activity.py`, then `timeout 15s node tools/compat_blocks.cjs`. a scheduled workflow checks for new agent work at least every 15 minutes, subject to GitHub Actions scheduling delays.

