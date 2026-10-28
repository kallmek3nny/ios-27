#include "ios27/images.h"

#include "ios27/binds.h"
#include "ios27/chained.h"
#include "ios27/exports.h"
#include "ios27/macho.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>
#define IOS27_POSIX_MAPPING 1
#else
#define IOS27_POSIX_MAPPING 0
#endif

#define IOS27_IMAGE_MAX_BYTES (16u * 1024u * 1024u)
#define LC_LOAD_DYLIB 0x0cu
#define LC_LOAD_WEAK_DYLIB 0x80000018u
#define LC_REEXPORT_DYLIB 0x8000001fu
#define LC_LOAD_UPWARD_DYLIB 0x80000023u
#define LC_LAZY_LOAD_DYLIB 0x20u
#define LC_DYLD_INFO 0x22u
#define LC_DYLD_INFO_ONLY 0x80000022u
#define LC_DYLD_CHAINED_FIXUPS 0x80000034u

typedef struct {
    char name[IOS27_IMAGE_NAME_MAX];
    char dependencies[IOS27_IMAGES_MAX][IOS27_IMAGE_NAME_MAX];
    size_t dep_count;
    size_t dep_index[IOS27_IMAGES_MAX];
    const uint8_t *source;
    size_t source_size;
    ios27_macho_info info;
    ios27_exports *exports;
    ios27_bind_plan *classic;
    ios27_chained_plan *chained;
    uint8_t *staging;
    uint8_t *mapping;
    size_t span;
    uint64_t min_vm;
    ios27_image_state state;
} image_node;

struct ios27_images {
    size_t count;
    size_t page_size;
    image_node nodes[IOS27_IMAGES_MAX];
};

