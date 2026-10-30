#include "ios27/phase25a_inspect.h"
#include <stdint.h>
#include <string.h>

#define SIZE_MAX_IMAGE (8u * 1024u * 1024u)
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4u) << 32u);
}
static int fits(size_t size, uint64_t at, uint64_t count) {
    return at <= size && count <= size - at;
}
static int secname(const uint8_t *p, const char *name) {
    size_t n = strlen(name);
    return n <= 16u && !memcmp(p, name, n) &&
           (n == 16u || p[n] == 0u);
}
int ios27_phase25a_preflight(const uint8_t *file, size_t size,
                            ios27_phase25a_summary *out) {
    ios27_phase25a_summary tmp = {0};
    uint32_t ncmds, bytes, chained = 0u;
    uint64_t position, end;
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!file || size < 32u || size > SIZE_MAX_IMAGE ||
        rd32(file) != 0xfeedfacfu || rd32(file + 4u) != 0x0100000cu ||
        (rd32(file + 8u) & 0xffu) != 0u || rd32(file + 12u) != 6u)
        return -1;
    ncmds = rd32(file + 16u);
    bytes = rd32(file + 20u);
    if (!ncmds || ncmds > 256u || !fits(size, 32u, bytes)) return -1;
    position = 32u;
    end = 32u + bytes;
    for (uint32_t i = 0u; i < ncmds; ++i) {
        uint32_t op, length;
        if (position > end || end - position < 8u) return -1;
        op = rd32(file + position);
        length = rd32(file + position + 4u);
        if (length < 8u || (length & 7u) || length > end - position)
            return -1;
        if (op == 0x19u) {
            uint32_t nsections;
            uint64_t vm, vmsz, foff, fsz;
            uint32_t protections, maxprotection;
            if (length < 72u || tmp.segments == 24u) return -1;
            vm = rd64(file + position + 24u);
            vmsz = rd64(file + position + 32u);
            foff = rd64(file + position + 40u);
            fsz = rd64(file + position + 48u);
            maxprotection = rd32(file + position + 56u);
            protections = rd32(file + position + 60u);
            nsections = rd32(file + position + 64u);
            if (nsections > (length - 72u) / 80u ||
                nsections > 128u - tmp.sections || fsz > vmsz ||
                (fsz && !fits(size, foff, fsz)) ||
                vm > UINT64_MAX - vmsz || (protections & ~7u) ||
                (maxprotection & ~7u) || (protections & ~maxprotection) ||
                ((protections & 6u) == 6u)) return -1;
            ++tmp.segments;
            for (uint32_t j = 0u; j < nsections; ++j) {
                const uint8_t *s = file + position + 72u + 80u * j;
                uint64_t a = rd64(s + 32u), z = rd64(s + 40u);
                uint32_t at = rd32(s + 48u);
                if (a < vm || z > vmsz || a - vm > vmsz - z ||
                    (z && (rd32(s + 64u) & 0xffu) != 1u &&
                     !fits(size, at, z))) return -1;
                ++tmp.sections;
                if (secname(s, "__objc_classlist") ||
                    secname(s, "__objc_catlist") ||
                    secname(s, "__objc_selrefs") ||
                    secname(s, "__objc_classrefs") ||
                    secname(s, "__objc_superrefs") ||
                    secname(s, "__objc_protolist")) {
                    if ((z & 7u) || z > 128u * 8u) return -1;
                    ++tmp.metadata_sections;
                    if (secname(s, "__objc_classlist")) tmp.class_slots += (uint32_t)(z / 8u);
                    if (secname(s, "__objc_catlist")) tmp.category_slots += (uint32_t)(z / 8u);
                    if (secname(s, "__objc_selrefs")) tmp.selector_slots += (uint32_t)(z / 8u);
                }
            }
        } else if (op == 0x80000034u) {
            uint32_t off, z;
            if (length < 16u || chained++) return -1;
            off = rd32(file + position + 8u);
            z = rd32(file + position + 12u);
            if (z < 28u || !fits(size, off, z)) return -1;
            tmp.chained_fixup_bytes = z;
        } else if (op == 0x22u || op == 0x80000022u) {
            if (length < 48u || rd32(file + position + 8u) ||
                rd32(file + position + 16u) ||
                rd32(file + position + 24u) ||
                rd32(file + position + 32u)) return -1;
        }
        position += length;
    }
    if (position != end || !tmp.segments || !chained ||
        !tmp.metadata_sections) return -1;
    *out = tmp;
    return 0;
}
