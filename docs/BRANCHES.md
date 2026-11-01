# ios-27 | live agent branch source diffs

![proportional compatibility blocks with unmerged and merged-source markers](blocks.svg)

**purple dashed = active unmerged branch; blue solid = recently merged source awaiting compatibility audit.** neither is a verified Apple ABI function or application. the verified green/amber counts remain strictly controlled by reviewed main catalogs.

candidate branches with mapped changes: **31** · audited base: `6341edbb6ecb`

### [codex/loader-persona](https://github.com/kallmek3nny/ios-27/pull/121)

head: `371cf59ededf` · ci: **passing_checks** · unmerged, not credited.

- **unclassified**: 2 source files, 1 test/fixture files, +101/-13 lines

### [phase26e-android-host-scenes-input](https://github.com/kallmek3nny/ios-27/pull/112)

head: `63310d737d10` · ci: **passing_checks** · unmerged, not credited.

- **Android host integration**: 4 source files, 1 test/fixture files, +1173/-0 lines

### [phase26a-native-libc-abi](https://github.com/kallmek3nny/ios-27/pull/109)

head: `c710a3bdcb12` · ci: **passing_checks** · unmerged, not credited.

- **libsystem_c / libc**: 2 source files, 5 test/fixture files, +943/-0 lines
  - candidate declarations: `ios27_26a_calloc`, `ios27_26a_free`, `ios27_26a_getenv`, `ios27_26a_last_error`, `ios27_26a_live_bytes`, `ios27_26a_malloc`, `ios27_26a_memcmp`, `ios27_26a_memcpy`, `ios27_26a_memmove`, `ios27_26a_memset`, `ios27_26a_realloc`, `ios27_26a_setenv`

### [phase26c-headless-graphics-interop-20261030](https://github.com/kallmek3nny/ios-27/pull/111)

head: `90a78d0307fa` · ci: **passing_checks** · unmerged, not credited.

- **Metal / IOSurface / gpu**: 4 source files, 1 test/fixture files, +870/-0 lines

### [phase26d-coreanimation-compositor](https://github.com/kallmek3nny/ios-27/pull/108)

head: `6bdcffce6c7b` · ci: **passing_checks** · unmerged, not credited.

- **CoreAnimation / compositor**: 2 source files, 2 test/fixture files, +927/-0 lines

### [agent/phase26b-bsd-capabilities-20261030](https://github.com/kallmek3nny/ios-27/pull/110)

head: `ab6320cdc916` · ci: **passing_checks** · unmerged, not credited.

- **bsd syscall boundary**: 2 source files, 3 test/fixture files, +718/-0 lines
  - candidate declarations: `ios27_phase26b_copyin`, `ios27_phase26b_copyout`, `ios27_phase26b_create`, `ios27_phase26b_destroy`, `ios27_phase26b_stop`, `ios27_phase26b_trap`

### [agent/phase25a-objc-linked-metadata-20261030](https://github.com/kallmek3nny/ios-27/pull/105)

head: `a9bcda8e7432` · ci: **passing_checks** · unmerged, not credited.

- **ObjC host prototype (not Apple libobjc)**: 3 source files, 8 test/fixture files, +1098/-0 lines
  - candidate declarations: `ios27_phase25a_preflight`

### [phase25b-native-objc-dispatch](https://github.com/kallmek3nny/ios-27/pull/106)

head: `99e3678e6241` · ci: **passing_checks** · unmerged, not credited.

- **ObjC host prototype (not Apple libobjc)**: 3 source files, 13 test/fixture files, +1275/-0 lines

### [phase25c-arc-ownership-20261030](https://github.com/kallmek3nny/ios-27/pull/102)

head: `70d7d9826a03` · ci: **passing_checks** · unmerged, not credited.

