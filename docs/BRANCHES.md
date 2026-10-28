# ios-27 | live agent branch source diffs

![proportional compatibility blocks with unmerged and merged-source markers](blocks.svg)

**purple dashed = active unmerged branch; blue solid = recently merged source awaiting compatibility audit.** neither is a verified Apple ABI function or application. the verified green/amber counts remain strictly controlled by reviewed main catalogs.

candidate branches with mapped changes: **2** · audited base: `2a1f8af8f814`

### [phase21a/darwin-pthread-abi-bridge](https://github.com/kallmek3nny/ios-27/pull/69)

head: `913abc9a6bb6` · ci: **running** · unmerged, not credited.

- **libSystem guest model (not dylib)**: 2 source files, 7 test/fixture files, +1369/-0 lines
  - candidate declarations: `ios27_21a_call`, `ios27_21a_create`, `ios27_21a_create_regions`, `ios27_21a_destroy`, `ios27_21a_register_once`, `ios27_21a_release_once`, `ios27_21a_validate`, `ios27_21a_validate_access`

### [feat/phase21d-fair-rwlock-once](https://github.com/kallmek3nny/ios-27/pull/73)

head: `1fd3dc09051a` · ci: **passing_checks** · unmerged, not credited.

- **libSystem guest model (not dylib)**: 2 source files, 5 test/fixture files, +1054/-0 lines
  - candidate declarations: `ios27_21d_create`, `ios27_21d_destroy`, `ios27_21d_once`, `ios27_21d_rdlock`, `ios27_21d_runtime_create`, `ios27_21d_runtime_destroy`, `ios27_21d_tryrdlock`, `ios27_21d_trywrlock`, `ios27_21d_unlock`, `ios27_21d_usage`, `ios27_21d_wrlock`

## recently merged agent source changes · not yet compatibility-reviewed

recent merges appear here even after their purple WIP marker disappears. blue means merged code, **not** a newly verified iOS function.

recent merged PRs with source changes: **6**

### [merged PR #74](https://github.com/kallmek3nny/ios-27/pull/74)

merged: 2026-10-28T20:05:34Z · source changes awaiting function-level audit.

- **libSystem guest model (not dylib)**: 2 source files, 6 fixtures, +405/-22 lines

### [merged PR #70](https://github.com/kallmek3nny/ios-27/pull/70)

merged: 2026-10-28T19:59:38Z · source changes awaiting function-level audit.

- **libSystem guest model (not dylib)**: 2 source files, 6 fixtures, +1004/-0 lines
  - added declarations: `ios27_21c_cond_broadcast`, `ios27_21c_cond_signal`, `ios27_21c_cond_timedwait`, `ios27_21c_cond_wait`, `ios27_21c_mutex_lock`, `ios27_21c_mutex_trylock`, `ios27_21c_mutex_unlock`, `ios27_21c_runtime_create`, `ios27_21c_runtime_destroy`, `ios27_21c_sync_create`, `ios27_21c_sync_destroy`, `ios27_21c_usage`

### [merged PR #72](https://github.com/kallmek3nny/ios-27/pull/72)

merged: 2026-10-28T19:56:54Z · source changes awaiting function-level audit.

- **libSystem guest model (not dylib)**: 2 source files, 4 fixtures, +975/-0 lines
  - added declarations: `ios27_21e_attach`, `ios27_21e_cancel_checkpoint`, `ios27_21e_cancel_register`, `ios27_21e_cancel_request`, `ios27_21e_cancel_setstate`, `ios27_21e_cancel_unregister`, `ios27_21e_cleanup_pop`, `ios27_21e_cleanup_push`, `ios27_21e_cleanup_unwind`, `ios27_21e_create`, `ios27_21e_destroy`, `ios27_21e_getspecific`

### [merged PR #71](https://github.com/kallmek3nny/ios-27/pull/71)

merged: 2026-10-28T19:54:07Z · source changes awaiting function-level audit.

- **libSystem guest model (not dylib)**: 2 source files, 3 fixtures, +774/-0 lines
  - added declarations: `ios27_21b_attr_init`, `ios27_21b_attr_validate`, `ios27_21b_runtime_create`, `ios27_21b_runtime_destroy`, `ios27_21b_runtime_usage`, `ios27_21b_runtime_wait_idle`, `ios27_21b_thread_create`, `ios27_21b_thread_detach`, `ios27_21b_thread_equal`, `ios27_21b_thread_info`, `ios27_21b_thread_join`, `ios27_21b_thread_self`

### [merged PR #67](https://github.com/kallmek3nny/ios-27/pull/67)

merged: 2026-10-28T19:24:38Z · source changes awaiting function-level audit.

- **ObjC host prototype (not Apple libobjc)**: 2 source files, 4 fixtures, +938/-0 lines
  - added declarations: `ios27_objc20c_alloc`, `ios27_objc20c_autorelease`, `ios27_objc20c_create`, `ios27_objc20c_destroy`, `ios27_objc20c_get_stats`, `ios27_objc20c_pool_pop`, `ios27_objc20c_pool_push`, `ios27_objc20c_release`, `ios27_objc20c_retain`, `ios27_objc20c_retain_count`, `ios27_objc20c_strong_store`, `ios27_objc20c_validate`

### [merged PR #62](https://github.com/kallmek3nny/ios-27/pull/62)

merged: 2026-10-28T19:22:22Z · source changes awaiting function-level audit.

- **libSystem guest model (not dylib)**: 2 source files, 3 fixtures, +558/-0 lines
  - added declarations: `ios27_20e_advance_time`, `ios27_20e_calloc`, `ios27_20e_close`, `ios27_20e_free`, `ios27_20e_getenv`, `ios27_20e_gettimeofday`, `ios27_20e_guest_ptr`, `ios27_20e_init`, `ios27_20e_lseek`, `ios27_20e_malloc`, `ios27_20e_mmap`, `ios27_20e_mprotect`

## verification policy

changed paths and added declarations are parsed from GitHub PR file patches or main-vs-branch diffs, not commit messages. CI status is a hint only. staged branches may fail integration, omit needed tests, or duplicate already merged work. only separately reviewed catalog updates from merged main can change confirmed target statuses.

Refresh: `timeout 150s python3 tools/scan_branch_activity.py`, then `timeout 15s node tools/compat_blocks.cjs`. a scheduled workflow checks for new agent work at least every 15 minutes, subject to GitHub Actions scheduling delays.

