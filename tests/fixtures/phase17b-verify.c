/* Standalone phase17b verifier. Build against the unchanged ios27_core
 * target, so no CMakeLists.txt or other agent's files need editing. */
#include "ios27/chained.h"
#include "ios27/exports.h"
#include "ios27/images.h"
#include "ios27/macho.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIMIT (4u * 1024u * 1024u)

static uint8_t *read_image(const char *path, size_t *length) {
    FILE *stream = fopen(path, "rb");
    long end;
    uint8_t *bytes;
    if (!stream) return NULL;
    if (fseek(stream, 0, SEEK_END) || (end = ftell(stream)) < 32 ||
        (unsigned long)end > LIMIT || fseek(stream, 0, SEEK_SET)) {
        (void)fclose(stream);
        return NULL;
    }
    *length = (size_t)end;
    bytes = malloc(*length);
    if (!bytes) {
        (void)fclose(stream);
        return NULL;
    }
    if (fread(bytes, 1u, *length, stream) != *length || fclose(stream)) {
        free(bytes);
        return NULL;
    }
    return bytes;
}

int main(int argc, char **argv) {
    const char *dep[] = {"phase17b-provider.dylib"};
    ios27_image_spec specs[2];
    static ios27_macho_info info;
    static ios27_chained_plan fixups;
    static ios27_exports exports;
    ios27_images *images = NULL;
    uintptr_t provider, caller, base = 0u;
    const ios27_export *symbol;
    size_t provider_size, consumer_size;
    uint8_t *provider_file, *consumer_file;
    uint64_t min_vm = UINT64_MAX;
    int inspected_bind = 0;
    char error[256] = {0};

    if (argc != 3) {
        fputs("usage: phase17b-verify PROVIDER.dylib CONSUMER.dylib\n", stderr);
        return 2;
    }
    provider_file = read_image(argv[1], &provider_size);
    consumer_file = read_image(argv[2], &consumer_size);
    if (!provider_file || !consumer_file) {
        fputs("cannot read bounded linked dylib fixtures\n", stderr);
        free(provider_file);
        free(consumer_file);
        return 2;
    }
    specs[0] = (ios27_image_spec){"phase17b-consumer.dylib",
                                   consumer_file, consumer_size, dep, 1u};
    specs[1] = (ios27_image_spec){"phase17b-provider.dylib",
                                   provider_file, provider_size, NULL, 0u};
    assert(ios27_macho_parse(consumer_file, consumer_size, &info,
                            error, sizeof(error)) == 0);
    assert(info.filetype == 6u);
    assert(ios27_exports_parse(consumer_file, consumer_size, &exports,
                               error, sizeof(error)) == 0);
    assert(ios27_chained_parse(consumer_file, consumer_size, &info,
                               &fixups, error, sizeof(error)) == 0);
    symbol = ios27_exports_find(&exports, "_phase17b_call");
    assert(symbol != NULL && symbol->flags == 0u);
    for (size_t s = 0u; s < info.segment_count; ++s)
        if (info.segments[s].filesize && info.segments[s].vmaddr < min_vm)
            min_vm = info.segments[s].vmaddr;

    assert(ios27_images_load(specs, 2u, &images,
                             error, sizeof(error)) == 0);
    assert(images != NULL);
    assert(ios27_images_symbol(images, "phase17b-provider.dylib",
                               "_phase17b_answer", &provider) == 0);
    assert(ios27_images_symbol(images, "phase17b-consumer.dylib",
                               "_phase17b_call", &caller) == 0);
    assert(caller >= symbol->address);
    base = caller - (uintptr_t)symbol->address;
    for (size_t i = 0u; i < fixups.count; ++i) {
        const ios27_chained_patch *patch = &fixups.patches[i];
        uint64_t value;
        uintptr_t location;
        if (!patch->is_bind) continue;
        assert(patch->segment_index < info.segment_count);
        assert(patch->import_index < fixups.import_count);
        assert(strcmp(fixups.imports[patch->import_index].symbol,
                      "_phase17b_answer") == 0);
        location = base + (uintptr_t)(info.segments[patch->segment_index].vmaddr -
                                       min_vm + patch->segment_offset);
        memcpy(&value, (const void *)location, sizeof(value));
        assert(value == (uint64_t)provider);
        ++inspected_bind;
    }
    assert(inspected_bind > 0);

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    {
        int (*provider_fn)(void), (*consumer_fn)(void);
        memcpy(&provider_fn, &provider, sizeof(provider_fn));
        memcpy(&consumer_fn, &caller, sizeof(consumer_fn));
        assert(provider_fn() == 42);
        assert(consumer_fn() == 43); /* native call across real dylibs */
    }
    puts("phase17b: linked ARM64 cross-image native call returned 43");
#else
    puts("phase17b: genuine linked dylib bind verified (execution requires ARM64)");
#endif
    ios27_images_unload(images);

    /* The same strong import cannot be loaded without its dependency. */
    images = NULL;
    assert(ios27_images_load(specs, 1u, &images, error, sizeof(error)) != 0);
    assert(images == NULL);
    free(provider_file);
    free(consumer_file);
    puts("phase17b: linked dylib bind and atomic rollback checks passed");
    return 0;
}