static int fail(char *error, size_t cap, const char *message) {
    if (error && cap) (void)snprintf(error, cap, "%s", message);
    return -1;
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static int valid_name(const char *name) {
    size_t n;
    if (!name) return 0;
    for (n = 0u; n < IOS27_IMAGE_NAME_MAX; ++n) {
        unsigned char c = (unsigned char)name[n];
        if (!c) return n != 0u;
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-'))
            return 0;
    }
    return 0;
}
static int index_of(const ios27_images *images, const char *name) {
    for (size_t i = 0u; i < images->count; ++i)
        if (strcmp(images->nodes[i].name, name) == 0)
            return (int)i;
    return -1;
}
static int visit(const ios27_images *images, size_t i,
                 unsigned char color[IOS27_IMAGES_MAX]) {
    const image_node *node = &images->nodes[i];
    if (color[i] == 1u) return -1;
    if (color[i] == 2u) return 0;
    color[i] = 1u;
    for (size_t d = 0u; d < node->dep_count; ++d)
        if (visit(images, node->dep_index[d], color))
            return -1;
    color[i] = 2u;
    return 0;
}
static int dependency_commands(const image_node *node, char *error, size_t cap) {
    const uint8_t *file = node->source;
    size_t cursor = 32u, found = 0u;
    unsigned chained = 0u, legacy = 0u;
    for (uint32_t i = 0u; i < rd32(file + 16u); ++i) {
        uint32_t cmd = rd32(file + cursor);
        uint32_t cmdsize = rd32(file + cursor + 4u);
        if (cmd == LC_LOAD_DYLIB) {
            uint32_t start;
            if (cmdsize < 24u || found >= node->dep_count)
                return fail(error, cap, "dylib load commands exceed explicit dependencies");
            start = rd32(file + cursor + 8u);
            if (start < 24u || start >= cmdsize ||
                !memchr(file + cursor + start, '\0', cmdsize - start) ||
                strcmp((const char *)(file + cursor + start),
                       node->dependencies[found]) != 0)
                return fail(error, cap, "dylib load order/name does not match explicit graph");
            ++found;
        } else if (cmd == LC_LOAD_WEAK_DYLIB || cmd == LC_REEXPORT_DYLIB ||
                   cmd == LC_LOAD_UPWARD_DYLIB || cmd == LC_LAZY_LOAD_DYLIB) {
            return fail(error, cap, "weak/reexport/upward/lazy dependencies unsupported");
        } else if (cmd == LC_DYLD_CHAINED_FIXUPS) {
            if (chained++) return fail(error, cap, "duplicate chained command");
        } else if (cmd == LC_DYLD_INFO || cmd == LC_DYLD_INFO_ONLY) {
            if (legacy++) return fail(error, cap, "duplicate dyld info command");
        }
        cursor += (size_t)cmdsize;
    }
    if (found != node->dep_count || (legacy && chained))
        return fail(error, cap, "missing dylib command or mixed fixup formats");
    return chained ? 2 : legacy ? 1 : 0;
}
static int valid_layout(image_node *node, size_t page,
                        char *error, size_t cap) {
    uint64_t min_vm = UINT64_MAX, max_vm = 0u;
    size_t executable = 0u;
    if (node->info.segment_count == 0u)
        return fail(error, cap, "no segments");
    for (size_t i = 0u; i < node->info.segment_count; ++i) {
        const ios27_segment *s = &node->info.segments[i];
        uint64_t end;
        /* __PAGEZERO is a deliberate inaccessible guest address reservation,
         * not host memory to allocate. Keep it out of the span entirely. */
        if (strcmp(s->name, "__PAGEZERO") == 0) {
            if (s->vmaddr != 0u || s->fileoff != 0u ||
                s->filesize || s->initprot || s->maxprot)
                return fail(error, cap, "invalid __PAGEZERO permissions");
            continue;
        }
        if (!s->vmsize || s->vmaddr % page || s->vmsize % page ||
            (s->maxprot & ~7u) != 0u ||
            (s->initprot & ~s->maxprot) != 0u ||
            (s->initprot != 1u && s->initprot != 3u &&
             s->initprot != 5u) ||
            (s->maxprot & ~7u) || (s->maxprot & 6u) == 6u ||
            (s->initprot & ~s->maxprot) ||
            s->vmsize > UINT64_MAX - s->vmaddr)
            return fail(error, cap, "unaligned, overflowing or W+X segment");
        end = s->vmaddr + s->vmsize;
        if (s->vmaddr < min_vm) min_vm = s->vmaddr;
        if (end > max_vm) max_vm = end;
        if ((s->initprot & 4u) && s->filesize) ++executable;
        for (size_t j = 0u; j < i; ++j) {
            const ios27_segment *prev = &node->info.segments[j];
            if (strcmp(prev->name, "__PAGEZERO") != 0 &&
                s->vmaddr < prev->vmaddr + prev->vmsize &&
                prev->vmaddr < end)
                return fail(error, cap, "overlapping VM segments");
            if (s->filesize && prev->filesize &&
                s->fileoff < prev->fileoff + prev->filesize &&
                prev->fileoff < s->fileoff + s->filesize)
                return fail(error, cap, "overlapping file segments");
        }
    }
    for (size_t i = 0u; i < node->info.segment_count; ++i)
        if (strcmp(node->info.segments[i].name, "__PAGEZERO") == 0 &&
            node->info.segments[i].vmsize > min_vm)
            return fail(error, cap, "__PAGEZERO overlaps loadable VM");
    if (!executable || max_vm <= min_vm ||
        max_vm - min_vm > IOS27_IMAGE_MAX_BYTES)
        return fail(error, cap, "missing executable code or oversized image span");
    node->min_vm = min_vm;
    node->span = (size_t)(max_vm - min_vm);
    return 0;
}
static int find_symbol(const image_node *node, const char *symbol,
                       uintptr_t *address) {
    const ios27_export *entry;
    if (!node || !node->mapping || !node->exports) return -1;
    entry = ios27_exports_find(node->exports, symbol);
    if (!entry || entry->flags != 0u || entry->address >= node->span)
        return -1;
    for (size_t i = 0u; i < node->info.segment_count; ++i) {
        const ios27_segment *s = &node->info.segments[i];
        uint64_t offset;
        if (strcmp(s->name, "__PAGEZERO") == 0) continue;
        offset = s->vmaddr - node->min_vm;
        /* Export tries may expose code, read-only constants, or writable
         * data, including zero-filled globals. Resolve only within a
         * readable, mapped segment, never __PAGEZERO or a VM hole. */
        if ((s->initprot & 1u) &&
            entry->address >= offset &&
            entry->address - offset < s->vmsize) {
            *address = (uintptr_t)node->mapping + (uintptr_t)entry->address;
            return 0;
        }
    }
    return -1;
}
typedef struct {
    ios27_images *images;
    size_t importing_index;
} import_context;
static int resolve(void *opaque, const char *symbol,
                   int32_t ordinal, uint64_t *address) {
    import_context *ctx = opaque;
    const image_node *importer = &ctx->images->nodes[ctx->importing_index];
    uintptr_t location;
    if (ordinal <= 0 || (size_t)ordinal > importer->dep_count ||
        find_symbol(&ctx->images->nodes[importer->dep_index[(size_t)ordinal - 1u]],
                    symbol, &location))
        return -1;
    *address = (uint64_t)location;
    return 0;
}

void ios27_images_unload(ios27_images *images) {
    if (!images) return;
    for (size_t i = 0u; i < images->count; ++i) {
        image_node *node = &images->nodes[i];
#if IOS27_POSIX_MAPPING
        if (node->mapping)
            (void)munmap(node->mapping, node->span);
#endif
        node->mapping = NULL;
        free(node->staging);
        free(node->classic);
        free(node->chained);
        free(node->exports);
        node->state = IOS27_IMAGE_RETIRED;
    }
    free(images);
}
int ios27_images_status(const ios27_images *images, const char *name,
                        ios27_image_state *state) {
    int index;
    if (!images || !name || !state) return -1;
    index = index_of(images, name);
    if (index < 0) return -1;
    *state = images->nodes[(size_t)index].state;
    return 0;
}
int ios27_images_symbol(const ios27_images *images, const char *name,
                        const char *symbol, uintptr_t *address) {
    int index;
    if (!images || !name || !symbol || !address) return -1;
    index = index_of(images, name);
    if (index < 0 || images->nodes[(size_t)index].state != IOS27_IMAGE_READY)
        return -1;
    return find_symbol(&images->nodes[(size_t)index], symbol, address);
}
int ios27_images_entrypoint(const ios27_images *images, const char *name,
                            uintptr_t *address) {
    int index;
    const image_node *node;
    uint64_t entry_vm;
    if (!images || !name || !address) return -1;
    index = index_of(images, name);
    if (index < 0) return -1;
    node = &images->nodes[(size_t)index];
    if (node->state != IOS27_IMAGE_READY || node->info.needs_dyld ||
        ios27_macho_entry_vm(&node->info, &entry_vm) ||
        entry_vm < node->min_vm || entry_vm - node->min_vm >= node->span)
        return -1;
    *address = (uintptr_t)node->mapping +
               (uintptr_t)(entry_vm - node->min_vm);
    return 0;
}

int ios27_images_load(const ios27_image_spec *specs, size_t count,
                      ios27_images **out, char *error, size_t cap) {
    ios27_images *images;
    unsigned char color[IOS27_IMAGES_MAX] = {0};
#if IOS27_POSIX_MAPPING
    long os_page = sysconf(_SC_PAGESIZE);
#else
    long os_page = -1;
#endif
    if (out) *out = NULL;
    if (!out || !specs || count == 0u || count > IOS27_IMAGES_MAX ||
        os_page < 4096 || os_page > 65536 ||
        (((unsigned long)os_page & ((unsigned long)os_page - 1u)) != 0u))
        return fail(error, cap, "unsupported host or image graph");
    images = calloc(1u, sizeof(*images));
    if (!images) return fail(error, cap, "graph allocation failed");
    images->count = count;
    images->page_size = (size_t)os_page;

    for (size_t i = 0u; i < count; ++i) {
        image_node *node = &images->nodes[i];
        const ios27_image_spec *spec = &specs[i];
        if (!valid_name(spec->name) || !spec->file ||
            spec->size < 32u || spec->size > IOS27_IMAGE_MAX_BYTES ||
            spec->dependency_count > IOS27_IMAGES_MAX ||
            (spec->dependency_count && !spec->dependencies)) {
            (void)fail(error, cap, "invalid explicit image spec");
            goto rollback;
        }
        (void)snprintf(node->name, sizeof(node->name), "%s", spec->name);
        for (size_t j = 0u; j < i; ++j)
            if (strcmp(images->nodes[j].name, node->name) == 0) {
                (void)fail(error, cap, "duplicate image name");
                goto rollback;
            }
        node->source = spec->file;
        node->source_size = spec->size;
        node->dep_count = spec->dependency_count;
        for (size_t d = 0u; d < node->dep_count; ++d) {
            if (!valid_name(spec->dependencies[d])) {
                (void)fail(error, cap, "invalid dependency name");
                goto rollback;
            }
            (void)snprintf(node->dependencies[d],
                           sizeof(node->dependencies[d]), "%s",
                           spec->dependencies[d]);
            for (size_t earlier = 0u; earlier < d; ++earlier)
                if (strcmp(node->dependencies[earlier],
                           node->dependencies[d]) == 0) {
                    (void)fail(error, cap, "duplicate dependency");
                    goto rollback;
                }
        }
        if (ios27_macho_parse(node->source, node->source_size,
                             &node->info, error, cap))
            goto rollback;
        if (node->info.encrypted || node->info.unsupported_required) {
            (void)fail(error, cap, "encrypted or unknown-required image unsupported");
            goto rollback;
        }
        if ((node->info.filetype != IOS27_MH_EXECUTE &&
             node->info.filetype != 6u) ||
            valid_layout(node, images->page_size, error, cap)) {
            if (error && cap && error[0] == '\0')
                (void)fail(error, cap, "invalid loadable Mach-O image");
            goto rollback;
        }
        /* LC_MAIN in the load-command table or unmapped code is never valid.
         * Reject it before mapping any node in the dependency graph. */
        if (node->info.filetype == IOS27_MH_EXECUTE &&
            (!node->info.has_main ||
             node->info.entryoff < 32u + rd32(node->source + 20u) ||
             ios27_macho_entry_vm(&node->info, &(uint64_t){0}))) {
            (void)fail(error, cap, "invalid executable LC_MAIN entry");
            goto rollback;
        }
        node->state = IOS27_IMAGE_PARSED;
        node->exports = calloc(1u, sizeof(*node->exports));
        if (!node->exports) {
            (void)fail(error, cap, "export plan allocation failed");
            goto rollback;
        }
        if (ios27_exports_parse(node->source, node->source_size,
                                node->exports, error, cap))
            goto rollback;
        for (size_t e = 0u; e < node->exports->count; ++e)
            if (node->exports->entries[e].flags != 0u) {
                (void)fail(error, cap, "unsupported export flags");
                goto rollback;
            }
    }

    for (size_t i = 0u; i < count; ++i) {
        image_node *node = &images->nodes[i];
        int fixup_type;
        for (size_t d = 0u; d < node->dep_count; ++d) {
            int index = index_of(images, node->dependencies[d]);
            if (index < 0 || (size_t)index == i) {
                (void)fail(error, cap, "missing/self dependency");
                goto rollback;
            }
            node->dep_index[d] = (size_t)index;
        }
        fixup_type = dependency_commands(node, error, cap);
        if (fixup_type < 0) goto rollback;
        if (fixup_type == 1) {
            node->classic = calloc(1u, sizeof(*node->classic));
            if (!node->classic) {
                (void)fail(error, cap, "classic plan allocation failed");
                goto rollback;
            }
            if (ios27_binds_parse(node->source, node->source_size, &node->info,
                                  node->classic, error, cap))
                goto rollback;
        } else if (fixup_type == 2) {
            node->chained = calloc(1u, sizeof(*node->chained));
            if (!node->chained) {
                (void)fail(error, cap, "chained plan allocation failed");
                goto rollback;
            }
            if (ios27_chained_parse(node->source, node->source_size, &node->info,
                                    node->chained, error, cap))
                goto rollback;
        }
    }
    for (size_t i = 0u; i < count; ++i)
        if (visit(images, i, color)) {
            (void)fail(error, cap, "cyclic image dependency graph");
            goto rollback;
        }

#if IOS27_POSIX_MAPPING
    /* Reserve all guest segments before resolving any cross-image addresses. */
    for (size_t i = 0u; i < count; ++i) {
        image_node *node = &images->nodes[i];
        void *mapping = mmap(NULL, node->span, PROT_NONE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) {
            (void)fail(error, cap, "anonymous mapping failed");
            goto rollback;
        }
        node->mapping = mapping;
        node->state = IOS27_IMAGE_MAPPED;
        node->staging = malloc(node->source_size);
        if (!node->staging) {
            (void)fail(error, cap, "staging allocation failed");
            goto rollback;
        }
        memcpy(node->staging, node->source, node->source_size);
    }
    /* All symbols resolve against reserved runtime addresses, never file offsets. */
    for (size_t i = 0u; i < count; ++i) {
        image_node *node = &images->nodes[i];
        import_context ctx = { images, i };
        if (node->classic &&
            ios27_binds_apply(node->staging, node->source_size, &node->info,
                              node->classic, resolve, &ctx, error, cap))
            goto rollback;
        if (node->chained &&
            ios27_chained_apply(node->staging, node->source_size, &node->info,
                                node->chained, (uint64_t)(uintptr_t)node->mapping,
                                resolve, &ctx, error, cap))
            goto rollback;
        node->state = IOS27_IMAGE_RELOCATED;
    }
    /* No execution permissions are granted during relocation or copying. */
    for (size_t i = 0u; i < count; ++i) {
        image_node *node = &images->nodes[i];
        for (size_t s = 0u; s < node->info.segment_count; ++s) {
            const ios27_segment *seg = &node->info.segments[s];
            if (strcmp(seg->name, "__PAGEZERO") == 0) continue;
            uint8_t *destination = node->mapping +
                                   (size_t)(seg->vmaddr - node->min_vm);
            if (mprotect(destination, (size_t)seg->vmsize,
                         PROT_READ | PROT_WRITE)) {
                (void)fail(error, cap, "staged VM mapping failed");
                goto rollback;
            }
            memcpy(destination, node->staging + (size_t)seg->fileoff,
                   (size_t)seg->filesize);
        }
    }
    /* Transition RX text and RW data separately: never a RWX mapping. */
    for (size_t i = 0u; i < count; ++i) {
        image_node *node = &images->nodes[i];
        for (size_t s = 0u; s < node->info.segment_count; ++s) {
            const ios27_segment *seg = &node->info.segments[s];
            if (strcmp(seg->name, "__PAGEZERO") == 0) continue;
            uint8_t *destination = node->mapping +
                                   (size_t)(seg->vmaddr - node->min_vm);
            int prot = PROT_READ | ((seg->initprot & 4u) ? PROT_EXEC : 0) |
                       ((seg->initprot & 2u) ? PROT_WRITE : 0);
#if defined(__GNUC__)
            if (seg->initprot & 4u)
                __builtin___clear_cache((char *)destination,
                                        (char *)destination + (size_t)seg->filesize);
#endif
            if (mprotect(destination, (size_t)seg->vmsize, prot)) {
                (void)fail(error, cap, "W^X final protection failed");
                goto rollback;
            }
        }
        node->state = IOS27_IMAGE_READY;
        free(node->staging);
        node->staging = NULL;
    }
    if (error && cap) error[0] = '\0';
    *out = images;
    return 0;
#else
    (void)fail(error, cap, "only POSIX userspace VM supported");
#endif
rollback:
    ios27_images_unload(images);
    return -1;
}
