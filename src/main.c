#include "ios27/macho.h"
#include "ios27/darwin_probe.h"
#include "ios27/inventory.h"
#include "ios27/dyld_paths.h"
#include "ios27/exports.h"
#include "ios27/binds.h"

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
    if (argc >= 2 && strcmp(argv[1], "resolve-dyld") == 0) {
        const char *paths[IOS27_DYLD_MAX_CANDIDATES] = {0};
        ios27_dyld_candidates candidates;
        size_t count;
        if (argc < 5 || argc > 5 + (int)IOS27_DYLD_MAX_CANDIDATES) {
            fprintf(stderr, "usage: ios27 resolve-dyld <install-name> <guest-loader-image> <guest-main-image> [rpath ...]\n");
            return 2;
        }
        count = (size_t)(argc - 5);
        for (size_t i = 0; i < count; i++) paths[i] = argv[5 + i];
        if (ios27_dyld_paths(argv[2], argv[3], argv[4], paths, count,
                             &candidates, error, sizeof(error)) != 0) {
            fprintf(stderr, "invalid virtual dyld paths: %s\n", error);
            return 1;
        }
        for (size_t i = 0; i < candidates.count; i++)
            printf("%s\n", candidates.paths[i]);
        return 0;
    }
    if (argc < 3 || (strcmp(argv[1], "inspect") != 0 &&
                     strcmp(argv[1], "run-freestanding") != 0 &&
                     strcmp(argv[1], "probe-darwin") != 0 &&
                     strcmp(argv[1], "audit-ios") != 0 &&
                     strcmp(argv[1], "symbols") != 0 &&
                     strcmp(argv[1], "binds") != 0)) {
        fprintf(stderr, "usage: ios27 inspect <macho>\n"
                        "       ios27 probe-darwin <synthetic-macho>\n"
                        "       ios27 audit-ios <arm64-ios-macho>\n"
                        "       ios27 symbols <thin-or-fat-arm64-macho>\n"
                        "       ios27 binds <thin-or-fat-arm64-macho>\n"
                        "       ios27 resolve-dyld <name> <loader> <main> [rpath ...]\n"
                        "       ios27 run-freestanding <trusted-macho> --unsafe\n");
        return 2;
    }
    if (strcmp(argv[1], "run-freestanding") != 0 && argc != 3) {
        fprintf(stderr, "expected exactly one image path\n");
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
    if (strcmp(argv[1], "binds") == 0) {
        ios27_inventory inventory;
        ios27_macho_info selected;
        ios27_bind_plan binds;
        if (ios27_inventory_parse(data, size, &inventory, error,
                                  sizeof(error)) != 0 ||
            ios27_macho_parse(data + inventory.slice_offset,
                              inventory.slice_size, &selected,
                              error, sizeof(error)) != 0 ||
            ios27_binds_parse(data + inventory.slice_offset,
                              inventory.slice_size, &selected, &binds,
                              error, sizeof(error)) != 0) {
            fprintf(stderr, "dyld bind audit rejected: %s\n", error);
            free(data);
            return 1;
        }
        printf("classic dyld pointer binds=%zu selected_slice=%zu\n",
               binds.count, inventory.slice_offset);
        for (size_t i = 0; i < binds.count; ++i) {
            const ios27_bind *b = &binds.entries[i];
            printf("  %s ordinal=%d target=%s+0x%llx addend=%lld\n",
                   b->symbol, b->ordinal,
                   selected.segments[b->segment_index].name,
                   (unsigned long long)b->segment_offset,
                   (long long)b->addend);
        }
        puts("read-only audit only: no libraries mapped or relocations applied");
        free(data);
        return 0;
    }
    if (strcmp(argv[1], "symbols") == 0) {
        ios27_inventory inventory;
        ios27_exports exports;
        if (ios27_inventory_parse(data, size, &inventory,
                                  error, sizeof(error)) != 0 ||
            ios27_exports_parse(data + inventory.slice_offset,
                                 inventory.slice_size, &exports,
                                 error, sizeof(error)) != 0) {
            fprintf(stderr, "symbol inventory rejected: %s\n", error);
            free(data);
            return 1;
        }
        printf("dyld exports: %zu, trie=%u, selected-slice=%zu\n",
               exports.count, exports.has_trie, inventory.slice_offset);
        for (size_t i = 0; i < exports.count; ++i) {
            const ios27_export *e = &exports.entries[i];
            if (e->flags & IOS27_EXPORT_REEXPORT)
                printf("  %s: reexport library=%llu name=%s\n",
                       e->name, (unsigned long long)e->ordinal,
                       e->imported_name[0] ? e->imported_name : e->name);
            else
                printf("  %s: offset=0x%llx flags=0x%llx\n",
                       e->name, (unsigned long long)e->address,
                       (unsigned long long)e->flags);
        }
        free(data);
        return 0;
    }
    if (strcmp(argv[1], "audit-ios") == 0) {
        ios27_inventory inventory;
        if (ios27_inventory_parse(data, size, &inventory,
                                  error, sizeof(error)) != 0) {
            fprintf(stderr, "iOS inventory rejected image: %s\n", error);
            free(data);
            return 1;
        }
        printf("native ARM64 import inventory: %s\n",
               inventory.is_fat ? "universal Mach-O" : "thin Mach-O");
        printf("slice_offset=%zu size=%zu arm64e=%u platform=%u\n",
               inventory.slice_offset, inventory.slice_size,
               inventory.is_arm64e, inventory.platform);
        printf("dependencies=%zu public_frameworks=%zu private_frameworks=%zu\n",
               inventory.imports_count, inventory.frameworks_count,
               inventory.private_frameworks_count);
        for (size_t i = 0; i < inventory.imports_count; ++i)
            printf("  %s\n", inventory.imports[i].path);
        printf("dyld=%u fixups=%u encrypted=%u\n",
               inventory.needs_dyld, inventory.has_fixups, inventory.encrypted);
        puts("official SpringBoard / Liquid Glass runnable: NO");
        puts("missing: Apple framework runtime, Mach/XNU services, dyld,");
        puts("         native Metal/CoreAnimation graphics bridge, UI services");
        free(data);
        return 0;
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
    if (strcmp(argv[1], "probe-darwin") == 0) {
        ios27_darwin_probe_result probe;
        if (ios27_darwin_probe(data, size, &image, &probe,
                               error, sizeof(error)) != 0) {
            fprintf(stderr, "Darwin probe rejected image: %s\n", error);
            free(data);
            return 1;
        }
        printf("Darwin probe: x0=%llu x16=0x%llx carry=%u exited=%u steps=%zu traps=%zu\n",
               (unsigned long long)probe.x0,
               (unsigned long long)probe.x16, probe.carry, probe.exited,
               probe.instructions, probe.syscalls);
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
