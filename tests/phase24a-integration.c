#include "ios27/phase24a_registry.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIBS 6u
#define THREADS 4u
#define WORKER_ITERATIONS 12000u
#define LOOKUP_ITERATIONS 160000u

typedef struct { uint8_t *bytes; size_t size; } fixture;
typedef struct {
    ios27_phase24a_registry *registry;
    unsigned failures;
} worker_context;

static fixture load(const char *path) {
    fixture data = {NULL, 0u};
    FILE *f = fopen(path, "rb");
    long bytes;
    assert(f);
    assert(fseek(f, 0L, SEEK_END) == 0);
    bytes = ftell(f);
    assert(bytes > 32 && bytes < 4L * 1024L * 1024L);
    assert(fseek(f, 0L, SEEK_SET) == 0);
    data.size = (size_t)bytes;
    data.bytes = malloc(data.size);
    assert(data.bytes);
    assert(fread(data.bytes, 1u, data.size, f) == data.size);
    assert(fclose(f) == 0);
    return data;
}

static void *worker(void *context) {
    worker_context *c = context;
    const char *path = "/app/lib/front.dylib";
    for (unsigned i = 0; i < WORKER_ITERATIONS; ++i) {
        ios27_phase24a_handle h = 0;
        ios27_phase24a_pin pin = 0;
        uintptr_t address = 0;
        char err[128] = {0};
        if (ios27_phase24a_open(c->registry, path, &h, err, sizeof(err)) ||
            ios27_phase24a_symbol(c->registry, h, "_phase24a_front",
                                   &address, &pin, err, sizeof(err)) ||
            !address || !pin ||
            ios27_phase24a_close(c->registry, h, err, sizeof(err)) ||
            ios27_phase24a_release(c->registry, pin, err, sizeof(err))) {
            ++c->failures;
        }
    }
    return NULL;
}

static void check_callable(uintptr_t address, int expected) {
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    int (*fn)(void) = NULL;
    memcpy(&fn, &address, sizeof(fn));
    assert(fn && fn() == expected); /* Actual mapped ARM64 Darwin code. */
#else
    (void)expected; /* x86_64: resolved address only; do not execute ARM64. */
    assert(address);
#endif
}

