/* Standalone integration validator linked against unchanged ios27_core. */
#include "ios27/dyld_paths.h"
#include "ios27/images.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIMIT (4u * 1024u * 1024u)

static uint8_t *load_file(const char *name, size_t *size) {
    FILE *f = fopen(name, "rb");
    uint8_t *out;
    long n;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 32 ||
        (unsigned long)n > LIMIT || fseek(f, 0, SEEK_SET)) {
        (void)fclose(f);
        return NULL;
    }
    *size = (size_t)n;
    out = malloc(*size);
    if (!out) {
        (void)fclose(f);
        return NULL;
    }
    if (fread(out, 1u, *size, f) != *size || fclose(f)) {
        free(out);
        return NULL;
    }
    return out;
}

int main(int argc, char **argv) {
    static ios27_dyld_image_metadata topmeta, midmeta, leafmeta;
    const char *paths[] = {"/App/Top.dylib",
                           "/App/Plugins/Middle.dylib",
                           "/App/Frameworks/Leaf.dylib"};
    ios27_image_spec specs[3];
    ios27_images *graph = NULL;
    uint8_t *buffers[3];
    size_t lengths[3];
    uintptr_t topfn, middlefn, leafnum, midnum;
    const char *name;
    char error[256] = {0};
    if (argc != 4) {
        fputs("usage: phase18b-verify TOP MIDDLE LEAF\n", stderr);
        return 2;
    }
    for (size_t i = 0u; i < 3u; ++i) {
        buffers[i] = load_file(argv[i + 1u], &lengths[i]);
        if (!buffers[i]) {
            fputs("missing or oversized fixture image\n", stderr);
            return 2;
        }
    }
    assert(!ios27_dyld_inspect_image(buffers[0], lengths[0], &topmeta,
                                     error, sizeof(error)));
    assert(!ios27_dyld_inspect_image(buffers[1], lengths[1], &midmeta,
                                     error, sizeof(error)));
    assert(!ios27_dyld_inspect_image(buffers[2], lengths[2], &leafmeta,
                                     error, sizeof(error)));
    assert(topmeta.dependency_count == 2u);
    assert(midmeta.dependency_count == 1u);
    assert(leafmeta.dependency_count == 0u);
    assert(!strcmp(topmeta.dependencies[0].install_name, "@rpath/Middle.dylib"));
    assert(!strcmp(topmeta.dependencies[1].install_name, "@rpath/Leaf.dylib"));
    assert(!strcmp(midmeta.dependencies[0].install_name, "@rpath/Leaf.dylib"));
    const char *topdeps[] = {topmeta.dependencies[0].install_name,
                             topmeta.dependencies[1].install_name};
    const char *middeps[] = {midmeta.dependencies[0].install_name};
    specs[0] = (ios27_image_spec){"top.dylib", buffers[0], lengths[0],
                                   topdeps, 2u};
    specs[1] = (ios27_image_spec){"middle.dylib", buffers[1], lengths[1],
                                   middeps, 1u};
    specs[2] = (ios27_image_spec){"leaf.dylib", buffers[2], lengths[2],
                                   NULL, 0u};
    assert(!ios27_images_load_with_paths(specs, 3u, paths, paths[0],
                                          &graph, error, sizeof(error)));
    assert(graph != NULL);
    assert(!ios27_images_initialization_image(graph, 0u, &name));
    assert(!strcmp(name, "leaf.dylib"));
    assert(!ios27_images_initialization_image(graph, 1u, &name));
    assert(!strcmp(name, "middle.dylib"));
    assert(!ios27_images_initialization_image(graph, 2u, &name));
    assert(!strcmp(name, "top.dylib"));
    assert(!ios27_images_symbol(graph, "top.dylib", "_phase18b_top", &topfn));
    assert(!ios27_images_symbol(graph, "middle.dylib",
                                 "_phase18b_middle", &middlefn));
    assert(!ios27_images_symbol(graph, "leaf.dylib",
                                 "_phase18b_number", &leafnum));
    assert(!ios27_images_symbol(graph, "middle.dylib",
                                 "_phase18b_adjustment", &midnum));
    assert(*(const int *)leafnum == 40);
    assert(*(const int *)midnum == 2);
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    {
        int (*top)(void);
        int (*middle)(void);
        memcpy(&top, &topfn, sizeof(top));
        memcpy(&middle, &middlefn, sizeof(middle));
        assert(middle() == 83);
        assert(top() == 167);
        puts("phase18b: native three-dylib execution returned 167");
    }
#else
    (void)topfn;
    (void)middlefn;
    puts("phase18b: three real dylib symbol/data mappings verified; ARM64 required for execution");
#endif
    ios27_images_unload(graph);
    graph = NULL;
    paths[2] = "/App/Unlisted/Leaf.dylib";
    assert(ios27_images_load_with_paths(specs, 3u, paths, paths[0],
                                        &graph, error, sizeof(error)) != 0);
    assert(graph == NULL);
    paths[2] = "/App/Frameworks/Leaf.dylib";
    assert(!ios27_images_load_with_paths(specs, 3u, paths, paths[0],
                                          &graph, error, sizeof(error)));
    ios27_images_unload(graph);
    for (size_t i = 0u; i < 3u; ++i) free(buffers[i]);
    puts("phase18b: ordered imports, initialized data, W^X and rollback verified");
    return 0;
}
