#include "ios27/objc_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MH_MAGIC_64 0xfeedfacfu
#define CPU_TYPE_ARM64 0x0100000cu
#define MH_OBJECT 1u
#define LC_SEGMENT_64 0x19u
#define LC_SYMTAB 2u
#define N_TYPE 0x0eu
#define N_SECT 0x0eu
#define N_STAB 0xe0u
#define MAX_FILE (2u * 1024u * 1024u)
#define MAX_SECTIONS 64u
#define MAX_SYMBOLS 4096u
#define MAX_RELOCS 4096u

typedef struct {
    char name[17];
    uint64_t addr, bytes;
    uint32_t offset, reloff, nreloc;
} section;
typedef struct {
    const uint8_t *file;
    size_t file_size;
    section sections[MAX_SECTIONS];
    uint32_t sec_count, symoff, nsyms, stroff, strsize;
} reader;
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t u64(const uint8_t *p) {
    return (uint64_t)u32(p) | ((uint64_t)u32(p + 4u) << 32u);
}
static int within(const reader *r, uint64_t off, uint64_t n) {
    return off <= r->file_size && n <= r->file_size - off;
}
static int fail(char *error, size_t cap, const char *why) {
    if (error && cap) (void)snprintf(error, cap, "%s", why);
    return -1;
}
static int parse(reader *r) {
    const uint8_t *f = r->file;
    uint32_t count, commands, saw_syms = 0u;
    size_t pos, end;
    if (r->file_size < 32u || r->file_size > MAX_FILE ||
        u32(f) != MH_MAGIC_64 || u32(f + 4u) != CPU_TYPE_ARM64 ||
        (u32(f + 8u) & 255u) != 0u || u32(f + 12u) != MH_OBJECT)
        return -1;
    count = u32(f + 16u);
    commands = u32(f + 20u);
    if (!count || count > 256u || commands > r->file_size - 32u)
        return -1;
    pos = 32u;
    end = pos + commands;
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t op, n, nsecs;
        const uint8_t *cmd;
        if (pos > end || end - pos < 8u) return -1;
        cmd = f + pos;
        op = u32(cmd);
        n = u32(cmd + 4u);
        if (n < 8u || n % 8u || n > end - pos) return -1;
        if (op == LC_SEGMENT_64) {
            if (n < 72u) return -1;
            nsecs = u32(cmd + 64u);
            if (nsecs > (n - 72u) / 80u ||
                nsecs > MAX_SECTIONS - r->sec_count) return -1;
            for (uint32_t j = 0u; j < nsecs; ++j) {
                const uint8_t *p = cmd + 72u + j * 80u;
                section *s = &r->sections[r->sec_count++];
                memcpy(s->name, p, 16u);
                s->name[16] = 0;
                s->addr = u64(p + 32u);
                s->bytes = u64(p + 40u);
                s->offset = u32(p + 48u);
                s->reloff = u32(p + 56u);
                s->nreloc = u32(p + 60u);
                if (s->bytes > UINT64_MAX - s->addr ||
                    !within(r, s->offset, s->bytes) ||
                    s->nreloc > MAX_RELOCS ||
                    !within(r, s->reloff, (uint64_t)s->nreloc * 8u))
                    return -1;
            }
        } else if (op == LC_SYMTAB) {
            if (n != 24u || saw_syms++) return -1;
            r->symoff = u32(cmd + 8u);
            r->nsyms = u32(cmd + 12u);
            r->stroff = u32(cmd + 16u);
            r->strsize = u32(cmd + 20u);
        } else if (op & 0x80000000u) {
            /* Unrecognized required load commands need a dyld. */
            return -1;
        }
        pos += n;
    }
    if (pos != end || !saw_syms || !r->nsyms ||
        r->nsyms > MAX_SYMBOLS || !r->strsize ||
        !within(r, r->symoff, (uint64_t)r->nsyms * 16u) ||
        !within(r, r->stroff, r->strsize)) return -1;
    for (uint32_t i = 0u; i < r->nsyms; ++i) {
        const uint8_t *sym = f + r->symoff + (size_t)i * 16u;
        uint32_t strx = u32(sym);
        if (strx >= r->strsize ||
            !memchr(f + r->stroff + strx, 0, r->strsize - strx))
            return -1;
    }
    for (uint32_t i = 0; i < r->sec_count; ++i) {
        const section *s = &r->sections[i];
        for (uint32_t j = 0; j < s->nreloc; ++j) {
            const uint8_t *p = f + s->reloff + (size_t)j * 8u;
            uint32_t addr = u32(p), info = u32(p + 4u);
            /* Metadata-only UNSIGNED64 relocations are supported; code
             * relocations are left to the ARM64 linker and not interpreted. */
            if (strcmp(s->name, "__text") != 0 &&
                ((info >> 28u) & 15u) == 0u &&
                ((info >> 27u) & 1u) == 1u &&
                ((info >> 25u) & 3u) == 3u &&
                ((info >> 24u) & 1u) == 0u) {
                if (addr > s->bytes || s->bytes - addr < 8u ||
                    (addr & 7u) || (info & 0xffffffu) >= r->nsyms)
                    return -1;
            }
        }
    }
    return 0;
}
static int find_section(const reader *r, const char *name) {
    int result = -1;
    for (uint32_t i = 0; i < r->sec_count; ++i) {
        if (strcmp(r->sections[i].name, name) == 0) {
            if (result >= 0) return -1;
            result = (int)i;
        }
    }
    return result;
}
static const uint8_t *at(const reader *r, int s, uint64_t addr, size_t n) {
    const section *sec;
    if (s < 0 || (uint32_t)s >= r->sec_count) return NULL;
    sec = &r->sections[s];
    if (addr < sec->addr || n > sec->bytes ||
        addr - sec->addr > sec->bytes - n) return NULL;
    return r->file + sec->offset + (size_t)(addr - sec->addr);
}
/* Look up exactly one relocation for a pointer field. Return the symbol
 * destination and require a direct local N_SECT definition. */
