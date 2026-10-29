#include "ios27/phase23a_loader.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PHASE23A_IMAGES 5u
#define PHASE23A_WORKERS 4u
#define PHASE23A_WORK_PER_THREAD 4000u

typedef struct {
    uint8_t *data;
    size_t size;
} file_data;

static file_data read_file(const char *path) {
    file_data result = {0};
    FILE *file = fopen(path, "rb");
    long size;
    assert(file != NULL);
    assert(fseek(file, 0L, SEEK_END) == 0);
    size = ftell(file);
    assert(size > 32L && size <= 16L * 1024L * 1024L);
    assert(fseek(file, 0L, SEEK_SET) == 0);
    result.data = malloc((size_t)size);
    assert(result.data != NULL);
    result.size = (size_t)size;
    assert(fread(result.data, 1u, result.size, file) == result.size);
    assert(fclose(file) == 0);
    return result;
}

typedef struct {
    ios27_phase23a_loader *loader;
    ios27_phase23a_handle anchor;
    unsigned failures;
} worker_args;

static void *worker(void *opaque) {
    worker_args *worker = opaque;
    for (unsigned i = 0u; i < PHASE23A_WORK_PER_THREAD; ++i) {
        char error[160] = {0};
        uintptr_t symbol = 0u;
        ios27_phase23a_handle copy = 0u;
        if (ios27_phase23a_open(worker->loader, "top.dylib", &copy,
                               error, sizeof(error)) != IOS27_PHASE23A_OK ||
            copy != worker->anchor ||
            ios27_phase23a_symbol(worker->loader, copy, "_phase23a_top",
                                  &symbol, error, sizeof(error)) ||
            !symbol ||
            ios27_phase23a_close(worker->loader, copy,
                                 error, sizeof(error)))
            ++worker->failures;
    }
    return NULL;
}

/* Modify a genuine compiler-linked LC_LOAD_DYLIB install name to create a
 * cyclic graph. Mach-O load commands retain correct sizes and offsets. */
static void patch_middle_cycle(uint8_t *image, size_t size) {
    size_t cursor = 32u;
    unsigned seen = 0u;
    uint32_t count;
    assert(size >= 32u);
    count = (uint32_t)image[16] | ((uint32_t)image[17] << 8u);
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t command, length, off;
        assert(cursor <= size && size - cursor >= 24u);
        command = (uint32_t)image[cursor] |
                  ((uint32_t)image[cursor + 1u] << 8u);
        length = (uint32_t)image[cursor + 4u] |
                 ((uint32_t)image[cursor + 5u] << 8u);
        assert(length >= 24u && length <= size - cursor);
        if (command == 0x0cu) {
            off = (uint32_t)image[cursor + 8u];
            assert(off >= 24u && off + sizeof("top.dylib") <= length);
            assert(strcmp((const char *)(image + cursor + off),
                          "base.dylib") == 0);
            memcpy(image + cursor + off, "top.dylib", sizeof("top.dylib"));
            seen = 1u;
            break;
        }
        cursor += (size_t)length;
    }
    assert(seen);
}

