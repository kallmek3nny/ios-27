#include "ios27/phase25a_inspect.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(const char *path) {
    FILE *fp = fopen(path, "rb");
    uint8_t *original, *mutated;
    ios27_phase25a_summary out = {0};
    long length;
    if (!fp || fseek(fp, 0, SEEK_END) != 0 ||
        (length = ftell(fp)) < 32 || length > 8 * 1024 * 1024 ||
        fseek(fp, 0, SEEK_SET) != 0) return 1;
    original = malloc((size_t)length);
    mutated = malloc((size_t)length);
    if (!original || !mutated ||
        fread(original, 1, (size_t)length, fp) != (size_t)length ||
        fclose(fp) != 0) return 1;
    if (ios27_phase25a_preflight(original, (size_t)length, &out) != 0 ||
        !out.chained_fixup_bytes || !out.metadata_sections) return 1;
    for (unsigned i = 0; i < 1200u; ++i) {
        memcpy(mutated, original, (size_t)length);
        size_t position = (size_t)(((uint64_t)i * 2654435761ull + i * i) %
                                   (uint64_t)length);
        mutated[position] ^= (uint8_t)(1u << (i % 8u));
        (void)ios27_phase25a_preflight(mutated, (size_t)length, &out);
        if (ios27_phase25a_preflight(mutated, 16u, &out) == 0 ||
            out.sections != 0u) return 1;
    }
    free(original);
    free(mutated);
    return 0;
}
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    for (int i = 1; i < argc; ++i) {
        if (check(argv[i])) {
            (void)fprintf(stderr, "phase25a preflight failed: %s\n", argv[i]);
            return 1;
        }
    }
    (void)puts("phase25a: native W^X/Mach-O preflight + mutation corpus passed");
    return 0;
}
