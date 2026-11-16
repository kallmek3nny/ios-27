#include "images_internal.h"
#include "ios27/initializers.h"

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u |
           (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4u) << 32u;
}
static void wr32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static void wr64(uint8_t *p, uint64_t value) {
    wr32(p, (uint32_t)value);
    wr32(p + 4u, (uint32_t)(value >> 32u));
}
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    assert(file && !fseek(file, 0, SEEK_END));
    length = ftell(file);
    assert(length > 32 && length <= 1024 * 1024 && !fseek(file, 0, SEEK_SET));
    *size = (size_t)length;
    bytes = malloc(*size);
    assert(bytes && fread(bytes, 1u, *size, file) == *size);
    assert(!fclose(file));
    return bytes;
}
static size_t section(const uint8_t *data, const char *name) {
    size_t cursor = 32u;
    for (uint32_t i = 0u; i < rd32(data + 16u); ++i) {
        if (rd32(data + cursor) == 0x19u)
            for (uint32_t j = 0u; j < rd32(data + cursor + 64u); ++j) {
                size_t found = cursor + 72u + (size_t)j * 80u;
                if (!strncmp((const char *)data + found, name, 16u)) return found;
            }
        cursor += rd32(data + cursor + 4u);
    }
    assert(!"missing compiler-produced section");
    return 0;
}
static void zeroed(const void *value, size_t size) {
    const uint8_t *bytes = value;
    for (size_t i = 0u; i < size; ++i) assert(bytes[i] == 0u);
}
static void rejected(const uint8_t *data, size_t size) {
    ios27_initializer_plan plan;
    char error[6] = {'L', 'x', 'x', 'x', 'x', 'R'};
    memset(&plan, 0xa5, sizeof(plan));
    assert(ios27_initializers_inspect(data, size, &plan, error + 1u, 4u) == -1);
    zeroed(&plan, sizeof(plan));
    assert(error[0] == 'L' && error[1] && error[4] == '\0' && error[5] == 'R');
}

