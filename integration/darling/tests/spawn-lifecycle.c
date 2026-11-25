/* Test the actual hook resolver, ordering, and kernel callback selector. */
#define DARLING 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "_libkernel_init.h"
#include "spawn_lifecycle.h"
#include "spawn_hooks.h"

static unsigned checks, lookups, event_count;
static int missing = -1, optional, external;
static const char *events[64];
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static const char *const names[] = {
    "_dyld_dlopen_atfork_prepare", "_dyld_dlopen_atfork_parent", "_dyld_dlopen_atfork_child",
    "_libSC_info_fork_prepare", "_libSC_info_fork_parent", "_libSC_info_fork_child",
    "xpc_atfork_prepare", "xpc_atfork_parent", "xpc_atfork_child",
    "_dyld_atfork_prepare", "_dyld_atfork_parent", "_dyld_fork_child",
    "_malloc_fork_prepare", "_malloc_fork_parent", "_malloc_fork_child",
    "cc_atfork_prepare", "cc_atfork_parent", "cc_atfork_child",
    "_libc_fork_prepare", "_libc_fork_parent", "_libc_fork_child",
    "_pthread_atfork_prepare", "_pthread_atfork_parent", "_pthread_atfork_child",
    "_asl_fork_child", "_notify_fork_child", "_libtrace_fork_child"
};
static void event(const char *name) { CHECK(event_count < 64); events[event_count++] = name; }
#define HOOK(n) static void hook_##n(void) { event(names[n]); }
HOOK(0) HOOK(1) HOOK(2) HOOK(3) HOOK(4) HOOK(5) HOOK(6) HOOK(7) HOOK(8)
HOOK(9) HOOK(10) HOOK(11) HOOK(12) HOOK(13) HOOK(14) HOOK(15) HOOK(16)
HOOK(17) HOOK(18) HOOK(19) HOOK(20) HOOK(21) HOOK(22) HOOK(23) HOOK(24) HOOK(25) HOOK(26)
static void (*const hooks[])(void) = {
    hook_0, hook_1, hook_2, hook_3, hook_4, hook_5, hook_6, hook_7, hook_8,
    hook_9, hook_10, hook_11, hook_12, hook_13, hook_14, hook_15, hook_16,
    hook_17, hook_18, hook_19, hook_20, hook_21, hook_22, hook_23, hook_24, hook_25, hook_26
};
static void core_child(void) { event("coreservices"); }
static void *lookup(void *handle, const char *name) {
    CHECK(handle == (void *)-2);
    ++lookups;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(name, names[i]) == 0) return (int)i == missing ? NULL : (void *)hooks[i];
    CHECK(strcmp(name, "_libcoreservices_fork_child") == 0);
    return optional ? (void *)core_child : NULL;
}
static void native_parent(void) { event("mach-parent"); }
static void native_child(void) { event("mach-child"); }
static void native_reset(void) { event("native-state"); }
static void tagged_prepare(void) { event("tagged-prepare"); }
static void tagged_parent(void) { event("tagged-parent"); }
static void tagged_child_callback(void) { event("tagged-child"); }
static void expect(const char *const *order, size_t count) {
    CHECK(event_count == count);
    for (size_t i = 0; i < count; ++i) CHECK(strcmp(events[i], order[i]) == 0);
    event_count = 0;
}
typedef int libsimple_lock_t;
#define LIBSIMPLE_LOCK_INITIALIZER 0
static void libsimple_lock_lock(int *lock) { CHECK(*lock == 0); *lock = 1; }
static void libsimple_lock_unlock(int *lock) { CHECK(*lock == 1); *lock = 0; }
int darling_uses_external_libsystem(void) { return external; }
int _mach_fork_parent(void) { native_parent(); return 0; }
int _mach_fork_child(void) { native_child(); return 0; }
void kqueue_atfork(void) { event("kqueue-state"); }
void darling_kevent_workq_fork_child(void) { event("async-state"); }
void darling_workloop_fork_child(void) { event("workloop-state"); }
_libkernel_functions_t _libkernel_functions;
#include SPAWN_LIFECYCLE_IMPLEMENTATION

