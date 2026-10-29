# ios-27 | live agent branch source diffs

![proportional compatibility blocks with unmerged and merged-source markers](blocks.svg)

**purple dashed = active unmerged branch; blue solid = recently merged source awaiting compatibility audit.** neither is a verified Apple ABI function or application. the verified green/amber counts remain strictly controlled by reviewed main catalogs.

candidate branches with mapped changes: **9** · audited base: `9193cdee3bd7`

### [agent/phase22c-transactional-dyld-graph](https://github.com/kallmek3nny/ios-27/pull/83)

head: `1a277ef5e829` · ci: **running** · unmerged, not credited.

- **unclassified**: 2 source files, 5 test/fixture files, +1302/-0 lines
  - candidate declarations: `ios27_22c_acquire`, `ios27_22c_create`, `ios27_22c_create_with_paths`, `ios27_22c_destroy`, `ios27_22c_initialize`, `ios27_22c_load`, `ios27_22c_order`, `ios27_22c_pins`, `ios27_22c_release`, `ios27_22c_resolve`, `ios27_22c_symbol`, `ios27_22c_unload`

### [phase22a-guest-address-spaces](https://github.com/kallmek3nny/ios-27/pull/85)

head: `7e29a48f2fd6` · ci: **passing_checks** · unmerged, not credited.

- **unclassified**: 3 source files, 9 test/fixture files, +1109/-0 lines
  - candidate declarations: `ios27_22a_close`, `ios27_22a_copyin`, `ios27_22a_copyout`, `ios27_22a_create`, `ios27_22a_destroy`, `ios27_22a_fail_map_after`, `ios27_22a_fail_stage`, `ios27_22a_map`, `ios27_22a_map_stack`, `ios27_22a_pin`, `ios27_22a_probe`, `ios27_22a_protect`

### [phase22e-mach-ipc-mig](https://github.com/kallmek3nny/ios-27/pull/87)

head: `ad180562e914` · ci: **running** · unmerged, not credited.

- **unclassified**: 2 source files, 7 test/fixture files, +1448/-0 lines

### [phase22b-darwin-bsd-capability-20261028](https://github.com/kallmek3nny/ios-27/pull/86)

head: `e909e8faed23` · ci: **passing_checks** · unmerged, not credited.

- **unclassified**: 2 source files, 3 test/fixture files, +642/-0 lines
  - candidate declarations: `ios27_22b_advance`, `ios27_22b_capture`, `ios27_22b_create`, `ios27_22b_destroy`, `ios27_22b_dispatch`, `ios27_22b_errno`, `ios27_22b_interrupt`, `ios27_22b_metadata_fd`, `ios27_22b_preopen`, `ios27_22b_set_clock`, `ios27_22b_stdin`

### [phase21j-deterministic-sync-diagnostics](https://github.com/kallmek3nny/ios-27/pull/77)

head: `9e15234388e2` · ci: **passing_checks** · unmerged, not credited.

- **libsystem_pthread**: 6 source files, 27 test/fixture files, +4440/-0 lines
  - candidate declarations: `ios27_21j_acquire`, `ios27_21j_acquire_injected`, `ios27_21j_audit`, `ios27_21j_create`, `ios27_21j_destroy`, `ios27_21j_export`, `ios27_21j_lockorder_acquire`, `ios27_21j_lockorder_audit`, `ios27_21j_lockorder_cancel_wait`, `ios27_21j_lockorder_create`, `ios27_21j_lockorder_destroy`, `ios27_21j_lockorder_edge`

### [phase21f-fifo-mutex-20261028](https://github.com/kallmek3nny/ios-27/pull/78)

head: `6acea7b2995c` · ci: **passing_checks** · unmerged, not credited.

- **libsystem_pthread**: 2 source files, 20 test/fixture files, +2127/-0 lines
  - candidate declarations: `ios27_21f_mutex_create`, `ios27_21f_mutex_destroy`, `ios27_21f_mutex_info`, `ios27_21f_mutex_lock`, `ios27_21f_mutex_trylock`, `ios27_21f_mutex_unlock`, `ios27_21f_runtime_create`, `ios27_21f_runtime_destroy`, `ios27_21f_runtime_stats`

### [agent/phase21i-darwin-pthread-abi](https://github.com/kallmek3nny/ios-27/pull/80)

head: `b552d8499416` · ci: **passing_checks** · unmerged, not credited.

- **libsystem_pthread**: 6 source files, 17 test/fixture files, +2688/-0 lines
  - candidate declarations: `ios27_21i_create`, `ios27_21i_create_apple_lp64_attrs`, `ios27_21i_create_checked`, `ios27_21i_destroy`, `ios27_21i_dispatch`, `ios27_21i_dispatch_arm64`, `ios27_21i_dispatch_arm64_n`, `ios27_21i_errno_to_darwin`, `ios27_21i_get_stats`, `ios27_21i_lookup`, `ios27_21i_verify`

### [feat/phase21d-fair-rwlock-once](https://github.com/kallmek3nny/ios-27/pull/73)

head: `9ef58a1bf4cc` · ci: **passing_checks** · unmerged, not credited.