static void inspector_checks(const uint8_t *data, size_t size) {
    uint8_t *copy = malloc(size);
    size_t table = section(data, "__init_offsets");
    size_t offset = rd32(data + table + 48u);
    ios27_initializer_plan plan;
    char error[160];
    assert(copy);
    assert(rd64(data + table + 40u) == 8u);
    assert(!ios27_initializers_inspect(data, size, &plan, error, sizeof(error)));
    assert(plan.count == 2u && !plan.unsupported && !plan.entry_vmaddr);
    for (uint32_t target = 0u; target < 4u; ++target) {
        memcpy(copy, data, size);
        wr32(copy + offset, target);
        rejected(copy, size);
    }
    memcpy(copy, data, size); wr32(copy + offset + 4u, UINT32_MAX); rejected(copy, size);
    memcpy(copy, data, size); wr64(copy + table + 40u, 7u); rejected(copy, size);
    memcpy(copy, data, size); wr64(copy + table + 40u, 4u * (IOS27_INITIALIZERS_MAX + 1u)); rejected(copy, size);
    memcpy(copy, data, size); wr64(copy + table + 32u, UINT64_MAX); rejected(copy, size);
    memcpy(copy, data, size); wr32(copy + table + 48u, (uint32_t)offset + 4u); rejected(copy, size);
    memcpy(copy, data, size); wr32(copy + table + 52u, 1u); rejected(copy, size);
    memcpy(copy, data, size); wr32(copy + table + 52u, 31u); rejected(copy, size);
    memcpy(copy, data, size); wr32(copy + table + 60u, 1u); rejected(copy, size);
    memcpy(copy, data, size); wr32(copy + table + 64u, 0u); rejected(copy, size);
    memcpy(copy, data, size); copy[table] = 'X'; rejected(copy, size);
    memcpy(copy, data, size); wr32(copy + 8u, 2u); rejected(copy, size); /* arm64e */
    {
        size_t cursor = 32u;
        int found = 0;
        for (uint32_t i = 0u; i < rd32(data + 16u); ++i) {
            if (rd32(data + cursor) == 0x19u && !memcmp(data + cursor + 8u, "__LINKEDIT", 10u)) {
                memcpy(copy, data, size);
                wr64(copy + cursor + 24u, plan.addresses[0] & ~UINT64_C(4095));
                rejected(copy, size);
                found = 1;
            }
            cursor += rd32(data + cursor + 4u);
        }
        assert(found);
    }
    /* The first image's plan must not escape when the second entry fails. */
    memcpy(copy, data, size); wr32(copy + offset + 4u, 0u); rejected(copy, size);
    for (size_t length = 0u; length < 32u; ++length) {
        uint8_t *prefix = malloc(length ? length : 1u);
        assert(prefix);
        memcpy(prefix, data, length);
        rejected(prefix, length);
        free(prefix);
    }
    {
        const uint32_t flags[] = {9u, 10u};
        const unsigned required[] = {IOS27_INIT_LEGACY_POINTERS, IOS27_INIT_TERMINATORS};
        for (size_t i = 0u; i < 2u; ++i) {
            memcpy(copy, data, size);
            memset(copy + table, 0, 16u);
            memcpy(copy + table, i ? "__mod_term_func" : "__mod_init_func", 15u);
            wr32(copy + table + 64u, flags[i]);
            assert(!ios27_initializers_inspect(copy, size, &plan, error, sizeof(error)));
            assert(plan.count == 0u && plan.unsupported == required[i]);
        }
    }
    /* Repeated entries stay repeated: the linker table, not a symbol set,
     * defines the constructor call sequence. */
    memcpy(copy, data, size); wr32(copy + offset + 4u, rd32(copy + offset));
    assert(!ios27_initializers_inspect(copy, size, &plan, error, sizeof(error)));
    assert(plan.count == 2u && plan.addresses[0] == plan.addresses[1]);
    rejected(NULL, 0u);
    assert(ios27_initializers_inspect(data, size, NULL, NULL, 0u) == -1);
    free(copy);
}

static ios27_images *load_graph(const uint8_t *provider, size_t psize,
                                const uint8_t *main, size_t msize, int reversed) {
    const char *dependencies[] = {"/Fixture/Provider.dylib"};
    ios27_image_spec specs[] = {{"main", main, msize, dependencies, 1u},
                                {"provider", provider, psize, NULL, 0u}};
    const char *paths[] = {"/Fixture/Main", "/Fixture/Provider.dylib"};
    ios27_images *images = NULL;
    char error[192] = {0};
    if (reversed) {
        ios27_image_spec saved = specs[0];
        const char *saved_path = paths[0];
        specs[0] = specs[1]; specs[1] = saved;
        paths[0] = paths[1]; paths[1] = saved_path;
    }
    int status = ios27_images_load_with_paths(specs, 2u, paths, "/Fixture/Main",
                                              &images, error, sizeof(error));
    if (status) fprintf(stderr, "initializer graph load: %s\n", error);
    assert(!status && images);
    return images;
}

