#include "ios27/phase23c_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(v) do { if (!(v)) { \
    (void)fprintf(stderr, "phase23c reject fail at %d: %s\n", __LINE__, #v); \
    exit(1); } } while (0)
static uintptr_t value(void *r, phase22d_sel s) {
    (void)r; (void)s; return 1u;
}
static void inspect_file(phase23c_bridge *b, const char *name,
                         const char *path, const char *class_name) {
    FILE *f = fopen(path, "rb");
    REQUIRE(f != NULL);
    REQUIRE(fseek(f, 0, SEEK_END) == 0);
    long n = ftell(f);
    REQUIRE(n > 0 && n <= 2L * 1024L * 1024L);
    REQUIRE(fseek(f, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)n);
    REQUIRE(bytes != NULL);
    REQUIRE(fread(bytes, 1u, (size_t)n, f) == (size_t)n);
    REQUIRE(fclose(f) == 0);
    phase22d_method_spec method = {
        .selector = !strcmp(class_name, "Phase23cCategoryRoot") ? "value" : "required",
        .signature = PHASE22D_WORD0, .implementation = {.word0 = value}
    };
    phase22d_class_spec klass = {
        .name = class_name, .instance_size = 8u,
        .instance_methods = &method, .instance_count = 1u
    };
    char reason[256] = {0};
    phase23c_image h = {0};
    REQUIRE(phase23c_import(b, name, bytes, (size_t)n,
                            &klass, 1u, &h, reason, sizeof(reason)) != 0);
    REQUIRE(h.generation == 0u);
    REQUIRE(strstr(reason, "unsupported") != NULL);
    free(bytes);
}
int main(int argc, char **argv) {
    REQUIRE(argc == 3);
    phase23c_bridge *b = NULL;
    REQUIRE(phase23c_create(&b) == 0);
    inspect_file(b, "protocol.o", argv[1], "Phase23cProtocolRoot");
    inspect_file(b, "category.o", argv[2], "Phase23cCategoryRoot");
    phase23c_stats stats;
    REQUIRE(phase23c_statistics(b, &stats) == 0);
    REQUIRE(stats.images == 0 && stats.substrate.classes == 0);
    REQUIRE(phase23c_destroy(b) == 0);
    (void)puts("phase23c reject ok: unsupported ObjC2 protocol/category refused");
    return 0;
}
