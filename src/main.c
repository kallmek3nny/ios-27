#include "ios27/macho.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT_BYTES (256u * 1024u * 1024u)

static uint8_t *read_image(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    uint8_t *buffer;
    long length;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) < 0 ||
        (unsigned long)length > MAX_INPUT_BYTES ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buffer = malloc((size_t)length ? (size_t)length : 1u);
    if (buffer == NULL || fread(buffer, 1u, (size_t)length, f) != (size_t)length) {
        free(buffer);
        fclose(f);
        return NULL;
    }
    if (fclose(f) != 0) {
        free(buffer);
        return NULL;
    }
    *size = (size_t)length;
    return buffer;
}

static void version(uint32_t packed) {
    printf("%u.%u.%u", (packed >> 16) & 0xffffu, (packed >> 8) & 0xffu,
           packed & 0xffu);
}

int main(int argc, char **argv) {
    uint8_t *data;
    size_t size;
    char error[192] = {0};
    ios27_macho_info image;
    int result;
    if (argc < 3 || (strcmp(argv[1], "inspect") != 0 &&
                     strcmp(argv[1], "run-freestanding") != 0)) {
        fprintf(stderr, "usage: ios27 inspect <macho>\n"
                        "       ios27 run-freestanding <trusted-macho> --unsafe\n");
        return 2;
    }
    if (strcmp(argv[1], "run-freestanding") == 0 &&
        (argc != 4 || strcmp(argv[3], "--unsafe") != 0)) {
        fprintf(stderr, "refusing to run code without explicit --unsafe flag\n");
        return 2;
    }
    data = read_image(argv[2], &size);
    if (!data) {
        fprintf(stderr, "could not read file or file exceeds %u bytes: %s\n",
                MAX_INPUT_BYTES, argv[2]);
        return 1;
    }
    if (ios27_macho_parse(data, size, &image, error, sizeof(error)) != 0) {
        fprintf(stderr, "mach-o rejected: %s\n", error);
        free(data);
        return 1;
    }
    if (strcmp(argv[1], "inspect") == 0) {
        printf("ARM64 Mach-O | filetype=%u | flags=0x%08x\n",
               image.filetype, image.flags);
        printf("segments=%zu LC_MAIN=%s dyld=%s dylibs=%s fixups=%s encrypted=%s\n",
               image.segment_count, image.has_main ? "yes" : "no",
               image.needs_dyld ? "yes" : "no",
               image.has_dylibs ? "yes" : "no",
               image.has_fixups ? "yes" : "no",
               image.encrypted ? "yes" : "no");
        if (image.has_main)
            printf("entryoff=0x%llx stacksize=0x%llx\n",
                   (unsigned long long)image.entryoff,
                   (unsigned long long)image.stacksize);
        if (image.platform) {
            printf("platform=%u minos=", image.platform);
            version(image.minos);
            printf(" sdk=");
            version(image.sdk);
            putchar('\n');
        }
        for (size_t i = 0; i < image.segment_count; ++i) {
            const ios27_segment *s = &image.segments[i];
            printf("  %-16s vm=0x%llx size=0x%llx file=0x%llx size=0x%llx prot=%u\n",
                   s->name, (unsigned long long)s->vmaddr,
                   (unsigned long long)s->vmsize,
                   (unsigned long long)s->fileoff,
                   (unsigned long long)s->filesize, s->initprot);
        }
        free(data);
        return 0;
    }
    if (ios27_run_freestanding(data, size, &image, &result,
                               error, sizeof(error)) != 0) {
        fprintf(stderr, "cannot execute: %s\n", error);
        free(data);
        return 1;
    }
    printf("freestanding ARM64 entry returned %d\n", result);
    free(data);
    return 0;
}