static void graph_checks(const uint8_t *provider, size_t psize,
                          const uint8_t *main, size_t msize) {
    ios27_initializer_plan pplan, mplan;
    char error[192];
    assert(!ios27_initializers_inspect(provider, psize, &pplan, error, sizeof(error)));
    assert(!ios27_initializers_inspect(main, msize, &mplan, error, sizeof(error)));
    assert(pplan.count == 2u && mplan.count == 1u && mplan.entry_vmaddr);
    for (int reversed = 0; reversed < 2; ++reversed) {
        ios27_images *images = load_graph(provider, psize, main, msize, reversed);
        ios27_image_startup plan;
        uintptr_t provider_value, main_value;
        ios27_macho_info info;
        assert(!ios27_images_prepare_startup(images, "main", &plan, error, sizeof(error)));
        assert(plan.count == 3u && plan.entry && plan.main_header);
        assert(rd32((const uint8_t *)plan.main_header) == UINT32_C(0xfeedfacf));
        assert(rd32((const uint8_t *)plan.main_header + 12u) == 2u);
        assert(plan.initializers[1] - plan.initializers[0] ==
               (uintptr_t)(pplan.addresses[1] - pplan.addresses[0]));
        assert(plan.entry - plan.initializers[2] ==
               (uintptr_t)(mplan.entry_vmaddr - mplan.addresses[0]));
        assert(!ios27_images_symbol(images, "provider", "_initializer_provider_value", &provider_value));
        assert(!ios27_images_symbol(images, "main", "_initializer_main_value", &main_value));
        assert(*(const int *)provider_value == 0 && *(const int *)main_value == 0);
        assert(!ios27_macho_parse(provider, psize, &info, error, sizeof(error)));
        /* Verify actual instructions were mapped at the planned address. */
        for (size_t i = 0u; i < info.segment_count; ++i) {
            const ios27_segment *s = &info.segments[i];
            if (pplan.addresses[0] >= s->vmaddr && pplan.addresses[0] - s->vmaddr < s->filesize)
                assert(!memcmp((const void *)plan.initializers[0],
                    provider + (size_t)(s->fileoff + pplan.addresses[0] - s->vmaddr), 4u));
        }
        memset(&plan, 0xa5, sizeof(plan));
        assert(ios27_images_prepare_startup(images, "provider", &plan, error, sizeof(error)) == -1);
        zeroed(&plan, sizeof(plan));
        assert(ios27_images_prepare_startup(images, "absent", &plan, error, sizeof(error)) == -1);
        zeroed(&plan, sizeof(plan));
        ios27_images_unload(images);
    }
    {
        uint8_t *bad = malloc(msize);
        ios27_image_startup plan;
        ios27_images *images;
        assert(bad); memcpy(bad, main, msize);
        wr32(bad + rd32(bad + section(bad, "__init_offsets") + 48u), 0u);
        images = load_graph(provider, psize, bad, msize, 0);
        memset(&plan, 0xa5, sizeof(plan));
        assert(ios27_images_prepare_startup(images, "main", &plan, error, sizeof(error)) == -1);
        zeroed(&plan, sizeof(plan));
        ios27_images_unload(images);
        free(bad);
    }
    {
        const char *markers[] = {"__objc_data", "__swift5_types"};
        const char *diagnostics[] = {"Objective-C", "Swift"};
        uint8_t *bad = malloc(psize);
        assert(bad);
        for (size_t i = 0u; i < 2u; ++i) {
            ios27_image_startup plan;
            memcpy(bad, provider, psize);
            size_t code = section(bad, "__text");
            memset(bad + code, 0, 16u);
            memcpy(bad + code, markers[i], strlen(markers[i]));
            ios27_images *images = load_graph(bad, psize, main, msize, 0);
            memset(&plan, 0xa5, sizeof(plan));
            assert(ios27_images_prepare_startup(images, "main", &plan, error, sizeof(error)) == -1);
            assert(strstr(error, diagnostics[i]));
            zeroed(&plan, sizeof(plan));
            ios27_images_unload(images);
        }
        free(bad);
    }
    {
        /* An unrelated image in the explicit catalog must not have its
         * constructors dispatched or its unsupported runtime consulted. */
        const char *deps[] = {"/Fixture/Provider.dylib"};
        const char *paths[] = {"/Fixture/Main", "/Fixture/Provider.dylib", "/Other/Dylib"};
        uint8_t *bad = malloc(psize);
        ios27_images *images = NULL;
        ios27_image_startup plan;
        assert(bad); memcpy(bad, provider, psize);
        wr32(bad + rd32(bad + section(bad, "__init_offsets") + 48u), 0u);
        ios27_image_spec specs[] = {{"main", main, msize, deps, 1u},
                                    {"provider", provider, psize, NULL, 0u},
                                    {"unrelated", bad, psize, NULL, 0u}};
        assert(!ios27_images_load_with_paths(specs, 3u, paths, paths[0], &images, error, sizeof(error)));
        assert(!ios27_images_prepare_startup(images, "main", &plan, error, sizeof(error)));
        assert(plan.count == 3u);
        ios27_images_unload(images);
        free(bad);
    }
    {
        ios27_process_result result;
        ios27_process_options options = {0u};
        ios27_startup_context context = {0, NULL, NULL, NULL};
        memset(&result, 0xa5, sizeof(result));
        assert(ios27_images_run_contained(NULL, "main", &context, &options, &result, error, sizeof(error)) == -1);
        zeroed(&result, sizeof(result));
        assert(ios27_images_run_contained(NULL, "main", &context, NULL, NULL, NULL, 0u) == -1);
    }
}