- **libsystem_pthread**: 2 source files, 12 test/fixture files, +2487/-0 lines
  - candidate declarations: `ios27_21d_create`, `ios27_21d_destroy`, `ios27_21d_once`, `ios27_21d_rdlock`, `ios27_21d_runtime_create`, `ios27_21d_runtime_destroy`, `ios27_21d_timedrdlock`, `ios27_21d_timedwrlock`, `ios27_21d_tryrdlock`, `ios27_21d_trywrlock`, `ios27_21d_unlock`, `ios27_21d_usage`
- **ObjC host prototype (not Apple libobjc)**: 0 source files, 1 test/fixture files, +47/-0 lines

### [phase21a/darwin-pthread-abi-bridge](https://github.com/kallmek3nny/ios-27/tree/phase21a%2Fdarwin-pthread-abi-bridge)

head: `9da58aba0436` · ci: **passing_checks** · unmerged, not credited.

- **libsystem_pthread**: 2 source files, 19 test/fixture files, +2922/-0 lines
  - candidate declarations: `ios27_21a_attach_thread`, `ios27_21a_call`, `ios27_21a_create`, `ios27_21a_create_regions`, `ios27_21a_destroy`, `ios27_21a_detach_thread`, `ios27_21a_register_once`, `ios27_21a_release_once`, `ios27_21a_validate`, `ios27_21a_validate_access`

## recently merged agent source changes · not yet compatibility-reviewed

recent merges appear here even after their purple WIP marker disappears. blue means merged code, **not** a newly verified iOS function.

recent merged PRs with source changes: **7**

### [merged PR #84](https://github.com/kallmek3nny/ios-27/pull/84)

merged: 2026-10-29T00:22:29Z · source changes awaiting function-level audit.

- **unclassified**: 4 source files, 4 fixtures, +1334/-0 lines

### [merged PR #81](https://github.com/kallmek3nny/ios-27/pull/81)

merged: 2026-10-28T21:16:51Z · source changes awaiting function-level audit.

- **libsystem_pthread**: 2 source files, 3 fixtures, +872/-0 lines
  - added declarations: `ios27_21g_cond_broadcast`, `ios27_21g_cond_signal`, `ios27_21g_cond_stats`, `ios27_21g_cond_timedwait`, `ios27_21g_cond_wait`, `ios27_21g_create`, `ios27_21g_destroy`, `ios27_21g_mutex_lock`, `ios27_21g_mutex_trylock`, `ios27_21g_mutex_unlock`, `ios27_21g_runtime_create`, `ios27_21g_runtime_destroy`

### [merged PR #79](https://github.com/kallmek3nny/ios-27/pull/79)

merged: 2026-10-28T21:15:49Z · source changes awaiting function-level audit.

- **libsystem_pthread**: 2 source files, 7 fixtures, +672/-0 lines
  - added declarations: `ios27_21h_get_stats`, `ios27_21h_lock`, `ios27_21h_object_close`, `ios27_21h_object_create`, `ios27_21h_object_destroy`, `ios27_21h_runtime_create`, `ios27_21h_runtime_destroy`, `ios27_21h_trylock`, `ios27_21h_unlock`

### [merged PR #75](https://github.com/kallmek3nny/ios-27/pull/75)

merged: 2026-10-28T20:14:11Z · source changes awaiting function-level audit.

- **libsystem_pthread**: 2 source files, 7 fixtures, +452/-15 lines

### [merged PR #74](https://github.com/kallmek3nny/ios-27/pull/74)

merged: 2026-10-28T20:05:34Z · source changes awaiting function-level audit.

- **libsystem_pthread**: 2 source files, 6 fixtures, +405/-22 lines

### [merged PR #70](https://github.com/kallmek3nny/ios-27/pull/70)

merged: 2026-10-28T19:59:38Z · source changes awaiting function-level audit.

- **libsystem_pthread**: 2 source files, 6 fixtures, +1004/-0 lines
  - added declarations: `ios27_21c_cond_broadcast`, `ios27_21c_cond_signal`, `ios27_21c_cond_timedwait`, `ios27_21c_cond_wait`, `ios27_21c_mutex_lock`, `ios27_21c_mutex_trylock`, `ios27_21c_mutex_unlock`, `ios27_21c_runtime_create`, `ios27_21c_runtime_destroy`, `ios27_21c_sync_create`, `ios27_21c_sync_destroy`, `ios27_21c_usage`

### [merged PR #72](https://github.com/kallmek3nny/ios-27/pull/72)

merged: 2026-10-28T19:56:54Z · source changes awaiting function-level audit.

- **libsystem_pthread**: 2 source files, 4 fixtures, +975/-0 lines
  - added declarations: `ios27_21e_attach`, `ios27_21e_cancel_checkpoint`, `ios27_21e_cancel_register`, `ios27_21e_cancel_request`, `ios27_21e_cancel_setstate`, `ios27_21e_cancel_unregister`, `ios27_21e_cleanup_pop`, `ios27_21e_cleanup_push`, `ios27_21e_cleanup_unwind`, `ios27_21e_create`, `ios27_21e_destroy`, `ios27_21e_getspecific`

## verification policy

changed paths and added declarations are parsed from GitHub PR file patches or main-vs-branch diffs, not commit messages. CI status is a hint only. staged branches may fail integration, omit needed tests, or duplicate already merged work. only separately reviewed catalog updates from merged main can change confirmed target statuses.

Refresh: `timeout 150s python3 tools/scan_branch_activity.py`, then `timeout 15s node tools/compat_blocks.cjs`. a scheduled workflow checks for new agent work at least every 15 minutes, subject to GitHub Actions scheduling delays.

