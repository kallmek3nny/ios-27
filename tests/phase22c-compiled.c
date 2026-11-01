#include "ios27/macho.h"
#include "ios27/dyld_paths.h"
#include "ios27/phase22c_graph.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_LIMIT (16u * 1024u * 1024u)
static uint8_t *read_file(const char *name, size_t *length) {
    FILE *f = fopen(name, "rb");
    uint8_t *p = NULL;
    long size;
    if (!f) return NULL;
    if (fseek(f, 0L, SEEK_END) || (size = ftell(f)) < 0L ||
        (unsigned long)size > FILE_LIMIT || fseek(f, 0L, SEEK_SET))
        goto done;
    p = malloc((size_t)size + 1u);
    if (!p) goto done;
    if (fread(p, 1u, (size_t)size, f) != (size_t)size) {
        free(p);
        p = NULL;
        goto done;
    }
    *length = (size_t)size;
done:
    (void)fclose(f);
    return p;
}
static uint64_t rng = UINT64_C(0x77c22c123456abef);
static uint32_t next_random(void) {
    rng ^= rng << 13u;
    rng ^= rng >> 7u;
    rng ^= rng << 17u;
    return (uint32_t)(rng >> 16u);
}
static void fuzz_image(const uint8_t *data, size_t length) {
    uint8_t *copy = malloc(length + 1u);
    assert(copy);
    ios27_macho_info info;
    char error[192];
    /* One to four bit flips, skewed toward load commands and file edges.
     * Fresh independent bytes, no mutation of the trusted baseline. */
    for (size_t round = 0u; round < 20000u; ++round) {
        size_t n = 1u + next_random() % 4u;
        memcpy(copy, data, length);
        for (size_t m = 0u; m < n; ++m) {
            size_t off = (size_t)next_random() % length;
            if (round % 3u == 0u) off %= length < 512u ? length : 512u;
            copy[off] ^= (uint8_t)(1u << (next_random() % 8u));
        }
        (void)ios27_macho_parse(copy, length, &info, error, sizeof(error));
        /* Truncation is always bounded and must never overread. */
        if (round % 7u == 0u)
            (void)ios27_macho_parse(copy, next_random() % length,
                                    &info, error, sizeof(error));
    }
    free(copy);
}
int main(int argc, char **argv) {
    ios27_macho_info info;
    char error[192];
    size_t n1 = 0u, n2 = 0u;
    uint8_t *provider, *consumer;
    if (argc != 3 && argc != 5) {
        fprintf(stderr, "usage: compiled provider.o consumer.o [provider.dylib consumer.dylib]\n");
        return 2;
    }
    provider = read_file(argv[1], &n1);
    consumer = read_file(argv[2], &n2);
    assert(provider && consumer && n1 && n2);
    assert(!ios27_macho_parse(provider, n1, &info, error, sizeof(error)));
    assert(info.filetype == 1u);
    assert(!ios27_macho_parse(consumer, n2, &info, error, sizeof(error)));
    assert(info.filetype == 1u);
    fuzz_image(provider, n1);
    fuzz_image(consumer, n2);
    free(provider);
    free(consumer);
    if (argc == 5) {
        const char *deps[] = {"provider.dylib"};
        ios27_22c_diagnostic diagnostic;
        ios27_22c_graph *graph = NULL;
        ios27_dyld_image_metadata meta;
        ios27_image_spec specs[2];
        provider = read_file(argv[3], &n1);
        consumer = read_file(argv[4], &n2);
        assert(provider && consumer);
        assert(!ios27_macho_parse(provider, n1, &info, error, sizeof(error)));
        assert(info.filetype == 6u);
        assert(!ios27_macho_parse(consumer, n2, &info, error, sizeof(error)));
        assert(info.filetype == 6u);
        assert(!ios27_dyld_inspect_image(consumer, n2, &meta,
                                         error, sizeof(error)));
        assert(meta.dependency_count == 1u);
        assert(!strcmp(meta.dependencies[0].install_name, deps[0]));
        specs[0] = (ios27_image_spec){"consumer.dylib", consumer, n2, deps, 1u};
        specs[1] = (ios27_image_spec){"provider.dylib", provider, n1, NULL, 0u};
        if (ios27_22c_create(specs, 2u, &graph, &diagnostic)) {
            fprintf(stderr, "phase22c linked fixture rejected (reason=%d image=%zu): %s\n",
                    diagnostic.reason, diagnostic.image, diagnostic.message);
            return 1;
        }
        /* Real fixture load failure is a test failure, not falsely reported
         * as native execution. The dependency may use unsupported fixups. */
        if (ios27_22c_load(graph, &diagnostic)) {
            fprintf(stderr, "phase22c loader rejects compiled fixture (reason=%d): %s\n",
                    diagnostic.reason, diagnostic.message);
            ios27_22c_destroy(graph);
            return 1;
        }
        assert(!ios27_22c_unload(graph, &diagnostic));
        ios27_22c_destroy(graph);
        fuzz_image(provider, n1);
        fuzz_image(consumer, n2);
        free(provider);
        free(consumer);
    }
    puts("phase22c compiler-produced Mach-O validation and 40000+ mutations passed");
    return 0;
}
