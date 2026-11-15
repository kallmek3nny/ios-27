/* Original callback ABI contracts. Include the actual patched libkernel
 * declarations; a host build needs only declaration-only Mach type headers. */
#define DARLING 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "_libkernel_init.h"
#include "darling_kernel_callbacks.h"

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static int lookup(const char *name, void **result) { (void)name; (void)result; return 0; }
static void hook(void) { }
static void *find(void *handle, const char *name) { (void)handle; (void)name; return NULL; }

int main(void) {
    struct _libkernel_functions in = {0}, out;
    in.version = DARLING_LIBKERNEL_FUNCTIONS_VERSION;
    in.dlsym = find;
    in.dyld_func_lookup = lookup;
    in.posix_spawn_prepare = hook;
    in.posix_spawn_parent = hook;
    in.posix_spawn_child = hook;
    darling_kernel_callbacks_normalize(&out, &in, NULL);
    CHECK(out.version == 4 && out.dlsym == find);
    CHECK(out.dyld_func_lookup == lookup && out.posix_spawn_prepare == hook);
    CHECK(out.posix_spawn_parent == hook && out.posix_spawn_child == hook);

    /* This is the observed collision: same address slot, public v5 meaning.
     * No unmarked public table may confer any private callback. */
    for (unsigned version = 4; version <= 6; ++version) {
        in.version = version;
        darling_kernel_callbacks_normalize(&out, &in, NULL);
        CHECK(out.version == 4 && out.dlsym == find);
        CHECK(!out.dyld_func_lookup && !out.posix_spawn_prepare && !out.posix_spawn_parent && !out.posix_spawn_child);
    }
    const size_t sizes[] = {sizeof(unsigned long), offsetof(struct _libkernel_functions, reserved1),
        offsetof(struct _libkernel_functions, pthread_clear_qos_tsd),
        offsetof(struct _libkernel_functions, pthread_current_stack_contains_np),
        offsetof(struct _libkernel_functions, dyld_func_lookup)};
    for (unsigned version = 0; version <= 4; ++version) {
        in.version = version;
        void *short_table = malloc(sizes[version]); CHECK(short_table != NULL);
        memcpy(short_table, &in, sizes[version]);
        darling_kernel_callbacks_normalize(&out, short_table, NULL);
        CHECK(out.version == version && !out.dyld_func_lookup);
        CHECK(!out.posix_spawn_prepare && !out.kqueue_close && !out.kqueue_closed_fd);
        if (version) CHECK(out.dlsym == find);
        free(short_table);
    }
    char key[80];
    snprintf(key, sizeof(key), "darling_dyld_lookup=0x%llx", (unsigned long long)(uintptr_t)lookup);
    const char *apple[] = {"unrelated=1", key, NULL};
    in.version = 5;
    darling_kernel_callbacks_normalize(&out, &in, apple);
    CHECK(out.dyld_func_lookup == lookup && !out.posix_spawn_prepare && out.dlsym == find);
    const char *duplicate[] = {key, key, NULL};
    darling_kernel_callbacks_normalize(&out, &in, duplicate);
    CHECK(out.dyld_func_lookup == NULL);
    const char *bad[] = {"darling_dyld_lookup=0x", "darling_dyld_lookup=0x0", "darling_dyld_lookup=0xg",
        "darling_dyld_lookup=0x10000000000000000", "darling_dyld_lookup=0x123tail"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        const char *params[] = {bad[i], NULL};
        darling_kernel_callbacks_normalize(&out, &in, params);
        CHECK(out.dyld_func_lookup == NULL);
    }
    const char **bounded = calloc(512, sizeof(char*)); CHECK(bounded != NULL);
    for (size_t i = 0; i < 512; ++i) bounded[i] = "x";
    darling_kernel_callbacks_normalize(&out, &in, bounded);
    CHECK(out.dyld_func_lookup == NULL);
    free(bounded);
    darling_kernel_callbacks_normalize(&out, NULL, NULL);
    CHECK(out.version == 0 && !out.dlsym && !out.dyld_func_lookup);
    printf("KERNEL_CALLBACKS_OK checks=%u\n", checks);
    return 0;
}