- **ObjC host prototype (not Apple libobjc)**: 2 source files, 8 test/fixture files, +1179/-0 lines
  - candidate declarations: `ios27_phase25c_alloc`, `ios27_phase25c_autorelease`, `ios27_phase25c_can_unload`, `ios27_phase25c_create`, `ios27_phase25c_destroy`, `ios27_phase25c_destroy_weak`, `ios27_phase25c_fail_alloc_after`, `ios27_phase25c_get_stats`, `ios27_phase25c_identity`, `ios27_phase25c_load_weak_retained`, `ios27_phase25c_pin`, `ios27_phase25c_pool_pop`

### [agent/phase25e-objc-app-integration-20261030](https://github.com/kallmek3nny/ios-27/pull/103)

head: `ae4bdd578843` · ci: **passing_checks** · unmerged, not credited.

- **ObjC host prototype (not Apple libobjc)**: 0 source files, 4 test/fixture files, +826/-0 lines

### [agent/phase25d-objc-lifecycle-20261030](https://github.com/kallmek3nny/ios-27/pull/104)

head: `7c434d616cf2` · ci: **passing_checks** · unmerged, not credited.

- **ObjC host prototype (not Apple libobjc)**: 2 source files, 12 test/fixture files, +922/-0 lines

### [phase24c-objc2-typed-metadata-dispatch](https://github.com/kallmek3nny/ios-27/pull/100)

head: `4b7fe765e9b8` · ci: **passing_checks** · unmerged, not credited.

- **ObjC host prototype (not Apple libobjc)**: 2 source files, 12 test/fixture files, +847/-0 lines

### [phase24d-darwin-services-20261029](https://github.com/kallmek3nny/ios-27/pull/101)

head: `26cfbfabc081` · ci: **passing_checks** · unmerged, not credited.

- **bsd syscall boundary**: 2 source files, 10 test/fixture files, +954/-0 lines
  - candidate declarations: `ios27_24d_free`, `ios27_24d_get_stats`, `ios27_24d_memory_protect`, `ios27_24d_memory_read`, `ios27_24d_memory_write`, `ios27_24d_new`, `ios27_24d_poll_dead`, `ios27_24d_port_allocate`, `ios27_24d_port_destroy`, `ios27_24d_port_grant`, `ios27_24d_process_create`, `ios27_24d_process_destroy`

### [agent/phase24b-native-lifecycle-20261029](https://github.com/kallmek3nny/ios-27/pull/99)

head: `f1064a7910d1` · ci: **passing_checks** · unmerged, not credited.

- **dyld / image graph**: 2 source files, 8 test/fixture files, +1315/-0 lines
  - candidate declarations: `ios27_24b_bind`, `ios27_24b_close`, `ios27_24b_ensure`, `ios27_24b_inspect`, `ios27_24b_open`, `ios27_24b_parse`, `ios27_24b_register`, `ios27_24b_runtime_destroy`, `ios27_24b_runtime_init`

### [phase24a-global-dyld-identity](https://github.com/kallmek3nny/ios-27/pull/97)

head: `982d7575cdc5` · ci: **passing_checks** · unmerged, not credited.

- **dyld / image graph**: 2 source files, 2 test/fixture files, +970/-0 lines
  - candidate declarations: `ios27_phase24a_close`, `ios27_phase24a_create`, `ios27_phase24a_destroy`, `ios27_phase24a_flat_symbol`, `ios27_phase24a_open`, `ios27_phase24a_refs`, `ios27_phase24a_release`, `ios27_phase24a_stats_read`, `ios27_phase24a_symbol`

### [phase24e-native-application-startup](https://github.com/kallmek3nny/ios-27/pull/98)

head: `872607e045f9` · ci: **passing_checks** · unmerged, not credited.

- **mach-o parser and native loader**: 2 source files, 3 test/fixture files, +1056/-0 lines
  - candidate declarations: `ios27_24e_enter_main`, `ios27_24e_run`

### [phase23c-objc-native-bridge](https://github.com/kallmek3nny/ios-27/pull/94)

head: `f6c0ee963e39` · ci: **passing_checks** · unmerged, not credited.

