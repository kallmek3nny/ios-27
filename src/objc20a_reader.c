#include "ios27/objc20a_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE (4u * 1024u * 1024u)
#define MAX_SECTIONS 96u
#define MAX_SYMBOLS 8192u
#define MAX_RELOCS 8192u
#define MH_MAGIC_64 0xfeedfacfu
#define CPU_ARM64 0x0100000cu
#define MH_OBJECT 1u
#define LC_SEGMENT_64 0x19u
#define LC_SYMTAB 2u
#define N_SECT 0x0eu
#define N_TYPE 0x0eu

typedef struct {
    char name[17];
    uint64_t addr, size;
    uint32_t offset, reloff, nreloc;
} sec20;
typedef struct {
    const uint8_t *f;
    size_t size;
    sec20 sec[MAX_SECTIONS];
    uint32_t nsec, symoff, nsyms, stroff, strsize;
} reader20;
typedef struct {
    int sec;
    uint64_t addr;
    char external[IOS27_OBJC20A_NAME];
} ref20;

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}
static int range(const reader20 *r, uint64_t off, uint64_t count) {
    return off <= r->size && count <= r->size - off;
}
static const uint8_t *location(const reader20 *r, int sec, uint64_t vm, size_t len) {
    if (sec < 0 || (unsigned)sec >= r->nsec) return NULL;
    const sec20 *s = &r->sec[sec];
    if (vm < s->addr || len > s->size || vm - s->addr > s->size - len)
        return NULL;
    return r->f + s->offset + (size_t)(vm - s->addr);
}
static int section_index(const reader20 *r, const char *name) {
    int found = -1;
    for (unsigned i = 0; i < r->nsec; ++i)
        if (!strcmp(r->sec[i].name, name)) {
            if (found >= 0) return -2;
            found = (int)i;
        }
    return found;
}
static int parse_macho(reader20 *r) {
    if (r->size < 32 || r->size > MAX_FILE || rd32(r->f) != MH_MAGIC_64 ||
        rd32(r->f + 4) != CPU_ARM64 || (rd32(r->f + 8) & 255) != 0 ||
        rd32(r->f + 12) != MH_OBJECT) return -1;
    uint32_t cmds = rd32(r->f + 16), bytes = rd32(r->f + 20);
    if (!cmds || cmds > 256 || !range(r, 32, bytes)) return -1;
    size_t pos = 32, end = pos + bytes;
    unsigned syms = 0;
    for (uint32_t i = 0; i < cmds; ++i) {
        if (pos > end || end - pos < 8) return -1;
        const uint8_t *p = r->f + pos;
        uint32_t op = rd32(p), sz = rd32(p + 4);
        if (sz < 8 || (sz & 7) || sz > end - pos) return -1;
        if (op == LC_SEGMENT_64) {
            if (sz < 72) return -1;
            uint32_t n = rd32(p + 64);
            if (n > (sz - 72) / 80 || n > MAX_SECTIONS - r->nsec) return -1;
            for (uint32_t j = 0; j < n; ++j) {
                const uint8_t *s = p + 72 + j * 80;
                sec20 *t = &r->sec[r->nsec++];
                memcpy(t->name, s, 16);
                t->name[16] = 0;
                t->addr = rd64(s + 32);
                t->size = rd64(s + 40);
                t->offset = rd32(s + 48);
                t->reloff = rd32(s + 56);
                t->nreloc = rd32(s + 60);
                if (t->size > UINT64_MAX - t->addr ||
                    !range(r, t->offset, t->size) ||
                    t->nreloc > MAX_RELOCS ||
                    !range(r, t->reloff, (uint64_t)t->nreloc * 8)) return -1;
            }
        } else if (op == LC_SYMTAB) {
            if (sz != 24 || syms++) return -1;
            r->symoff = rd32(p + 8);
            r->nsyms = rd32(p + 12);
            r->stroff = rd32(p + 16);
            r->strsize = rd32(p + 20);
        } else if (op & 0x80000000u) return -1;
        pos += sz;
    }
    if (pos != end || syms != 1 || !r->nsyms || r->nsyms > MAX_SYMBOLS ||
        !r->strsize || !range(r, r->symoff, (uint64_t)r->nsyms * 16) ||
        !range(r, r->stroff, r->strsize)) return -1;
    for (unsigned i = 0; i < r->nsyms; ++i) {
        const uint8_t *s = r->f + r->symoff + (size_t)i * 16;
        uint32_t off = rd32(s);
        if (off >= r->strsize ||
            !memchr(r->f + r->stroff + off, 0, r->strsize - off)) return -1;
    }
    return 0;
}
/* return: 1 local, 2 external symbol, 0 absent, -1 invalid. */
static int pointer_at(const reader20 *r, int sec, uint64_t vm, ref20 *out) {
    const uint8_t *p = location(r, sec, vm, 8);
    if (!p || (vm & 7)) return -1;
    const sec20 *s = &r->sec[sec];
    uint64_t addend = rd64(p);
    unsigned found = 0;
    memset(out, 0, sizeof(*out));
    for (uint32_t i = 0; i < s->nreloc; ++i) {
        const uint8_t *q = r->f + s->reloff + (size_t)i * 8;
        uint32_t at = rd32(q), info = rd32(q + 4);
        if (at != vm - s->addr) continue;
        if (++found != 1 || (info >> 28) != 0 ||
            ((info >> 27) & 1) != 1 || ((info >> 25) & 3) != 3 ||
            ((info >> 24) & 1) != 0) return -1;
        uint32_t idx = info & 0xffffffu;
        if (idx >= r->nsyms) return -1;
        const uint8_t *sym = r->f + r->symoff + (size_t)idx * 16;
        unsigned typ = sym[4] & N_TYPE, nsect = sym[5];
        if (sym[4] & 0xe0) return -1;
        if (typ == N_SECT) {
            uint64_t base = rd64(sym + 8);
            if (!nsect || nsect > r->nsec || addend > UINT64_MAX - base)
                return -1;
            out->sec = (int)nsect - 1;
            out->addr = base + addend;
            return 1;
        }
        if (typ != 0 || addend != 0) return -1;
        const char *symname = (const char *)r->f + r->stroff + rd32(sym);
        size_t len = strlen(symname);
        if (!len || len >= sizeof(out->external)) return -1;
        memcpy(out->external, symname, len + 1);
        return 2;
    }
    return addend ? -1 : 0;
}
static int text_at(const reader20 *r, ref20 p, char out[IOS27_OBJC20A_NAME]) {
    if (p.external[0]) return -1;
    const uint8_t *begin = location(r, p.sec, p.addr, 1);
    if (!begin) return -1;
    const sec20 *s = &r->sec[p.sec];
    size_t max = (size_t)(s->size - (p.addr - s->addr));
    const uint8_t *end = memchr(begin, 0, max);
    if (!end || end == begin || (size_t)(end - begin) >= IOS27_OBJC20A_NAME)
        return -1;
    for (const uint8_t *c = begin; c != end; ++c)
        if (*c < 32 || *c > 126) return -1;
    memcpy(out, begin, (size_t)(end - begin));
    out[end - begin] = 0;
    return 0;
}
static int string_slot(const reader20 *r, int sec, uint64_t slot,
                       char out[IOS27_OBJC20A_NAME]) {
    ref20 p;
    return pointer_at(r, sec, slot, &p) == 1 ? text_at(r, p, out) : -1;
}
static int optional_ref(const reader20 *r, int sec, uint64_t slot, ref20 *p) {
    int ret = pointer_at(r, sec, slot, p);
    return ret == 0 || ret == 1 ? ret : -1;
}
static int add_selector(ios27_objc20a_image *image, const char *s) {
    for (unsigned i = 0; i < image->selector_count; ++i)
        if (!strcmp(s, image->selectors[i])) return 0;
    if (image->selector_count == IOS27_OBJC20A_SELECTORS) return -1;
    strcpy(image->selectors[image->selector_count++], s);
    return 0;
}
static int read_methods(const reader20 *r, int sec, uint64_t slot,
                        ios27_objc20a_method *methods, uint32_t *count,
                        uint32_t cap, int class_method, int optional,
                        int description, ios27_objc20a_image *image) {
    ref20 list;
    int present = optional_ref(r, sec, slot, &list);
    if (present < 0) return -1;
    if (!present) return 0;
    const uint8_t *header = location(r, list.sec, list.addr, 8);
    if (!header) return -1;
    uint32_t stride = rd32(header), n = rd32(header + 4);
    if (stride != 24u || n > cap - *count ||
        !location(r, list.sec, list.addr + 8, (size_t)n * stride)) return -1;
    for (uint32_t i = 0; i < n; ++i) {
        ios27_objc20a_method *m = &methods[(*count)++];
        uint64_t a = list.addr + 8 + (uint64_t)i * stride;
        if (string_slot(r, list.sec, a, m->name) ||
            string_slot(r, list.sec, a + 8, m->encoding) ||
            add_selector(image, m->name)) return -1;
        m->class_method = (uint8_t)class_method;
        m->optional = (uint8_t)optional;
        if (!description) {
            ref20 imp;
            if (pointer_at(r, list.sec, a + 16, &imp) != 1 ||
                strcmp(r->sec[imp.sec].name, "__text") ||
                (imp.addr & 3) || !location(r, imp.sec, imp.addr, 4)) return -1;
            m->implementation = imp.addr;
        }
        for (uint32_t j = 0; j + 1 < *count; ++j)
            if (!strcmp(m->name, methods[j].name) &&
                m->class_method == methods[j].class_method &&
                m->optional == methods[j].optional) return -1;
    }
    return 0;
}
static int read_properties(const reader20 *r, int sec, uint64_t slot,
                           ios27_objc20a_property *props,
                           uint32_t *count) {
    ref20 list;
    int present = optional_ref(r, sec, slot, &list);
    if (present <= 0) return present < 0 ? -1 : 0;
    const uint8_t *header = location(r, list.sec, list.addr, 8);
    if (!header) return -1;
    uint32_t stride = rd32(header), n = rd32(header + 4);
    if (stride != 16 || n > IOS27_OBJC20A_PROPERTIES - *count ||
        !location(r, list.sec, list.addr + 8, (size_t)n * 16)) return -1;
    for (uint32_t i = 0; i < n; ++i) {
        ios27_objc20a_property *p = &props[(*count)++];
        uint64_t a = list.addr + 8 + (uint64_t)i * 16;
        if (string_slot(r, list.sec, a, p->name) ||
            string_slot(r, list.sec, a + 8, p->attributes)) return -1;
        for (uint32_t j = 0; j + 1 < *count; ++j)
            if (!strcmp(p->name, props[j].name)) return -1;
    }
    return 0;
}
static int read_ivars(const reader20 *r, int sec, uint64_t slot,
                      ios27_objc20a_class *c) {
    ref20 list;
    int present = optional_ref(r, sec, slot, &list);
    if (present <= 0) return present < 0 ? -1 : 0;
    const uint8_t *header = location(r, list.sec, list.addr, 8);
    if (!header) return -1;
    uint32_t stride = rd32(header), n = rd32(header + 4);
    if (stride != 32 || n > IOS27_OBJC20A_IVARS ||
        !location(r, list.sec, list.addr + 8, (size_t)n * 32)) return -1;
    c->ivar_count = n;
    for (uint32_t i = 0; i < n; ++i) {
        ios27_objc20a_ivar *ivar = &c->ivars[i];
        uint64_t a = list.addr + 8 + (uint64_t)i * 32;
        ref20 offset;
        const uint8_t *p = location(r, list.sec, a, 32);
        if (!p || pointer_at(r, list.sec, a, &offset) != 1 ||
            !location(r, offset.sec, offset.addr, 4) ||
            string_slot(r, list.sec, a + 8, ivar->name) ||
            string_slot(r, list.sec, a + 16, ivar->encoding)) return -1;
        ivar->offset = rd32(location(r, offset.sec, offset.addr, 4));
        ivar->alignment_log2 = rd32(p + 24);
        ivar->size = rd32(p + 28);
        if (ivar->alignment_log2 > 5 || !ivar->size || ivar->size > 4096 ||
            ivar->offset > 4096 || ivar->size > 4096 - ivar->offset) return -1;
        for (uint32_t j = 0; j < i; ++j)
            if (!strcmp(c->ivars[j].name, ivar->name)) return -1;
    }
    return 0;
}
static int read_protocols(const reader20 *r, int sec, uint64_t slot,
                          char (*names)[IOS27_OBJC20A_NAME], uint32_t *count) {
    ref20 list;
    int present = optional_ref(r, sec, slot, &list);
    if (present <= 0) return present < 0 ? -1 : 0;
    const uint8_t *p = location(r, list.sec, list.addr, 8);
    if (!p || rd64(p) > IOS27_OBJC20A_ADOPTIONS - *count) return -1;
    uint64_t n = rd64(p);
    if (!location(r, list.sec, list.addr + 8, (size_t)n * 8)) return -1;
    for (uint64_t i = 0; i < n; ++i) {
        ref20 proto;
        uint64_t a = list.addr + 8 + i * 8;
        if (pointer_at(r, list.sec, a, &proto) != 1 ||
            !location(r, proto.sec, proto.addr, 64) ||
            string_slot(r, proto.sec, proto.addr + 8, names[*count])) return -1;
        for (uint32_t j = 0; j < *count; ++j)
            if (!strcmp(names[j], names[*count])) return -1;
        ++*count;
    }
    return 0;
}
static int read_protocol(const reader20 *r, ref20 address,
                         ios27_objc20a_protocol *p,
                         ios27_objc20a_image *image) {
    if (!location(r, address.sec, address.addr, 64) ||
        string_slot(r, address.sec, address.addr + 8, p->name) ||
        read_protocols(r, address.sec, address.addr + 16, p->protocols,
                       &p->protocol_count) ||
        read_methods(r, address.sec, address.addr + 24, p->methods,
                     &p->method_count, IOS27_OBJC20A_METHODS, 0, 0, 1, image) ||
        read_methods(r, address.sec, address.addr + 32, p->methods,
                     &p->method_count, IOS27_OBJC20A_METHODS, 1, 0, 1, image) ||
        read_methods(r, address.sec, address.addr + 40, p->methods,
                     &p->method_count, IOS27_OBJC20A_METHODS, 0, 1, 1, image) ||
        read_methods(r, address.sec, address.addr + 48, p->methods,
                     &p->method_count, IOS27_OBJC20A_METHODS, 1, 1, 1, image) ||
        read_properties(r, address.sec, address.addr + 56,
                        p->properties, &p->property_count)) return -1;
    return 0;
}
static int read_ro(const reader20 *r, ref20 ro, ios27_objc20a_class *c,
                   int meta, ios27_objc20a_image *image) {
    const uint8_t *p = location(r, ro.sec, ro.addr, 72);
    char name[IOS27_OBJC20A_NAME];
    if (!p || string_slot(r, ro.sec, ro.addr + 24, name) ||
        (meta ? strcmp(c->name, name) != 0 : 0)) return -1;
    if (!meta) {
        strcpy(c->name, name);
        c->instance_size = rd32(p + 8);
        if (c->instance_size > 4096 ||
            read_ivars(r, ro.sec, ro.addr + 48, c) ||
            read_properties(r, ro.sec, ro.addr + 64,
                            c->properties, &c->property_count) ||
            read_protocols(r, ro.sec, ro.addr + 40, c->protocols,
                           &c->protocol_count)) return -1;
    }
    return read_methods(r, ro.sec, ro.addr + 32, c->methods, &c->method_count,
                        IOS27_OBJC20A_METHODS, meta, 0, 0, image);
}
static int class_name_from_external(const char *symbol, char out[IOS27_OBJC20A_NAME]) {
    const char prefix[] = "_OBJC_CLASS_$_";
    if (strncmp(symbol, prefix, sizeof(prefix) - 1)) return -1;
    const char *name = symbol + sizeof(prefix) - 1;
    size_t n = strlen(name);
    if (!n || n >= IOS27_OBJC20A_NAME) return -1;
    strcpy(out, name);
    return 0;
}
static int walk_class(const reader20 *r, ref20 addr,
                      ios27_objc20a_class *c, ios27_objc20a_image *image) {
    if (!location(r, addr.sec, addr.addr, 40)) return -1;
    ref20 metaclass, ro, metaro;
    if (pointer_at(r, addr.sec, addr.addr, &metaclass) != 1 ||
        pointer_at(r, addr.sec, addr.addr + 32, &ro) != 1 ||
        !location(r, metaclass.sec, metaclass.addr, 40) ||
        pointer_at(r, metaclass.sec, metaclass.addr + 32, &metaro) != 1 ||
        read_ro(r, ro, c, 0, image) || read_ro(r, metaro, c, 1, image)) return -1;
    return 0;
}
static int scan_refs(const reader20 *r, const char *section,
                     ref20 *out, uint32_t *count, uint32_t max) {
    int index = section_index(r, section);
    if (index == -1) return 0;
    if (index < 0) return -1;
    const sec20 *s = &r->sec[index];
    if (s->size % 8 || s->size / 8 > max) return -1;
    *count = (uint32_t)(s->size / 8);
    for (uint32_t i = 0; i < *count; ++i) {
        if (pointer_at(r, index, s->addr + (uint64_t)i * 8, &out[i]) != 1)
            return -1;
    }
    return 0;
}
static int inspect_reader(const reader20 *r, ios27_objc20a_image *im) {
    ref20 classptr[IOS27_OBJC20A_CLASSES], protoptr[IOS27_OBJC20A_PROTOCOLS];
    ref20 catptr[IOS27_OBJC20A_CATEGORIES], selptr[IOS27_OBJC20A_SELECTORS];
    uint32_t cls = 0, pro = 0, cat = 0, sels = 0;
    if (scan_refs(r, "__objc_classlist", classptr, &cls, IOS27_OBJC20A_CLASSES) ||
        scan_refs(r, "__objc_protolist", protoptr, &pro, IOS27_OBJC20A_PROTOCOLS) ||
        scan_refs(r, "__objc_catlist", catptr, &cat, IOS27_OBJC20A_CATEGORIES) ||
        scan_refs(r, "__objc_selrefs", selptr, &sels, IOS27_OBJC20A_SELECTORS))
        return -1;
    im->class_count = cls;
    im->protocol_count = pro;
    im->category_count = cat;
    if (!cls && !pro && !cat) return -1;
    for (uint32_t i = 0; i < pro; ++i) {
        if (read_protocol(r, protoptr[i], &im->protocols[i], im)) return -1;
        for (uint32_t j = 0; j < i; ++j)
            if (!strcmp(im->protocols[j].name, im->protocols[i].name)) return -1;
    }
    for (uint32_t i = 0; i < cls; ++i) {
        if (walk_class(r, classptr[i], &im->classes[i], im)) return -1;
        for (uint32_t j = 0; j < i; ++j)
            if (!strcmp(im->classes[j].name, im->classes[i].name)) return -1;
    }
    for (uint32_t i = 0; i < cls; ++i) {
        ref20 parent;
        if (pointer_at(r, classptr[i].sec, classptr[i].addr + 8, &parent) < 0)
            return -1;
        if (parent.external[0]) {
            if (class_name_from_external(parent.external, im->classes[i].superclass))
                return -1;
        } else if (parent.sec || parent.addr) {
            int matched = 0;
            for (uint32_t j = 0; j < cls; ++j)
                if (classptr[j].sec == parent.sec &&
                    classptr[j].addr == parent.addr) {
                    strcpy(im->classes[i].superclass, im->classes[j].name);
                    matched = 1;
                    break;
                }
            if (!matched) return -1;
        } else {
            /* missing relocation means root superclass is exactly zero */
            int parent_state = pointer_at(r, classptr[i].sec, classptr[i].addr + 8, &parent);
            if (parent_state != 0) return -1;
        }
    }
    for (uint32_t i = 0; i < cat; ++i) {
        ios27_objc20a_category *c = &im->categories[i];
        ref20 target = catptr[i], owner;
        if (!location(r, target.sec, target.addr, 48) ||
            string_slot(r, target.sec, target.addr, c->name) ||
            pointer_at(r, target.sec, target.addr + 8, &owner) <= 0) return -1;
        if (owner.external[0]) {
            if (class_name_from_external(owner.external, c->class_name)) return -1;
        } else {
            int found = 0;
            for (uint32_t j = 0; j < cls; ++j)
                if (classptr[j].sec == owner.sec && classptr[j].addr == owner.addr) {
                    strcpy(c->class_name, im->classes[j].name);
                    found = 1;
                    break;
                }
            if (!found) return -1;
        }
        if (read_methods(r, target.sec, target.addr + 16, c->methods,
                         &c->method_count, IOS27_OBJC20A_METHODS, 0, 0, 0, im) ||
            read_methods(r, target.sec, target.addr + 24, c->methods,
                         &c->method_count, IOS27_OBJC20A_METHODS, 1, 0, 0, im) ||
            read_protocols(r, target.sec, target.addr + 32,
                           c->protocols, &c->protocol_count) ||
            read_properties(r, target.sec, target.addr + 40,
                            c->properties, &c->property_count)) return -1;
        for (uint32_t j = 0; j < i; ++j)
            if (!strcmp(c->class_name, im->categories[j].class_name) &&
                !strcmp(c->name, im->categories[j].name)) return -1;
    }
    for (uint32_t i = 0; i < sels; ++i) {
        char name[IOS27_OBJC20A_NAME];
        if (text_at(r, selptr[i], name) || add_selector(im, name)) return -1;
    }
    return 0;
}
int ios27_objc20a_inspect(const uint8_t *bytes, size_t size,
                         ios27_objc20a_image *out, char *err, size_t cap) {
    if (out) memset(out, 0, sizeof(*out));
    if (!bytes || !out || !size || size > MAX_FILE) goto invalid;
    reader20 *r = calloc(1, sizeof(*r));
    ios27_objc20a_image *scratch = calloc(1, sizeof(*scratch));
    if (!r || !scratch) {
        free(r); free(scratch);
        goto invalid;
    }
    r->f = bytes; r->size = size;
    int result = parse_macho(r) ? -1 : inspect_reader(r, scratch);
    if (!result) memcpy(out, scratch, sizeof(*out));
    free(scratch); free(r);
    if (result) goto invalid;
    if (err && cap) err[0] = 0;
    return 0;
invalid:
    if (err && cap) (void)snprintf(err, cap, "%s", "invalid or unsupported ARM64 Objective-C metadata");
    return -1;
}