#if defined(__SANITIZE_ADDRESS__)
#define TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TEST_ASAN 1
#endif
#endif

static int native_checks(const uint8_t *provider, size_t psize,
                           const uint8_t *main, size_t msize) {
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__)) && !defined(TEST_ASAN)
    ios27_images *images = load_graph(provider, psize, main, msize, 0);
    const char *args[] = {"fixture", "normal"};
    const char *env[] = {"EXPLICIT=yes", NULL};
    const char *apple[] = {"APP=fixture", NULL};
    ios27_startup_context context = {2, args, env, apple};
    ios27_process_options options = {1000u};
    ios27_process_result result;
    uintptr_t value;
    char error[192];
    assert(!ios27_images_symbol(images, "provider", "_initializer_provider_value", &value));
    for (size_t i = 0u; i < 8u; ++i) {
        assert(!ios27_images_run_contained(images, "main", &context, &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 42);
        assert(*(const int *)value == 0);
    }
    const char *modes[] = {"trap", "loop", "syscall", "normal"};
    const int signals[] = {SIGTRAP, SIGKILL, SIGSYS, 0};
    for (size_t i = 0u; i < 4u; ++i) {
        args[1] = modes[i];
        options.timeout_ms = i == 1u ? 50u : 1000u;
        assert(!ios27_images_run_contained(images, "main", &context, &options, &result, error, sizeof(error)));
        assert(result.signal == signals[i]);
        assert(result.outcome == (i == 1u ? IOS27_PROCESS_TIMED_OUT :
               i == 3u ? IOS27_PROCESS_RETURNED : IOS27_PROCESS_SIGNALED));
        if (i == 3u) assert(result.exit_code == 42);
        assert(*(const int *)value == 0);
    }
    context.argc = -1;
    memset(&result, 0xa5, sizeof(result));
    assert(ios27_images_run_contained(images, "main", &context, &options, &result, error, sizeof(error)) == -1);
    zeroed(&result, sizeof(result));
    ios27_images_unload(images);
    puts("native C graph: ProgramVars and 3 ordered constructors before main=42, isolated repeat/fault/timeout/syscall recovery passed");
    return 0;
#else
    (void)provider; (void)psize; (void)main; (void)msize;
    puts("native initializer dispatch skipped: requires ARM64 Linux/Android without ASan's filter-incompatible runtime");
    return 77;
#endif
}

int main(int argc, char **argv) {
    size_t psize, msize;
    uint8_t *provider, *entry;
    int result = 0;
    if (argc == 2 && !strcmp(argv[1], "--no-linker")) {
        puts("initializer fixtures skipped: LLVM Mach-O linker unavailable");
        return 77;
    }
    assert(argc == 3 || (argc == 4 && !strcmp(argv[3], "--native")));
    provider = read_file(argv[1], &psize);
    entry = read_file(argv[2], &msize);
    if (argc == 4) result = native_checks(provider, psize, entry, msize);
    else {
        inspector_checks(provider, psize);
        graph_checks(provider, psize, entry, msize);
        puts("compiler-linked initializer inspection and dependency-first mapped startup plans passed; no guest execution");
    }
    free(provider); free(entry);
    return result;
}
