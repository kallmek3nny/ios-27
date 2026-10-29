#include "ios27/phase22d_metadata.h"
#include "ios27/objc_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uintptr_t word0(void *r, phase22d_sel s) {
    (void)r; (void)s; return 11u;
}
#define REQUIRE(p) do { if (!(p)) { \
    (void)fprintf(stderr, "metadata check failed: %s (%d)\\n", #p, __LINE__); \
    return 1; } } while (0)
int main(int argc, char **argv) {
    FILE *f;
    uint8_t *bytes, *mutation;
    long length;
    phase22d_runtime *runtime = NULL;
    ios27_objc_metadata *parsed;
    char error[160] = {0};
    phase22d_stats stats = {0};
    REQUIRE(argc == 2);
    f = fopen(argv[1], "rb");
    REQUIRE(f != NULL);
    REQUIRE(fseek(f, 0, SEEK_END) == 0);
    length = ftell(f);
    REQUIRE(length > 0 && length <= (2L * 1024L * 1024L));
    REQUIRE(fseek(f, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)length);
    mutation = malloc((size_t)length);
    parsed = calloc(1u, sizeof(*parsed));
    REQUIRE(bytes && mutation && parsed);
    REQUIRE(fread(bytes, 1u, (size_t)length, f) == (size_t)length);
    REQUIRE(fclose(f) == 0);
    REQUIRE(ios27_objc_metadata_inspect(bytes, (size_t)length,
                                        parsed, error, sizeof(error)) == 0);
    REQUIRE(parsed->class_count == 2u);
    const phase22d_method_spec root_methods[] = {
        {"init", PHASE22D_WORD0, {.word0 = word0}},
        {"number", PHASE22D_WORD0, {.word0 = word0}},
        {"answer", PHASE22D_WORD0, {.word0 = word0}}
    };
    const phase22d_method_spec child_methods[] = {
        {"number", PHASE22D_WORD0, {.word0 = word0}}
    };
    size_t root_size = 0, child_size = 0;
    for (size_t i = 0; i < parsed->class_count; ++i) {
        const ios27_objc_metadata_class *c = &parsed->classes[i];
        if (!strcmp(c->name, "Root")) root_size = c->instance_size;
        if (!strcmp(c->name, "Child")) child_size = c->instance_size;
    }
    REQUIRE(root_size >= sizeof(void *) && child_size >= root_size);
    const phase22d_class_spec bindings[] = {
        {.name = "Child", .superclass = "Root",
         .instance_size = child_size,
         .instance_methods = child_methods, .instance_count = 1u},
        {.name = "Root", .instance_size = root_size,
         .instance_methods = root_methods, .instance_count = 3u}
    };
    REQUIRE(phase22d_create(&runtime) == 0);
    /* Strict metadata verification before any registration side effect. */
    REQUIRE(phase22d_register_compiled(runtime, bytes, (size_t)length,
                                       bindings, 1u, error, sizeof(error)) != 0);
    REQUIRE(phase22d_statistics(runtime, &stats) == 0 && stats.classes == 0);
    REQUIRE(phase22d_register_compiled(runtime, bytes, (size_t)length,
                                       bindings, 2u, error, sizeof(error)) == 0);
    REQUIRE(phase22d_statistics(runtime, &stats) == 0 && stats.classes == 2);
    REQUIRE(phase22d_validate(runtime) == 0);
    /* A bounded malformed-image corpus targets Mach header, load command,
     * relocations, symbol names, section data and Objective-C class metadata.
     * Valid but semantically different mutations may be accepted by parser. */
    unsigned rejected = 0, accepted = 0;
    for (unsigned i = 0; i < 512u; ++i) {
        ios27_objc_metadata *scratch = calloc(1u, sizeof(*scratch));
        REQUIRE(scratch != NULL);
        memcpy(mutation, bytes, (size_t)length);
        size_t pos = (size_t)((i * 104729u + i * i * 31u) % (unsigned long)length);
        mutation[pos] ^= (uint8_t)(1u << (i % 8u));
        if (ios27_objc_metadata_inspect(mutation, (size_t)length,
                                        scratch, error, sizeof(error))) {
            ++rejected;
            REQUIRE(scratch->class_count == 0);
        } else ++accepted;
        free(scratch);
    }
    REQUIRE(rejected > 0 && accepted + rejected == 512u);
    (void)printf("phase22d objc2: classes=%u methods=%u fuzz=%u rejected=%u accepted=%u\\n",
                 parsed->class_count,
                 parsed->classes[0].method_count + parsed->classes[1].method_count,
                 512u, rejected, accepted);
    phase22d_destroy(runtime);
    free(parsed); free(mutation); free(bytes);
    return 0;
}
