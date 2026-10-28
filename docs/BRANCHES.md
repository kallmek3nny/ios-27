# ios-27 | live agent branch source diffs

![proportional compatibility blocks with unmerged and merged-source markers](blocks.svg)

**purple dashed = active unmerged branch; blue solid = recently merged source awaiting compatibility audit.** neither is a verified Apple ABI function or application. the verified green/amber counts remain strictly controlled by reviewed main catalogs.

candidate branches with mapped changes: **0** · audited base: `760954f96010`

no unmerged branches currently have source or test changes matching the catalog's known subsystem paths. this does **not** mean older work is absent from main.

## recently merged agent source changes · not yet compatibility-reviewed

recent merges appear here even after their purple WIP marker disappears. blue means merged code, **not** a newly verified iOS function.

recent merged PRs with source changes: **5**

### [merged PR #67](https://github.com/kallmek3nny/ios-27/pull/67)

merged: 2026-10-28T19:24:38Z · source changes awaiting function-level audit.

- **ObjC host prototype (not Apple libobjc)**: 2 source files, 4 fixtures, +938/-0 lines
  - added declarations: `ios27_objc20c_alloc`, `ios27_objc20c_autorelease`, `ios27_objc20c_create`, `ios27_objc20c_destroy`, `ios27_objc20c_get_stats`, `ios27_objc20c_pool_pop`, `ios27_objc20c_pool_push`, `ios27_objc20c_release`, `ios27_objc20c_retain`, `ios27_objc20c_retain_count`, `ios27_objc20c_strong_store`, `ios27_objc20c_validate`

### [merged PR #62](https://github.com/kallmek3nny/ios-27/pull/62)

merged: 2026-10-28T19:22:22Z · source changes awaiting function-level audit.

- **libSystem guest model (not dylib)**: 2 source files, 3 fixtures, +558/-0 lines
  - added declarations: `ios27_20e_advance_time`, `ios27_20e_calloc`, `ios27_20e_close`, `ios27_20e_free`, `ios27_20e_getenv`, `ios27_20e_gettimeofday`, `ios27_20e_guest_ptr`, `ios27_20e_init`, `ios27_20e_lseek`, `ios27_20e_malloc`, `ios27_20e_mmap`, `ios27_20e_mprotect`

### [merged PR #63](https://github.com/kallmek3nny/ios-27/pull/63)

merged: 2026-10-28T19:21:00Z · source changes awaiting function-level audit.

- **ObjC host prototype (not Apple libobjc)**: 2 source files, 4 fixtures, +596/-0 lines
  - added declarations: `ios27_objc20b_alloc`, `ios27_objc20b_bind`, `ios27_objc20b_create`, `ios27_objc20b_destroy`, `ios27_objc20b_register`, `ios27_objc20b_release`, `ios27_objc20b_replace`, `ios27_objc20b_resolve`, `ios27_objc20b_resolve_global`, `ios27_objc20b_resolve_super_global`, `ios27_objc20b_selector`, `ios27_objc20b_send_words`

### [merged PR #64](https://github.com/kallmek3nny/ios-27/pull/64)

merged: 2026-10-28T19:20:45Z · source changes awaiting function-level audit.

- **libsystem_pthread**: 2 source files, 4 fixtures, +983/-0 lines
  - added declarations: `ios27_20d_cancel_point`, `ios27_20d_cancel_request`, `ios27_20d_cond_signal`, `ios27_20d_cond_wait`, `ios27_20d_create`, `ios27_20d_destroy`, `ios27_20d_getspecific`, `ios27_20d_key_create`, `ios27_20d_key_delete`, `ios27_20d_mutex_lock`, `ios27_20d_mutex_trylock`, `ios27_20d_mutex_unlock`

### [merged PR #65](https://github.com/kallmek3nny/ios-27/pull/65)

merged: 2026-10-28T19:20:17Z · source changes awaiting function-level audit.

- **ObjC host prototype (not Apple libobjc)**: 3 source files, 5 fixtures, +1002/-0 lines
  - added declarations: `ios27_objc20a_counts`, `ios27_objc20a_find_class`, `ios27_objc20a_inspect`, `ios27_objc20a_register`, `ios27_objc20a_register_image`, `ios27_objc20a_registry_create`, `ios27_objc20a_registry_destroy`

## verification policy

changed paths and added declarations are parsed from GitHub PR file patches or main-vs-branch diffs, not commit messages. CI status is a hint only. staged branches may fail integration, omit needed tests, or duplicate already merged work. only separately reviewed catalog updates from merged main can change confirmed target statuses.

Refresh: `timeout 150s python3 tools/scan_branch_activity.py`, then `timeout 15s node tools/compat_blocks.cjs`. a scheduled workflow checks for new agent work at least every 15 minutes, subject to GitHub Actions scheduling delays.