- **ObjC host prototype (not Apple libobjc)**: 2 source files, 6 test/fixture files, +771/-0 lines

### [agent/phase23b-lifecycle-20261029](https://github.com/kallmek3nny/ios-27/pull/96)

head: `7988310ebdb2` · ci: **passing_checks** · unmerged, not credited.

- **dyld / image graph**: 2 source files, 9 test/fixture files, +1005/-0 lines
  - candidate declarations: `ios27_23b_add`, `ios27_23b_bind`, `ios27_23b_close`, `ios27_23b_ensure`, `ios27_23b_open`, `ios27_23b_parse`, `ios27_23b_runtime_destroy`, `ios27_23b_runtime_init`, `ios27_23b_snapshot`

### [phase23d-syscall-mach-bridge-20261029](https://github.com/kallmek3nny/ios-27/pull/95)

head: `72e3f24e5f81` · ci: **passing_checks** · unmerged, not credited.

- **bsd syscall boundary**: 2 source files, 7 test/fixture files, +1021/-0 lines
  - candidate declarations: `ios27_23d_free`, `ios27_23d_get_stats`, `ios27_23d_memory_protect`, `ios27_23d_memory_read`, `ios27_23d_memory_write`, `ios27_23d_new`, `ios27_23d_process_create`, `ios27_23d_process_destroy`, `ios27_23d_seed`, `ios27_23d_service_destroy`, `ios27_23d_service_grant`, `ios27_23d_service_register`

### [phase23e/native-app-integration-harness](https://github.com/kallmek3nny/ios-27/pull/93)

head: `77cb73c4836c` · ci: **passing_checks** · unmerged, not credited.

- **mach-o parser and native loader**: 2 source files, 3 test/fixture files, +961/-0 lines
  - candidate declarations: `ios27_23e_application_probe`, `ios27_23e_library_stress`

### [phase23a-incremental-dyld-namespaces](https://github.com/kallmek3nny/ios-27/pull/92)

head: `280b1daed6f8` · ci: **passing_checks** · unmerged, not credited.

- **dyld / image graph**: 2 source files, 2 test/fixture files, +991/-0 lines
  - candidate declarations: `ios27_phase23a_close`, `ios27_phase23a_create`, `ios27_phase23a_destroy`, `ios27_phase23a_diagnostics_read`, `ios27_phase23a_flat_symbol`, `ios27_phase23a_open`, `ios27_phase23a_reference_count`, `ios27_phase23a_symbol`

### [phase22e-mach-ipc-mig](https://github.com/kallmek3nny/ios-27/pull/87)

head: `ad180562e914` · ci: **passing_checks** · unmerged, not credited.

- **mach ipc model**: 2 source files, 7 test/fixture files, +1448/-0 lines

### [agent/phase22c-transactional-dyld-graph](https://github.com/kallmek3nny/ios-27/pull/83)

head: `1a277ef5e829` · ci: **passing_checks** · unmerged, not credited.

- **dyld / image graph**: 2 source files, 5 test/fixture files, +1302/-0 lines
  - candidate declarations: `ios27_22c_acquire`, `ios27_22c_create`, `ios27_22c_create_with_paths`, `ios27_22c_destroy`, `ios27_22c_initialize`, `ios27_22c_load`, `ios27_22c_order`, `ios27_22c_pins`, `ios27_22c_release`, `ios27_22c_resolve`, `ios27_22c_symbol`, `ios27_22c_unload`

### [phase22a-guest-address-spaces](https://github.com/kallmek3nny/ios-27/pull/85)

head: `7e29a48f2fd6` · ci: **passing_checks** · unmerged, not credited.

- **guest security / isolation**: 3 source files, 9 test/fixture files, +1109/-0 lines
  - candidate declarations: `ios27_22a_close`, `ios27_22a_copyin`, `ios27_22a_copyout`, `ios27_22a_create`, `ios27_22a_destroy`, `ios27_22a_fail_map_after`, `ios27_22a_fail_stage`, `ios27_22a_map`, `ios27_22a_map_stack`, `ios27_22a_pin`, `ios27_22a_probe`, `ios27_22a_protect`

