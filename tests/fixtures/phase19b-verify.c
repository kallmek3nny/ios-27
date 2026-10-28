/* Standalone bounded native multi-framework integration/rollback test. */
#include "ios27/dyld_paths.h"
#include "ios27/images.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_LIMIT (4u * 1024u * 1024u)

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *buffer;
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) ||
        (length = ftell(file)) < 32 ||
        (unsigned long)length > FILE_LIMIT ||
        fseek(file, 0, SEEK_SET)) {
        (void)fclose(file);
        return NULL;
    }
    *size = (size_t)length;
    buffer = malloc(*size);
    if (!buffer) {
        (void)fclose(file);
        return NULL;
    }
    if (fread(buffer, 1u, *size, file) != *size) {
        free(buffer);
        (void)fclose(file);
        return NULL;
    }
    if (fclose(file)) {
        free(buffer);
        return NULL;
    }
    return buffer;
}
static void reject(const ios27_image_spec *specs, size_t count,
                   const char *const *paths, const char *main) {
    ios27_images *images = NULL;
    char error[256] = {0};
    assert(ios27_images_load_with_paths(specs, count, paths, main,
                                        &images, error, sizeof(error)) == -1);
    assert(images == NULL && error[0] != '\0');
}
int main(int argc, char **argv) {
    static ios27_dyld_image_metadata metadata[4];
    static const char *const paths[] = {
        "/App/Main", "/App/Plugins/Middle.dylib",
        "/App/Frameworks/Leaf.dylib", "/App/Frameworks/Base.dylib"
    };
    const char *altered_paths[] = {paths[0], paths[1], paths[2], paths[3]};
    static const char *names[] = {
        "root.dylib", "middle.dylib", "leaf.dylib", "base.dylib"
    };
    uint8_t *data[4] = {NULL, NULL, NULL, NULL};
    size_t sizes[4] = {0u, 0u, 0u, 0u};
    ios27_image_spec specs[4];
    ios27_images *images = NULL;
    char error[256] = {0};
    if (argc != 5) return 2;
    for (size_t i = 0u; i < 4u; ++i) {
        data[i] = read_file(argv[i + 1u], &sizes[i]);
        assert(data[i]);
        assert(!ios27_dyld_inspect_image(data[i], sizes[i],
                                          &metadata[i], error, sizeof(error)));
        specs[i].name = names[i];
        specs[i].file = data[i];
        specs[i].size = sizes[i];
        specs[i].dependencies = NULL;
        specs[i].dependency_count = metadata[i].dependency_count;
    }
    const char *deps[4][IOS27_IMAGES_MAX] = {{NULL}};
    for (size_t i = 0u; i < 4u; ++i) {
        assert(metadata[i].dependency_count <= IOS27_IMAGES_MAX);
        for (size_t j = 0u; j < metadata[i].dependency_count; ++j)
            deps[i][j] = metadata[i].dependencies[j].install_name;
        specs[i].dependencies = deps[i];
    }
    assert(specs[0].dependency_count == 3u);
    assert(specs[1].dependency_count == 2u);
    assert(specs[2].dependency_count == 1u);
    assert(specs[3].dependency_count == 0u);
    assert(metadata[0].rpath_count == 2u);
    assert(metadata[1].rpath_count == 0u);
    for (unsigned loop = 0u; loop < 32u; ++loop) {
        uintptr_t root, number;
        const char *name;
        assert(!ios27_images_load_with_paths(specs, 4u, paths, paths[0],
                                              &images, error, sizeof(error)));
        assert(images);
        for (size_t i = 0u; i < 4u; ++i) {
            assert(!ios27_images_initialization_image(images, i, &name));
            assert(!strcmp(name, names[3u - i]));
            assert(!ios27_images_finalization_image(images, i, &name));
            assert(!strcmp(name, names[i]));
        }
        assert(!ios27_images_symbol(images, names[0],
                                    "_phase19b_root_value", &root));
        assert(!ios27_images_symbol(images, names[3],
                                    "_phase19b_base_number", &number));
        assert(*(const int *)number == 11);
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
        {
            int (*call)(void);
            memcpy(&call, &root, sizeof(call));
            assert(call() == 166);
        }
#else
        (void)root;
#endif
        ios27_images_unload(images);
        images = NULL;
    }
    altered_paths[3] = "/App/Missing/Base.dylib";
    reject(specs, 4u, altered_paths, paths[0]);
    altered_paths[3] = paths[3];
    /* Detect malformed images before mapping and recover cleanly. */
    data[2][0] ^= 0xffu;
    reject(specs, 4u, paths, paths[0]);
    data[2][0] ^= 0xffu;
    assert(!ios27_images_load_with_paths(specs, 4u, paths, paths[0],
                                          &images, error, sizeof(error)));
    ios27_images_unload(images);
    for (size_t i = 0u; i < 4u; ++i) free(data[i]);
    puts("phase19b real 4-dylib graph: order, cross-library data/calls and rollback passed");
    return 0;
}
