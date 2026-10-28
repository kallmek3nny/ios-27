#include "ios27/chained.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define LC_DYLD_CHAINED_FIXUPS 0x80000034u
#define DYLD_CHAINED_PTR_64 2u
#define DYLD_CHAINED_PTR_64_OFFSET 6u
#define DYLD_CHAINED_IMPORT 1u
#define DYLD_CHAINED_IMPORT_ADDEND 2u
#define DYLD_CHAINED_PTR_START_NONE 0xffffu
#define DYLD_CHAINED_PTR_START_MULTI 0x8000u

static int fail(char *error, size_t cap, const char *why) {
    if (error && cap) (void)snprintf(error, cap, "%s", why);
    return -1;
}
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4u) << 32u);
}
static int fits(uint64_t off, uint64_t length, size_t size) {
    return off <= (uint64_t)size && length <= (uint64_t)size - off;
}
static int plus(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > UINT64_MAX - a) return -1;
    *out = a + b;
    return 0;
}
static int same_segments(const ios27_macho_info *a,
                         const ios27_macho_info *b) {
    if (a->segment_count != b->segment_count ||
        a->segment_count > IOS27_MAX_SEGMENTS) return 0;
    for (size_t i = 0; i < a->segment_count; ++i) {
        const ios27_segment *x = &a->segments[i];
        const ios27_segment *y = &b->segments[i];
        if (x->fileoff != y->fileoff || x->filesize != y->filesize ||
            x->vmaddr != y->vmaddr || x->vmsize != y->vmsize ||
            x->initprot != y->initprot)
            return 0;
    }
    return 1;
}
static int vm_base(const ios27_macho_info *info, uint64_t *out) {
    int found = 0;
    for (size_t i = 0; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        if (s->filesize && (!found || s->vmaddr < *out)) {
            *out = s->vmaddr;
            found = 1;
        }
    }
    return found ? 0 : -1;
}
static int rebase_inside_image(const ios27_macho_info *info,
                               uint64_t base, uint64_t target) {
    for (size_t i = 0u; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        if (!strcmp(s->name, "__PAGEZERO") || !s->vmsize ||
            s->vmaddr < base || target < s->vmaddr - base)
            continue;
        if (target - (s->vmaddr - base) < s->vmsize)
            return 1;
    }
    return 0;
}
static int add_signed(uint64_t *value, int64_t addend) {
    uint64_t magnitude;
    if (addend >= 0) {
        magnitude = (uint64_t)addend;
        if (*value > UINT64_MAX - magnitude) return -1;
        *value += magnitude;
    } else {
        magnitude = (uint64_t)(-(addend + 1)) + 1u;
        if (*value < magnitude) return -1;
        *value -= magnitude;
    }
    return 0;
}
static int add_patch(const uint8_t *file, size_t size,
                     const ios27_macho_info *info,
                     ios27_chained_plan *plan, size_t seg_index,
                     uint64_t offset, uint64_t base,
                     uint16_t format, char *error, size_t cap,
                     uint32_t *next) {
    const ios27_segment *seg = &info->segments[seg_index];
    uint64_t file_off, raw;
    ios27_chained_patch *patch;
    if (plan->count >= IOS27_CHAINED_MAX_PATCHES)
        return fail(error, cap, "too many chained relocations");
    if (!(seg->initprot & 2u) || (seg->initprot & 4u) ||
        !fits(offset, 8u, (size_t)seg->filesize) ||
        plus(seg->fileoff, offset, &file_off) ||
        !fits(file_off, 8u, size))
        return fail(error, cap, "chain pointer outside writable file-backed segment");
    raw = rd64(file + (size_t)file_off);
    patch = &plan->patches[plan->count];
    memset(patch, 0, sizeof(*patch));
    patch->raw = raw;
    patch->segment_index = seg_index;
    patch->segment_offset = offset;
    patch->is_bind = (unsigned)(raw >> 63u);
    *next = (uint32_t)((raw >> 51u) & 0xfffu);
    if (patch->is_bind) {
        uint32_t ordinal = (uint32_t)(raw & 0xffffffu);
        uint8_t addend = (uint8_t)((raw >> 24u) & 0xffu);
        if (((raw >> 32u) & 0x7ffffu) != 0u ||
            ordinal >= plan->import_count)
            return fail(error, cap, "invalid chained bind ordinal/reserved bits");
        patch->import_index = ordinal;
        patch->addend = (int8_t)addend;
    } else {
        /* Only a 36-bit image offset; high8/tagged addresses are not handled. */
        if (((raw >> 36u) & 0x7fffu) != 0u)
            return fail(error, cap, "unsupported tagged/reserved rebase bits");
        patch->rebase_target = raw & ((UINT64_C(1) << 36u) - 1u);
        /* PTR_64 stores the preferred VM address; PTR_64_OFFSET stores
         * an offset from the image's lowest file-backed segment. */
        if (format == DYLD_CHAINED_PTR_64) {
            if (patch->rebase_target < base)
                return fail(error, cap, "PTR_64 target precedes preferred base");
            patch->rebase_target -= base;
        }
        if (!rebase_inside_image(info, base, patch->rebase_target))
            return fail(error, cap, "chained rebase outside mapped image");
    }
    ++plan->count;
    return 0;
}