### [phase22b-darwin-bsd-capability-20261028](https://github.com/kallmek3nny/ios-27/pull/86)

head: `e909e8faed23` · ci: **passing_checks** · unmerged, not credited.

- **bsd syscall boundary**: 2 source files, 3 test/fixture files, +642/-0 lines
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

- **libsystem_pthread**: 2 source files, 13 test/fixture files, +2534/-0 lines
  - candidate declarations: `ios27_21d_create`, `ios27_21d_destroy`, `ios27_21d_once`, `ios27_21d_rdlock`, `ios27_21d_runtime_create`, `ios27_21d_runtime_destroy`, `ios27_21d_timedrdlock`, `ios27_21d_timedwrlock`, `ios27_21d_tryrdlock`, `ios27_21d_trywrlock`, `ios27_21d_unlock`, `ios27_21d_usage`

### [phase21a/darwin-pthread-abi-bridge](https://github.com/kallmek3nny/ios-27/pull/69)

head: `9da58aba0436` · ci: **passing_checks** · unmerged, not credited.

- **libsystem_pthread**: 2 source files, 20 test/fixture files, +3007/-0 lines
  - candidate declarations: `ios27_21a_attach_thread`, `ios27_21a_call`, `ios27_21a_create`, `ios27_21a_create_regions`, `ios27_21a_destroy`, `ios27_21a_detach_thread`, `ios27_21a_register_once`, `ios27_21a_release_once`, `ios27_21a_validate`, `ios27_21a_validate_access`

### [phase21e-exit-cancellation-20261028](https://github.com/kallmek3nny/ios-27/pull/76)

head: `298e5e8bcf28` · ci: **passing_checks** · unmerged, not credited.

- **libsystem_pthread**: 1 source files, 5 test/fixture files, +358/-11 lines

## recently merged agent source changes · not yet compatibility-reviewed

recent merges appear here even after their purple WIP marker disappears. blue means merged code, **not** a newly verified iOS function.

recent merged PRs with source changes: **20**

### [merged PR #120](https://github.com/kallmek3nny/ios-27/pull/120)

merged: 2026-11-01T17:58:30Z · source changes awaiting function-level audit.

- **unclassified**: 1 source files, 1 fixtures, +44/-12 lines

### [merged PR #119](https://github.com/kallmek3nny/ios-27/pull/119)

merged: 2026-11-01T17:33:13Z · source changes awaiting function-level audit.

- **unclassified**: 1 source files, 1 fixtures, +189/-14 lines

### [merged PR #118](https://github.com/kallmek3nny/ios-27/pull/118)

merged: 2026-11-01T17:00:58Z · source changes awaiting function-level audit.

- **unclassified**: 1 source files, 1 fixtures, +64/-11 lines

### [merged PR #117](https://github.com/kallmek3nny/ios-27/pull/117)

merged: 2026-11-01T16:03:20Z · source changes awaiting function-level audit.

- **unclassified**: 3 source files, 1 fixtures, +265/-5 lines
  - added declarations: `ios27_process_install_loader_filter`

### [merged PR #116](https://github.com/kallmek3nny/ios-27/pull/116)

merged: 2026-11-01T14:31:29Z · source changes awaiting function-level audit.

- **unclassified**: 1 source files, 1 fixtures, +185/-4 lines

### [merged PR #115](https://github.com/kallmek3nny/ios-27/pull/115)

merged: 2026-10-31T21:43:46Z · source changes awaiting function-level audit.

- **mach-o parser and native loader**: 1 source files, 0 fixtures, +40/-3 lines
  - added declarations: `ios27_native_handoff`