static int reference(const reader *r, int s, uint64_t slot,
                     int *target_section, uint64_t *target_addr) {
    const section *sec;
    const uint8_t *word = at(r, s, slot, 8u);
    uint64_t addend;
    int found = 0;
    if (!word) return -1;
    addend = u64(word);
    sec = &r->sections[s];
    for (uint32_t i = 0; i < sec->nreloc; ++i) {
        const uint8_t *p = r->file + sec->reloff + (size_t)i * 8u;
        uint32_t off = u32(p), data = u32(p + 4u);
        uint32_t symidx;
        const uint8_t *sym;
        uint8_t sect;
        if (off != slot - sec->addr) continue;
        if (found++ || (data >> 28u) != 0u || ((data >> 27u) & 1u) == 0u ||
            ((data >> 25u) & 3u) != 3u || ((data >> 24u) & 1u) ||
            (off & 7u)) return -1;
        symidx = data & 0xffffffu;
        if (symidx >= r->nsyms) return -1;
        sym = r->file + r->symoff + (size_t)symidx * 16u;
        sect = sym[5u];
        if (sym[4u] & N_STAB || (sym[4u] & N_TYPE) != N_SECT ||
            !sect || sect > r->sec_count ||
            addend > UINT64_MAX - u64(sym + 8u)) return -1;
        *target_section = sect - 1;
        *target_addr = u64(sym + 8u) + addend;
    }
    return found == 1 ? 0 : -1;
}
static int strref(const reader *r, int s, uint64_t slot,
                  char output[IOS27_OBJC_METADATA_NAME]) {
    int destination;
    uint64_t addr;
    const section *sec;
    const uint8_t *p, *end;
    if (reference(r, s, slot, &destination, &addr)) return -1;
    sec = &r->sections[destination];
    p = at(r, destination, addr, 1u);
    if (!p) return -1;
    end = memchr(p, 0, (size_t)(sec->bytes - (addr - sec->addr)));
    if (!end || end == p || (size_t)(end - p) >= IOS27_OBJC_METADATA_NAME)
        return -1;
    for (const uint8_t *q = p; q != end; ++q)
        if (*q < 32u || *q > 126u) return -1;
    memcpy(output, p, (size_t)(end - p));
    output[end - p] = 0;
    return 0;
}
static int inspect(const reader *r, ios27_objc_metadata *result) {
    int list_section = find_section(r, "__objc_classlist");
    int text_section = find_section(r, "__text");
    uint64_t classes[IOS27_OBJC_METADATA_CLASSES];
    int class_sections[IOS27_OBJC_METADATA_CLASSES];
    uint32_t raw_sizes[IOS27_OBJC_METADATA_CLASSES];
    uint32_t own_ivar_storage[IOS27_OBJC_METADATA_CLASSES] = {0};
    const section *list;
    if (list_section < 0 || text_section < 0) return -1;
    list = &r->sections[list_section];
    if (!list->bytes || list->bytes % 8u ||
        list->bytes / 8u > IOS27_OBJC_METADATA_CLASSES) return -1;
    result->class_count = (uint32_t)(list->bytes / 8u);
    for (uint32_t i = 0; i < result->class_count; ++i) {
        ios27_objc_metadata_class *c = &result->classes[i];
        int cls_section, ro_section;
        uint64_t cls_addr, ro_addr;
        const uint8_t *ro;
        if (reference(r, list_section, list->addr + (uint64_t)i * 8u,
                      &cls_section, &cls_addr) ||
            !at(r, cls_section, cls_addr, 40u) ||
            reference(r, cls_section, cls_addr + 32u,
                      &ro_section, &ro_addr)) return -1;
        classes[i] = cls_addr;
        class_sections[i] = cls_section;
        ro = at(r, ro_section, ro_addr, 72u);
        if (!ro) return -1;
        c->instance_size = u32(ro + 8u);
        raw_sizes[i] = c->instance_size;
        if (c->instance_size < 8u || c->instance_size > 4096u ||
            strref(r, ro_section, ro_addr + 24u, c->name)) return -1;
        /* Clang emits non-fragile ivar classes with an *unadjusted* size.
         * Runtime allocation must include the ivar list sizes. The backing
         * __objc_ivar offset cells are initially zero until dyld/ObjC fixes
         * them up; execution of code using those cells is not yet enabled. */
        {
            int ivar_sec;
            uint64_t ivar_addr;
            if (reference(r, ro_section, ro_addr + 48u,
                          &ivar_sec, &ivar_addr) == 0) {
                const uint8_t *hdr = at(r, ivar_sec, ivar_addr, 8u);
                uint32_t count;
                if (!hdr || u32(hdr) != 32u ||
                    (count = u32(hdr + 4u)) > 32u ||
                    !at(r, ivar_sec, ivar_addr + 8u, (size_t)count * 32u))
                    return -1;
                for (uint32_t j = 0; j < count; ++j) {
                    uint64_t slot = ivar_addr + 8u + (uint64_t)j * 32u;
                    const uint8_t *entry = at(r, ivar_sec, slot, 32u);
                    int offset_sec;
                    uint64_t offset_addr;
                    char ivar_name[IOS27_OBJC_METADATA_NAME];
                    char ivar_type[IOS27_OBJC_METADATA_NAME];
                    uint32_t align, width;
                    if (!entry || reference(r, ivar_sec, slot,
                                            &offset_sec, &offset_addr) ||
                        !at(r, offset_sec, offset_addr, 4u) ||
                        strref(r, ivar_sec, slot + 8u, ivar_name) ||
                        strref(r, ivar_sec, slot + 16u, ivar_type)) return -1;
                    align = u32(entry + 24u);
                    width = u32(entry + 28u);
                    if (align > 5u || !width || width > 4096u ||
                        own_ivar_storage[i] > 4096u - width ||
                        own_ivar_storage[i] + width > 4096u - ((1u << align) - 1u))
                        return -1;
                    own_ivar_storage[i] += width + ((1u << align) - 1u);
                }
            } else if (u64(ro + 48u) != 0u) return -1;
        }
        for (uint32_t j = 0; j < i; ++j)
            if (strcmp(result->classes[j].name, c->name) == 0) return -1;
        /* Instance method list; nullable only when both bytes and reloc
         * are absent. Do not silently ignore nonzero unexpected pointers. */
        {
            int method_sec;
            uint64_t method_addr;
            const uint8_t *m;
            if (reference(r, ro_section, ro_addr + 32u,
                          &method_sec, &method_addr)) {
                if (u64(ro + 32u) != 0u) return -1;
                continue;
            }
            m = at(r, method_sec, method_addr, 8u);
            if (!m || (u32(m) & 0xffff0000u) != 0u ||
                (u32(m) & 0xffffu) != 24u ||
                u32(m + 4u) > IOS27_OBJC_METADATA_METHODS) return -1;
            c->method_count = u32(m + 4u);
            if (!at(r, method_sec, method_addr + 8u,
                    c->method_count * 24u)) return -1;
            for (uint32_t j = 0; j < c->method_count; ++j) {
                ios27_objc_metadata_method *method = &c->methods[j];
                uint64_t entry = method_addr + 8u + (uint64_t)j * 24u;
                int impl_section;
                if (strref(r, method_sec, entry, method->selector) ||
                    strref(r, method_sec, entry + 8u, method->type_encoding) ||
                    reference(r, method_sec, entry + 16u,
                              &impl_section, &method->implementation_vmaddr) ||
                    impl_section != text_section ||
                    !at(r, impl_section, method->implementation_vmaddr, 4u) ||
                    (method->implementation_vmaddr & 3u)) return -1;
                for (uint32_t k = 0; k < j; ++k)
                    if (!strcmp(c->methods[k].selector, method->selector)) return -1;
                {
                    int already_seen = 0;
                    for (uint32_t old = 0; old <= i; ++old) {
                        const ios27_objc_metadata_class *previous = &result->classes[old];
                        size_t end = old == i ? j : previous->method_count;
                        for (size_t k = 0; k < end; ++k)
                            if (strcmp(previous->methods[k].selector,
                                       method->selector) == 0)
                                already_seen = 1;
                    }
                    if (!already_seen) ++result->selector_count;
                }
            }
        }
    }
    for (uint32_t i = 0; i < result->class_count; ++i) {
        const uint8_t *cls = at(r, class_sections[i], classes[i], 40u);
        int parent_section;
        uint64_t parent_addr;
        if (!cls) return -1;
        if (reference(r, class_sections[i], classes[i] + 8u,
                      &parent_section, &parent_addr) != 0) {
            if (u64(cls + 8u) != 0u) return -1;
            continue; /* Root class */
        }
        int found = 0;
        for (uint32_t j = 0; j < result->class_count; ++j) {
            if (classes[j] == parent_addr && class_sections[j] == parent_section) {
                (void)strcpy(result->classes[i].superclass,
                             result->classes[j].name);
                found = 1;
                break;
            }
        }
        if (!found) return -1; /* external class requires dyld */
    }
    /* Resolve non-fragile inherited ivar storage, rejecting impossible
     * cycles rather than trusting order within the class list. */
    for (uint32_t pass = 0; pass < result->class_count; ++pass) {
        int changed = 0;
        for (uint32_t i = 0; i < result->class_count; ++i) {
            ios27_objc_metadata_class *c = &result->classes[i];
            uint32_t base = raw_sizes[i];
            for (uint32_t j = 0; j < result->class_count; ++j)
                if (strcmp(c->superclass, result->classes[j].name) == 0 &&
                    base < result->classes[j].instance_size)
                    base = result->classes[j].instance_size;
            if (base > 4096u - own_ivar_storage[i]) return -1;
            if (c->instance_size != base + own_ivar_storage[i]) {
                c->instance_size = base + own_ivar_storage[i];
                changed = 1;
            }
        }
        if (!changed) return 0;
    }
    return -1;
}
int ios27_objc_metadata_inspect(const uint8_t *bytes, size_t size,
                                ios27_objc_metadata *out,
                                char *error, size_t cap) {
    reader *r;
    ios27_objc_metadata *scratch;
    int ok = -1;
    if (out) memset(out, 0, sizeof(*out));
    if (!bytes || !out || !size) return fail(error, cap, "invalid metadata input");
    r = calloc(1, sizeof(*r));
    scratch = calloc(1, sizeof(*scratch));
    if (!r || !scratch) goto finish;
    r->file = bytes;
    r->file_size = size;
    if (parse(r) || inspect(r, scratch)) goto finish;
    memcpy(out, scratch, sizeof(*out));
    if (error && cap) error[0] = 0;
    ok = 0;
finish:
    free(r);
    free(scratch);
    return ok ? fail(error, cap, "invalid or unsupported ObjC2 MH_OBJECT metadata") : 0;
}