int ios27_chained_parse(const uint8_t *file, size_t size,
                        const ios27_macho_info *info,
                        ios27_chained_plan *out,
                        char *error, size_t cap) {
    ios27_macho_info verified;
    uint32_t dataoff = 0u, datasize = 0u;
    uint32_t starts_off, imports_off, symbols_off, imports_count;
    uint32_t import_format, symbol_format, seg_count;
    uint32_t import_stride;
    size_t cursor, cmd_end;
    unsigned seen = 0u;
    const uint8_t *payload;
    uint64_t base;

    if (!file || !info || !out)
        return fail(error, cap, "null chained input");
    memset(out, 0, sizeof(*out));
    if (ios27_macho_parse(file, size, &verified, error, cap))
        return -1;
    if (verified.encrypted || !same_segments(&verified, info) ||
        vm_base(&verified, &base))
        return fail(error, cap, "encrypted/invalid/mismatched image");
    cursor = 32u;
    cmd_end = cursor + (size_t)rd32(file + 20u);
    for (uint32_t i = 0u; i < rd32(file + 16u); ++i) {
        uint32_t cmd = rd32(file + cursor);
        uint32_t cmdsize = rd32(file + cursor + 4u);
        if (cmd == LC_DYLD_CHAINED_FIXUPS) {
            if (seen++ || cmdsize < 16u)
                return fail(error, cap, "duplicate/truncated chained command");
            dataoff = rd32(file + cursor + 8u);
            datasize = rd32(file + cursor + 12u);
        }
        cursor += cmdsize;
        if (cursor > cmd_end)
            return fail(error, cap, "invalid command range");
    }
    if (seen != 1u || datasize < 28u || !fits(dataoff, datasize, size))
        return fail(error, cap, "missing/truncated chained payload");
    payload = file + dataoff;
    if (rd32(payload) != 0u)
        return fail(error, cap, "unsupported chained fixups version");
    starts_off = rd32(payload + 4u);
    imports_off = rd32(payload + 8u);
    symbols_off = rd32(payload + 12u);
    imports_count = rd32(payload + 16u);
    import_format = rd32(payload + 20u);
    symbol_format = rd32(payload + 24u);
    import_stride = import_format == DYLD_CHAINED_IMPORT ? 4u :
                    import_format == DYLD_CHAINED_IMPORT_ADDEND ? 8u : 0u;
    if (!import_stride || symbol_format != 0u ||
        imports_count > IOS27_CHAINED_MAX_IMPORTS ||
        !fits(starts_off, 4u, datasize) ||
        !fits(imports_off, (uint64_t)imports_count * import_stride, datasize) ||
        symbols_off > datasize ||
        (imports_count && symbols_off == datasize))
        return fail(error, cap, "unsupported or invalid chained header");
    out->import_count = imports_count;
    for (uint32_t i = 0u; i < imports_count; ++i) {
        uint32_t bits = rd32(payload + imports_off + (size_t)i * import_stride);
        uint32_t name_off = bits >> 9u;
        uint32_t name_start;
        size_t max_length, length;
        ios27_chained_import *entry = &out->imports[i];
        int32_t ordinal = (int32_t)(int8_t)(uint8_t)bits;
        if (ordinal <= 0 || name_off > UINT32_MAX - symbols_off)
            return fail(error, cap, "unsupported chained import flags/ordinal");
        name_start = symbols_off + name_off;
        if (name_start >= datasize)
            return fail(error, cap, "import symbol offset outside pool");
        max_length = (size_t)(datasize - name_start);
        length = 0u;
        while (length < max_length && payload[name_start + length] != 0u)
            ++length;
        if (!length || length == max_length || length >= sizeof(entry->symbol))
            return fail(error, cap, "unterminated/oversized chained symbol");
        memcpy(entry->symbol, payload + name_start, length);
        entry->ordinal = ordinal;
        entry->weak_import = (bits & 0x100u) != 0u;
        if (import_stride == 8u)
            entry->addend = (int32_t)rd32(payload + imports_off + (size_t)i * 8u + 4u);
    }
    seg_count = rd32(payload + starts_off);
    if (seg_count != verified.segment_count ||
        !fits(starts_off, 4u + (uint64_t)seg_count * 4u, datasize))
        return fail(error, cap, "chained starts/segment count mismatch");
    for (uint32_t i = 0u; i < seg_count; ++i) {
        uint32_t rel = rd32(payload + starts_off + 4u + (size_t)i * 4u);
        const uint8_t *segdata;
        const ios27_segment *seg = &verified.segments[i];
        uint64_t soff;
        uint32_t segsize;
        uint16_t pagesize, format, pagecount;
        if (!rel) continue;
        if (plus(starts_off, rel, &soff) ||
            !fits(soff, 22u, datasize))
            return fail(error, cap, "chained segment info out of range");
        segdata = payload + (size_t)soff;
        segsize = rd32(segdata);
        pagesize = rd16(segdata + 4u);
        format = rd16(segdata + 6u);
        pagecount = rd16(segdata + 20u);
        if ((format != DYLD_CHAINED_PTR_64_OFFSET &&
             format != DYLD_CHAINED_PTR_64) ||
            (pagesize != 4096u && pagesize != 16384u) ||
            pagecount > 4096u ||
            (uint64_t)pagecount > seg->vmsize / pagesize +
                                  (seg->vmsize % pagesize != 0u) ||
            segsize < 22u + (uint32_t)pagecount * 2u ||
            !fits(soff, segsize, datasize) ||
            seg->vmaddr < base ||
            rd64(segdata + 8u) != seg->vmaddr - base)
            return fail(error, cap, "unsupported chained pointer format/page/segment");
        for (uint32_t page = 0u; page < pagecount; ++page) {
            uint16_t start = rd16(segdata + 22u + page * 2u);
            uint64_t pageoff;
            uint32_t chainoff, next;
            if (start == DYLD_CHAINED_PTR_START_NONE) continue;
            if ((start & DYLD_CHAINED_PTR_START_MULTI) ||
                start > (uint32_t)pagesize - 8u ||
                (start & 3u))
                return fail(error, cap, "unsupported chained page start");
            pageoff = (uint64_t)page * pagesize;
            chainoff = start;
            /* Monotonically increasing 4-byte next strides bound each walk. */
            for (uint32_t steps = 0u; steps < (uint32_t)pagesize / 4u; ++steps) {
                uint64_t off;
                if (plus(pageoff, chainoff, &off) ||
                    add_patch(file, size, &verified, out, i, off,
                              base, format, error, cap, &next))
                    return -1;
                if (!next) break;
                if (next > ((uint32_t)pagesize - 8u - chainoff) / 4u)
                    return fail(error, cap, "chained next escapes page");
                chainoff += next * 4u;
            }
            if (out->count > IOS27_CHAINED_MAX_PATCHES)
                return fail(error, cap, "too many chained pointers");
        }
    }
    if (error && cap) error[0] = '\0';
    return 0;
}