- **unclassified**: 4 source files, 2 fixtures, +1213/-146 lines
  - added declarations: `ios27_launch_payload_close`, `ios27_launch_payload_create`, `ios27_launch_payload_open`, `ios27_native_handoff`, `ios27_process_close_outside`, `ios27_process_install_filter`, `ios27_process_report`, `ios27_process_supervise`, `ios27_process_supervise_exec`, `ios27_run_executable_exec`

### [merged PR #114](https://github.com/kallmek3nny/ios-27/pull/114)

merged: 2026-10-31T19:28:16Z · source changes awaiting function-level audit.

- **mach-o parser and native loader**: 1 source files, 0 fixtures, +56/-9 lines
  - added declarations: `ios27_run_executable_contained`, `ios27_run_executable_context`
- **unclassified**: 4 source files, 2 fixtures, +911/-3 lines
  - added declarations: `ios27_process_supervise`, `ios27_run_executable_contained`

### [merged PR #91](https://github.com/kallmek3nny/ios-27/pull/91)

merged: 2026-10-29T10:41:02Z · source changes awaiting function-level audit.

- **libSystem guest model (not dylib)**: 2 source files, 2 fixtures, +4/-4 lines

### [merged PR #84](https://github.com/kallmek3nny/ios-27/pull/84)

merged: 2026-10-29T00:22:29Z · source changes awaiting function-level audit.

- **ObjC host prototype (not Apple libobjc)**: 4 source files, 4 fixtures, +1334/-0 lines

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

### [merged PR #71](https://github.com/kallmek3nny/ios-27/pull/71)

merged: 2026-10-28T19:54:07Z · source changes awaiting function-level audit.

- **libsystem_pthread**: 2 source files, 3 fixtures, +774/-0 lines
  - added declarations: `ios27_21b_attr_init`, `ios27_21b_attr_validate`, `ios27_21b_runtime_create`, `ios27_21b_runtime_destroy`, `ios27_21b_runtime_usage`, `ios27_21b_runtime_wait_idle`, `ios27_21b_thread_create`, `ios27_21b_thread_detach`, `ios27_21b_thread_equal`, `ios27_21b_thread_info`, `ios27_21b_thread_join`, `ios27_21b_thread_self`

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

- **ObjC host prototype (not Apple libobjc)**: 3 source files, 4 fixtures, +655/-0 lines
  - added declarations: `ios27_objc20b_alloc`, `ios27_objc20b_bind`, `ios27_objc20b_create`, `ios27_objc20b_destroy`, `ios27_objc20b_register`, `ios27_objc20b_release`, `ios27_objc20b_replace`, `ios27_objc20b_resolve`, `ios27_objc20b_resolve_global`, `ios27_objc20b_resolve_super_global`, `ios27_objc20b_selector`, `ios27_objc20b_send_words`

### [merged PR #64](https://github.com/kallmek3nny/ios-27/pull/64)

merged: 2026-10-28T19:20:45Z · source changes awaiting function-level audit.

- **libsystem_pthread**: 2 source files, 4 fixtures, +983/-0 lines
  - added declarations: `ios27_20d_cancel_point`, `ios27_20d_cancel_request`, `ios27_20d_cond_signal`, `ios27_20d_cond_wait`, `ios27_20d_create`, `ios27_20d_destroy`, `ios27_20d_getspecific`, `ios27_20d_key_create`, `ios27_20d_key_delete`, `ios27_20d_mutex_lock`, `ios27_20d_mutex_trylock`, `ios27_20d_mutex_unlock`

## verification policy

changed paths and added declarations are parsed from GitHub PR file patches or main-vs-branch diffs, not commit messages. CI status is a hint only. staged branches may fail integration, omit needed tests, or duplicate already merged work. only separately reviewed catalog updates from merged main can change confirmed target statuses.

Refresh: `timeout 150s python3 tools/scan_branch_activity.py`, then `timeout 15s node tools/compat_blocks.cjs`. a scheduled workflow checks for new agent work at least every 15 minutes, subject to GitHub Actions scheduling delays.

