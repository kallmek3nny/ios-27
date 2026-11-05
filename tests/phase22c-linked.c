#include "ios27/phase22c_graph.h"
#include "ios27/dyld_paths.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIMIT (4u * 1024u * 1024u)
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long n = -1;
    if (!f) return NULL;
    if (!fseek(f, 0L, SEEK_END)) n = ftell(f);
    if (n < 32 || (unsigned long)n > LIMIT || fseek(f, 0L, SEEK_SET))
        goto done;
    buf = malloc((size_t)n);
    if (buf && fread(buf, 1u, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf) *size = (size_t)n;
done:
    (void)fclose(f);
    return buf;
}
typedef struct {
    size_t trace[16], count;
} log_state;
static int init(void *p, size_t index) {
    log_state *s = p;
    assert(s->count < 16u);
    s->trace[s->count++] = index;
    return 0;
}
static void fini(void *p, size_t index) {
    log_state *s = p;
    assert(s->count < 16u);
    s->trace[s->count++] = 100u + index;
}
int main(int argc, char **argv) {
    static const char *const paths[] = {
        "/App/Main", "/App/Plugins/Middle.dylib",
        "/App/Frameworks/Leaf.dylib", "/App/Frameworks/Base.dylib"
    };
    static const char *const names[] = {
        "root.dylib", "middle.dylib", "leaf.dylib", "base.dylib"
    };
    ios27_dyld_image_metadata meta[4];
    uint8_t *data[4] = {NULL, NULL, NULL, NULL};
    const char *deps[4][IOS27_IMAGES_MAX] = {{NULL}};
    size_t sizes[4] = {0u, 0u, 0u, 0u};
    ios27_image_spec specs[4];
    ios27_22c_graph *g = NULL;
    ios27_22c_diagnostic d;
    ios27_22c_lease lease;
    ios27_export_resolution symbol;
    char error[192];
    if (argc != 5) return 2;
    for (size_t i = 0; i < 4u; ++i) {
        data[i] = read_file(argv[i + 1u], &sizes[i]);
        assert(data[i]);
        assert(!ios27_dyld_inspect_image(data[i], sizes[i],
                                          &meta[i], error, sizeof(error)));
        assert(meta[i].dependency_count <= IOS27_IMAGES_MAX);
        for (size_t k = 0; k < meta[i].dependency_count; ++k)
            deps[i][k] = meta[i].dependencies[k].install_name;
        specs[i] = (ios27_image_spec){names[i], data[i], sizes[i],
                                      deps[i], meta[i].dependency_count};
    }
    assert(specs[0].dependency_count == 3u);
    assert(specs[1].dependency_count == 2u);
    assert(specs[2].dependency_count == 1u);
    assert(specs[3].dependency_count == 0u);
    assert(!ios27_22c_create_with_paths(specs, 4u, paths, paths[0], &g, &d));
    assert(g);
    for (size_t i = 0; i < 4u; ++i) {
        const char *name = NULL;
        assert(!ios27_22c_order(g, i, 0, &name));
        assert(!strcmp(name, names[3u - i]));
        assert(!ios27_22c_order(g, i, 1, &name));
        assert(!strcmp(name, names[i]));
    }
    assert(!ios27_22c_resolve(g, 0u, 3, "_phase19b_base_number", &symbol, &d));
    assert(symbol.library_index == 3u);
    assert(!ios27_22c_resolve(g, 0u, -2, "_phase19b_base_number", &symbol, &d));
    for (unsigned cycle = 0u; cycle < 32u; ++cycle) {
        uintptr_t root = 0u, number = 0u;
        log_state trace = {{0}, 0u};
        assert(!ios27_22c_load(g, &d));
        assert(!ios27_22c_acquire(g, 0u, &lease, &d));
        for (size_t i = 0; i < 4u; ++i)
            assert(ios27_22c_pins(g, i) == 1u);
        assert(!ios27_22c_symbol(g, lease, 0u, "_phase19b_root_value", &root));
        assert(!ios27_22c_symbol(g, lease, 3u, "_phase19b_base_number", &number));
        assert(*(const int *)number == 11);
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
        {
            int (*entry)(void);
            memcpy(&entry, &root, sizeof(entry));
            assert(entry() == 166);
        }
#else
        (void)root;
#endif
        assert(ios27_22c_unload(g, &d) && d.reason == IOS27_22C_BUSY);
        assert(!ios27_22c_release(g, lease));
        assert(!ios27_22c_initialize(g, init, fini, &trace, &d));
        assert(trace.count == 4u);
        assert(!ios27_22c_unload(g, &d));
        assert(trace.count == 8u);
        for (size_t i = 0; i < 4u; ++i) {
            assert(trace.trace[i] == 3u - i);
            assert(trace.trace[4u + i] == 100u + i);
        }
    }
    ios27_22c_destroy(g);
    g = NULL;
    const char *invalid_paths[4] = {
        paths[0], paths[1], paths[2], "/App/Wrong/Base.dylib"
    };
    assert(ios27_22c_create_with_paths(specs, 4u, invalid_paths,
                                        paths[0], &g, &d) == -1);
    assert(g == NULL && d.reason == IOS27_22C_DEPENDENCY);
    data[3][0] ^= 0xffu;
    assert(ios27_22c_create_with_paths(specs, 4u, paths,
                                        paths[0], &g, &d) == -1);
    assert(g == NULL && d.reason == IOS27_22C_MACHO);
    data[3][0] ^= 0xffu;
    for (size_t i = 0; i < 4u; ++i) free(data[i]);
    puts("phase22c: real compiler-linked four-dylib paths, symbols, native calls and 32 teardown cycles passed");
    return 0;
}