int ios27_chained_apply(uint8_t *copy, size_t size,
                        const ios27_macho_info *info,
                        const ios27_chained_plan *plan,
                        uint64_t load_base,
                        ios27_bind_resolver resolver, void *context,
                        char *error, size_t cap) {
    struct staged_patch { size_t offset; uint64_t value; } staged[IOS27_CHAINED_MAX_PATCHES];
    ios27_macho_info verified;
    uint64_t base;
    if (!copy || !info || !plan ||
        plan->count > IOS27_CHAINED_MAX_PATCHES ||
        plan->import_count > IOS27_CHAINED_MAX_IMPORTS)
        return fail(error, cap, "invalid chained apply arguments");
    if (ios27_macho_parse(copy, size, &verified, error, cap) ||
        verified.encrypted || !same_segments(info, &verified) ||
        vm_base(&verified, &base))
        return fail(error, cap, "chained application image changed");
    for (size_t i = 0u; i < plan->count; ++i) {
        const ios27_chained_patch *patch = &plan->patches[i];
        const ios27_segment *seg;
        uint64_t off, value;
        if (patch->segment_index >= info->segment_count)
            return fail(error, cap, "invalid chained patch segment");
        seg = &info->segments[patch->segment_index];
        if (!(seg->initprot & 2u) || (seg->initprot & 4u) ||
            !fits(patch->segment_offset, 8u, (size_t)seg->filesize) ||
            plus(seg->fileoff, patch->segment_offset, &off) ||
            !fits(off, 8u, size) ||
            rd64(copy + (size_t)off) != patch->raw)
            return fail(error, cap, "chained patch outside original writable data");
        if (patch->is_bind) {
            const ios27_chained_import *imp;
            if (patch->import_index >= plan->import_count || !resolver)
                return fail(error, cap, "missing chained import/resolver");
            imp = &plan->imports[patch->import_index];
            if (imp->ordinal <= 0 || !imp->symbol[0] ||
                !memchr(imp->symbol, '\0', sizeof(imp->symbol)))
                return fail(error, cap, "invalid chained import");
            if (resolver(context, imp->symbol, imp->ordinal, &value)) {
                if (!imp->weak_import)
                    return fail(error, cap, "unresolved chained import");
                value = 0u; /* absent weak symbols never receive addends */
            } else if (add_signed(&value, imp->addend) ||
                       add_signed(&value, patch->addend)) {
                return fail(error, cap, "chained bind addend overflow/underflow");
            }
        } else {
            if (!rebase_inside_image(&verified, base, patch->rebase_target) ||
                plus(load_base, patch->rebase_target, &value))
                return fail(error, cap, "chained rebase outside image/overflow");
        }
        staged[i].offset = (size_t)off;
        staged[i].value = value;
        for (size_t j = 0u; j < i; ++j) {
            if (staged[j].offset < staged[i].offset + 8u &&
                staged[i].offset < staged[j].offset + 8u)
                return fail(error, cap, "overlapping chained patches");
        }
    }
    /* No partial modifications on validation or resolution failures. */
    for (size_t i = 0u; i < plan->count; ++i)
        for (unsigned byte = 0u; byte < 8u; ++byte)
            copy[staged[i].offset + byte] =
                (uint8_t)(staged[i].value >> (8u * byte));
    if (error && cap) error[0] = '\0';
    return 0;
}