int main(void) {
    struct darling_spawn_hooks table, unchanged;
    memset(&table, 0xa5, sizeof(table)); unchanged = table;
    CHECK(!darling_spawn_hooks_resolve(&table, NULL));
    CHECK(memcmp(&table, &unchanged, sizeof(table)) == 0);
    for (missing = 0; missing < DSH_COUNT; ++missing) {
        CHECK(!darling_spawn_hooks_resolve(&table, lookup));
        CHECK(memcmp(&table, &unchanged, sizeof(table)) == 0 && event_count == 0);
    }
    missing = -1;
    CHECK(darling_spawn_hooks_resolve(&table, lookup) && !table.coreservices_child);
    const char *const prepare_order[] = {"_dyld_dlopen_atfork_prepare", "_libSC_info_fork_prepare",
        "xpc_atfork_prepare", "_dyld_atfork_prepare", "_malloc_fork_prepare", "cc_atfork_prepare",
        "_libc_fork_prepare", "_pthread_atfork_prepare"};
    const char *const parent_order[] = {"_pthread_atfork_parent", "cc_atfork_parent", "_malloc_fork_parent",
        "_libc_fork_parent", "_dyld_atfork_parent", "xpc_atfork_parent", "_libSC_info_fork_parent",
        "_dyld_dlopen_atfork_parent", "mach-parent"};
    const char *const child_order[] = {"mach-child", "_pthread_atfork_child", "cc_atfork_child",
        "_malloc_fork_child", "_libc_fork_child", "_dyld_fork_child", "native-state",
        "_asl_fork_child", "_notify_fork_child", "xpc_atfork_child", "_libtrace_fork_child",
        "_libSC_info_fork_child", "_dyld_dlopen_atfork_child"};
    darling_spawn_hooks_prepare(&table); expect(prepare_order, sizeof(prepare_order) / sizeof(*prepare_order));
    darling_spawn_hooks_parent(&table, native_parent); expect(parent_order, sizeof(parent_order) / sizeof(*parent_order));
    darling_spawn_hooks_child(&table, native_child, native_reset); expect(child_order, sizeof(child_order) / sizeof(*child_order));
    optional = 1;
    CHECK(darling_spawn_hooks_resolve(&table, lookup) && table.coreservices_child == core_child);
    darling_spawn_hooks_child(&table, native_child, native_reset);
    CHECK(event_count == 14 && strcmp(events[7], "coreservices") == 0);
    event_count = 0; optional = 0;

    struct darling_spawn_handlers selected = {0};
    CHECK(darling_spawn_get_handlers(&selected) == -ENOSYS);
    struct _libkernel_functions callbacks = {0};
    _libkernel_functions = &callbacks;
    callbacks.posix_spawn_prepare = tagged_prepare;
    CHECK(darling_spawn_get_handlers(&selected) == -ENOSYS);
    callbacks.posix_spawn_parent = tagged_parent;
    callbacks.posix_spawn_child = tagged_child_callback;
    CHECK(darling_spawn_get_handlers(&selected) == 0);
    selected.prepare(); selected.parent(); selected.child();
    const char *const tagged_order[] = {"tagged-prepare", "tagged-parent", "tagged-child", "async-state", "workloop-state"};
    expect(tagged_order, sizeof(tagged_order) / sizeof(*tagged_order));

    callbacks = (struct _libkernel_functions){.dlsym = lookup};
    CHECK(darling_spawn_get_handlers(&selected) == -ENOSYS);
    external = 1; missing = 5;
    CHECK(darling_spawn_get_handlers(&selected) == -ENOSYS && !hooks_ready);
    missing = -1;
    CHECK(darling_spawn_get_handlers(&selected) == 0 && hooks_ready);
    unsigned before = lookups;
    CHECK(darling_spawn_get_handlers(&selected) == 0 && lookups == before);
    selected.prepare(); expect(prepare_order, sizeof(prepare_order) / sizeof(*prepare_order));
    selected.parent(); expect(parent_order, sizeof(parent_order) / sizeof(*parent_order));
    selected.child();
    const char *const selected_child_order[] = {"mach-child", "_pthread_atfork_child", "cc_atfork_child",
        "_malloc_fork_child", "_libc_fork_child", "_dyld_fork_child", "kqueue-state", "async-state", "workloop-state",
        "_asl_fork_child", "_notify_fork_child", "xpc_atfork_child", "_libtrace_fork_child",
        "_libSC_info_fork_child", "_dyld_dlopen_atfork_child"};
    expect(selected_child_order, sizeof(selected_child_order) / sizeof(*selected_child_order));
    printf("SPAWN_LIFECYCLE_OK checks=%u\n", checks);
    return 0;
}