int main(int argc, char **argv) {
    static const char *const top_deps[] = {"facade.dylib", "base.dylib"};
    static const char *const facade_deps[] = {"mid.dylib", "util.dylib"};
    static const char *const util_deps[] = {"base.dylib"};
    static const char *const mid_deps[] = {"base.dylib"};
    static const char *const cyclic_mid_deps[] = {"top.dylib"};
    file_data files[PHASE23A_IMAGES];
    ios27_image_spec specs[PHASE23A_IMAGES] = {
        {"top.dylib", NULL, 0u, top_deps, 2u},
        {"facade.dylib", NULL, 0u, facade_deps, 2u},
        {"util.dylib", NULL, 0u, util_deps, 1u},
        {"mid.dylib", NULL, 0u, mid_deps, 1u},
        {"base.dylib", NULL, 0u, NULL, 0u}
    };
    ios27_phase23a_loader *loader = NULL, *cycle_loader = NULL;
    ios27_phase23a_handle top = 0u, duplicate = 0u, mid = 0u;
    ios27_phase23a_handle util = 0u, base = 0u, old = 0u;
    ios27_phase23a_diagnostics diag;
    pthread_t workers[PHASE23A_WORKERS];
    worker_args args[PHASE23A_WORKERS];
    uintptr_t entry = 0u, flat = 0u, base_symbol = 0u;
    uint64_t refs = 0u;
    char error[192] = {0};
    assert(argc == 6);
    for (size_t i = 0u; i < PHASE23A_IMAGES; ++i) {
        files[i] = read_file(argv[i + 1u]);
        specs[i].file = files[i].data;
        specs[i].size = files[i].size;
    }
    assert(ios27_phase23a_create(specs, PHASE23A_IMAGES, &loader,
                                 error, sizeof(error)) == IOS27_PHASE23A_OK);
    assert(ios27_phase23a_open(loader, "missing.dylib", &top,
                              error, sizeof(error)) == IOS27_PHASE23A_NOT_FOUND);
    assert(top == 0u);
    assert(ios27_phase23a_open(loader, "top.dylib", &top,
                              error, sizeof(error)) == IOS27_PHASE23A_OK);
    assert(ios27_phase23a_symbol(loader, top, "_phase23a_top", &entry,
                                error, sizeof(error)) == IOS27_PHASE23A_OK);
    assert(entry != 0u);
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    {
        int (*call)(void) = NULL;
        memcpy(&call, &entry, sizeof(call));
        assert(call != NULL && call() == 31);
    }
#endif
    assert(ios27_phase23a_open(loader, "top.dylib", &duplicate,
                              error, sizeof(error)) == IOS27_PHASE23A_OK);
    assert(top == duplicate);
    assert(ios27_phase23a_reference_count(loader, "base.dylib", &refs) == 0);
    assert(refs == 2u);
    assert(ios27_phase23a_close(loader, duplicate,
                               error, sizeof(error)) == IOS27_PHASE23A_OK);
    assert(ios27_phase23a_open(loader, "mid.dylib", &mid,
                              error, sizeof(error)) == IOS27_PHASE23A_OK);
    assert(ios27_phase23a_reference_count(loader, "base.dylib", &refs) == 0);
    assert(refs == 2u);
    assert(ios27_phase23a_close(loader, mid,
                               error, sizeof(error)) == IOS27_PHASE23A_OK);
    assert(ios27_phase23a_open(loader, "util.dylib", &util,
                              error, sizeof(error)) == IOS27_PHASE23A_OK);
    assert(ios27_phase23a_open(loader, "base.dylib", &base,
                              error, sizeof(error)) == IOS27_PHASE23A_OK);
    assert(ios27_phase23a_symbol(loader, base, "_phase23a_base", &base_symbol,
                                error, sizeof(error)) == IOS27_PHASE23A_OK);
    assert(ios27_phase23a_flat_symbol(loader, "_phase23a_choice", &flat,
                                     error, sizeof(error)) == IOS27_PHASE23A_OK);
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    {
        int (*choice)(void) = NULL;
        memcpy(&choice, &flat, sizeof(choice));
        assert(choice != NULL && choice() == 9); /* strong beats weak */
    }
#endif
    assert(ios27_phase23a_symbol(loader, top, "_phase23a_absent", &flat,
                                error, sizeof(error)) == IOS27_PHASE23A_NOT_FOUND);
    assert(flat == 0u);
    assert(ios27_phase23a_close(loader, util, error, sizeof(error)) == 0);
    assert(ios27_phase23a_close(loader, base, error, sizeof(error)) == 0);

    for (unsigned i = 0u; i < 120000u; ++i) {
        assert(ios27_phase23a_symbol(loader, top, "_phase23a_top", &flat,
                                    error, sizeof(error)) == 0 && flat == entry);
        assert(ios27_phase23a_flat_symbol(loader, "_phase23a_base", &flat,
                                         error, sizeof(error)) == 0 && flat != 0u);
    }
    for (unsigned i = 0u; i < 70000u; ++i) {
        assert(ios27_phase23a_open(loader, "top.dylib", &duplicate,
                                  error, sizeof(error)) == 0 && duplicate == top);
        assert(ios27_phase23a_close(loader, duplicate, error, sizeof(error)) == 0);
    }
    for (unsigned w = 0u; w < PHASE23A_WORKERS; ++w) {
        args[w].loader = loader;
        args[w].anchor = top;
        args[w].failures = 0u;
        assert(pthread_create(&workers[w], NULL, worker, &args[w]) == 0);
    }
    for (unsigned w = 0u; w < PHASE23A_WORKERS; ++w) {
        assert(pthread_join(workers[w], NULL) == 0);
        assert(args[w].failures == 0u);
    }
    old = top;
    assert(ios27_phase23a_close(loader, top, error, sizeof(error)) == 0);
    assert(ios27_phase23a_close(loader, old, error, sizeof(error)) ==
           IOS27_PHASE23A_INVALID);
    assert(ios27_phase23a_symbol(loader, old, "_phase23a_top", &flat,
                                error, sizeof(error)) == IOS27_PHASE23A_INVALID);
    assert(ios27_phase23a_reference_count(loader, "base.dylib", &refs) == 0);
    assert(refs == 0u);
    assert(ios27_phase23a_open(loader, "top.dylib", &top,
                              error, sizeof(error)) == 0);
    assert(top != old); /* ABA-safe generation */
    assert(ios27_phase23a_close(loader, top, error, sizeof(error)) == 0);
    for (unsigned i = 0u; i < 300u; ++i) {
        assert(ios27_phase23a_open(loader, "base.dylib", &base,
                                  error, sizeof(error)) == 0);
        assert(ios27_phase23a_close(loader, base, error, sizeof(error)) == 0);
    }

    /* The modified Clang/LLD binary declares a real top -> facade -> mid
     * -> top cycle. Reject it before mapping and keep other images usable. */
    {
        ios27_image_spec changed[PHASE23A_IMAGES];
        uint8_t *cyclic = malloc(files[3].size);
        assert(cyclic != NULL);
        memcpy(cyclic, files[3].data, files[3].size);
        patch_middle_cycle(cyclic, files[3].size);
        memcpy(changed, specs, sizeof(changed));
        changed[3].file = cyclic;
        changed[3].dependencies = cyclic_mid_deps;
        assert(ios27_phase23a_create(changed, PHASE23A_IMAGES, &cycle_loader,
                                     error, sizeof(error)) == 0);
        assert(ios27_phase23a_open(cycle_loader, "top.dylib", &top,
                                  error, sizeof(error)) != 0);
        assert(top == 0u && strstr(error, "cyclic") != NULL);
        assert(ios27_phase23a_open(cycle_loader, "base.dylib", &base,
                                  error, sizeof(error)) == 0);
        assert(ios27_phase23a_close(cycle_loader, base,
                                   error, sizeof(error)) == 0);
        ios27_phase23a_destroy(cycle_loader);
        free(cyclic);
    }
    assert(ios27_phase23a_diagnostics_read(loader, &diag) == 0);
    assert(diag.lookups >= 240000u);
    assert(diag.opens == diag.closes);
    assert(diag.mapping_loads == diag.mapping_unloads);
    assert(diag.live_roots == 0u);
    printf("phase23a PASS: lookups=%llu opens=%llu closes=%llu "
           "mapping_loads=%llu failures=%llu on %s\n",
           (unsigned long long)diag.lookups,
           (unsigned long long)diag.opens,
           (unsigned long long)diag.closes,
           (unsigned long long)diag.mapping_loads,
           (unsigned long long)diag.failures,
#if defined(__aarch64__)
           "arm64 native"
#else
           "host parser+mapping (no arm64 execution)"
#endif
    );
    ios27_phase23a_destroy(loader);
    for (size_t i = 0u; i < PHASE23A_IMAGES; ++i) free(files[i].data);
    return 0;
}
