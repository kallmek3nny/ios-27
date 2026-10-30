#include "ios27/images.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *data; size_t size; } blob;
static blob read_image(const char *path) {
    blob b = {NULL, 0u};
    FILE *f = fopen(path, "rb");
    long length;
    assert(f && fseek(f, 0L, SEEK_END) == 0);
    length = ftell(f);
    assert(length > 32 && length < 2 * 1024 * 1024);
    assert(fseek(f, 0L, SEEK_SET) == 0);
    b.size = (size_t)length;
    b.data = malloc(b.size);
    assert(b.data && fread(b.data, 1u, b.size, f) == b.size);
    assert(fclose(f) == 0);
    return b;
}
int main(int argc, char **argv) {
    if (argc != 4 && argc != 5) return 3;
    blob provider = read_image(argv[1]);
    blob client = read_image(argv[2]);
    blob exe = {NULL, 0u};
    ios27_images *images = NULL;
    const char *client_deps[] = {"phase26a-c.dylib"};
    const char *exe_deps[] = {"phase26a-client.dylib"};
    ios27_image_spec specs[3] = {
        {"phase26a-c.dylib", provider.data, provider.size, NULL, 0u},
        {"phase26a-client.dylib", client.data, client.size, client_deps, 1u},
        {"phase26a-exec", NULL, 0u, exe_deps, 1u}
    };
    char err[256] = {0};
    uintptr_t entry = 0u;
    if (argc == 5) {
        exe = read_image(argv[3]);
        specs[2].file = exe.data;
        specs[2].size = exe.size;
    }
    if (ios27_images_load(specs, argc == 5 ? 3u : 2u, &images, err, sizeof(err))) {
        fprintf(stderr, "phase26a mapped image rejected: %s\n", err);
        return 4;
    }
    if (ios27_images_symbol(images, "phase26a-client.dylib", argv[argc - 1], &entry))
        return 5;
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    {
        int (*call)(void) = (int (*)(void))entry;
        if (call() != 42) return 6;
        if (argc == 5) {
            uintptr_t start = 0u;
            /* LC_MAIN public entrypoint intentionally refuses images requiring dyld.\n             * Invoke only the compiler-linked, exported _main of this trusted\n             * fixture through the explicitly resolved validated symbol. */\n            if (ios27_images_symbol(images, "phase26a-exec", "_main", &start))
                return 7;
            int (*main_entry)(void) = (int (*)(void))start;
            if (main_entry() != 42) return 8;
        }
    }
#else
    (void)entry; /* inspect and relocation resolution only, never execute */
#endif
    ios27_images_unload(images);
    free(provider.data);
    free(client.data);
    free(exe.data);
    puts("phase26a: linked guest image bindings verified");
    return 0;
}