int main(int argc, char **argv) {
    static const char *const front_deps[] = {"@rpath/left.dylib", "@rpath/right.dylib"};
    static const char *const left_deps[] = {"@rpath/math.dylib", "@rpath/data.dylib"};
    static const char *const right_deps[] = {"@rpath/data.dylib", "@rpath/core.dylib"};
    static const char *const math_deps[] = {"@rpath/core.dylib"};
    static const char *const data_deps[] = {"@rpath/core.dylib"};
    const char *paths[LIBS] = {
        "/app/lib/front.dylib", "/app/lib/left.dylib",
        "/app/lib/right.dylib", "/app/lib/math.dylib",
        "/app/lib/data.dylib", "/app/lib/core.dylib"
    };
    ios27_image_spec specs[LIBS] = {
        {"front.dylib", NULL, 0u, front_deps, 2u},
        {"left.dylib", NULL, 0u, left_deps, 2u},
        {"right.dylib", NULL, 0u, right_deps, 2u},
        {"math.dylib", NULL, 0u, math_deps, 1u},
        {"data.dylib", NULL, 0u, data_deps, 1u},
        {"core.dylib", NULL, 0u, NULL, 0u}
    };
    fixture files[LIBS];
    ios27_phase24a_registry *r = NULL, *bad = NULL;
    ios27_phase24a_handle front = 0, right = 0, tmp = 0, obsolete = 0;
    ios27_phase24a_pin pin = 0, second_pin = 0;
    ios27_phase24a_stats stats;
    uintptr_t entry = 0, data_ptr = 0;
    uint64_t refs = 0;
    pthread_t threads[THREADS];
    worker_context workers[THREADS];
    char err[256] = {0};
    assert(argc == 7);
    for (size_t i = 0u; i < LIBS; ++i) {
        files[i] = load(argv[i + 1u]);
        specs[i].file = files[i].bytes;
        specs[i].size = files[i].size;
    }
    assert(!ios27_phase24a_create(specs, paths, LIBS,
                                   "/app/bin/launcher", &r, err, sizeof(err)));
    assert(ios27_phase24a_open(r, "/app/lib/absent.dylib", &tmp,
                              err, sizeof(err)) == IOS27_PHASE24A_NOT_FOUND);
    assert(!tmp);
    assert(!ios27_phase24a_open(r, paths[0], &front, err, sizeof(err)));
    assert(!ios27_phase24a_open(r, paths[2], &right, err, sizeof(err)));
    assert(front != right);
    assert(!ios27_phase24a_refs(r, paths[5], &refs) && refs == 2u);
    /* Both code and writable data exports come from real mapped Mach-O
     * segments. A separately opened provider must reuse their identity. */
    {
        ios27_phase24a_handle core_handle = 0;
        ios27_phase24a_pin data_pin = 0;
        uintptr_t global = 0;
        int value = -1;
        assert(!ios27_phase24a_open(r, paths[5], &core_handle, err, sizeof(err)));
        assert(!ios27_phase24a_symbol(r, core_handle, "_phase24a_core_value",
                                      &global, &data_pin, err, sizeof(err)));
        assert(global != 0u);
        memcpy(&value, (const void *)global, sizeof(value));
        assert(value == 7);
        assert(!ios27_phase24a_close(r, core_handle, err, sizeof(err)));
        assert(!ios27_phase24a_release(r, data_pin, err, sizeof(err)));
    }
    assert(!ios27_phase24a_symbol(r, front, "_phase24a_front",
                                  &entry, &pin, err, sizeof(err)));
    check_callable(entry, 47);
    assert(!ios27_phase24a_symbol(r, right, "_phase24a_right",
                                  &data_ptr, &second_pin, err, sizeof(err)));
    check_callable(data_ptr, 21);
    assert(!ios27_phase24a_release(r, second_pin, err, sizeof(err)));
    assert(!ios27_phase24a_flat_symbol(r, "_phase24a_choice",
                                       &data_ptr, &second_pin, err, sizeof(err)));
    check_callable(data_ptr, 11); /* strong core outranks right's weak */
    assert(!ios27_phase24a_release(r, second_pin, err, sizeof(err)));
    for (unsigned i = 0; i < LOOKUP_ITERATIONS; ++i) {
        ios27_phase24a_pin p = 0;
        uintptr_t resolved = 0;
        assert(!ios27_phase24a_symbol(r, front, "_phase24a_front",
                                       &resolved, &p, err, sizeof(err)));
        assert(resolved == entry);
        assert(!ios27_phase24a_release(r, p, err, sizeof(err)));
    }
    for (size_t t = 0u; t < THREADS; ++t) {
        workers[t].registry = r;
        workers[t].failures = 0u;
        assert(!pthread_create(&threads[t], NULL, worker, &workers[t]));
    }
    for (size_t t = 0u; t < THREADS; ++t) {
        assert(!pthread_join(threads[t], NULL));
        assert(workers[t].failures == 0u);
    }
    assert(!ios27_phase24a_close(r, right, err, sizeof(err)));
    assert(!ios27_phase24a_close(r, front, err, sizeof(err)));
    /* Last handle closes, but the pinned guest address must remain mapped. */
    check_callable(entry, 47);
    assert(!ios27_phase24a_stats_read(r, &stats));
    assert(stats.live_handles == 0u && stats.active_pins == 1u &&
           stats.graph_maps == 1u && stats.graph_unmaps == 0u);
    assert(ios27_phase24a_release(r, pin, err, sizeof(err)) == 0);
    assert(ios27_phase24a_release(r, pin, err, sizeof(err)) == IOS27_PHASE24A_INVALID);
    assert(!ios27_phase24a_stats_read(r, &stats) && stats.graph_unmaps == 1u);
    obsolete = front;
    assert(ios27_phase24a_symbol(r, obsolete, "_phase24a_front",
                                 &entry, &pin, err, sizeof(err)) == IOS27_PHASE24A_INVALID);
    assert(!ios27_phase24a_open(r, paths[0], &front, err, sizeof(err)));
    assert(front != obsolete);
    assert(ios27_phase24a_close(r, obsolete, err, sizeof(err)) == IOS27_PHASE24A_INVALID);
    assert(!ios27_phase24a_close(r, front, err, sizeof(err)));
    for (unsigned cycle = 0u; cycle < 12u; ++cycle) {
        assert(!ios27_phase24a_open(r, paths[0], &tmp, err, sizeof(err)));
        assert(!ios27_phase24a_close(r, tmp, err, sizeof(err)));
    }
    assert(!ios27_phase24a_stats_read(r, &stats));
    assert(stats.lookups >= 160000u + THREADS * WORKER_ITERATIONS);
    assert(stats.opens >= THREADS * WORKER_ITERATIONS + 16u);
    assert(stats.closes >= THREADS * WORKER_ITERATIONS + 16u);
    assert(stats.graph_maps == stats.graph_unmaps);
    fprintf(stdout, "phase24a: lookups=%llu opens=%llu closes=%llu maps=%llu unmaps=%llu\n",
            (unsigned long long)stats.lookups,
            (unsigned long long)stats.opens,
            (unsigned long long)stats.closes,
            (unsigned long long)stats.graph_maps,
            (unsigned long long)stats.graph_unmaps);
    ios27_phase24a_destroy(r);
    /* Deterministic, bounded malformed-header fault campaign. Every mutation
     * changes one of the four magic bytes and must fail without publishing
     * an image handle, graph, or registry. Seed recorded for reproduction. */
    {
        uint32_t seed = 0x24a52b19u;
        for (unsigned iter = 0u; iter < 1024u; ++iter) {
            size_t index;
            uint8_t saved, delta;
            seed = seed * 1664525u + 1013904223u;
            index = (size_t)((seed >> 8u) & 3u);
            delta = (uint8_t)(1u << ((seed >> 16u) & 7u));
            saved = files[0].bytes[index];
            files[0].bytes[index] ^= delta;
            bad = NULL;
            assert(ios27_phase24a_create(specs, paths, LIBS,
                                          "/app/bin/launcher", &bad,
                                          err, sizeof(err)) == IOS27_PHASE24A_INVALID);
            assert(bad == NULL);
            files[0].bytes[index] = saved;
        }
    }
    /* Recovery from all rejected images must leave clean mappings and
     * ordinary symbol lookup functional on a fresh manager. */
    assert(!ios27_phase24a_create(specs, paths, LIBS,
                                   "/app/bin/launcher", &bad, err, sizeof(err)));
    assert(!ios27_phase24a_open(bad, paths[0], &tmp, err, sizeof(err)));
    assert(!ios27_phase24a_symbol(bad, tmp, "_phase24a_front",
                                  &entry, &pin, err, sizeof(err)));
    check_callable(entry, 47);
    assert(!ios27_phase24a_close(bad, tmp, err, sizeof(err)));
    assert(!ios27_phase24a_release(bad, pin, err, sizeof(err)));
    ios27_phase24a_destroy(bad);
    for (size_t i = 0u; i < LIBS; ++i) free(files[i].bytes);
    return 0;
}
