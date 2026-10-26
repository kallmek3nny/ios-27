#include "ios27/binds.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define LC_DYLD_INFO 0x22u
#define LC_DYLD_INFO_ONLY 0x80000022u
#define LC_DYLD_CHAINED_FIXUPS 0x80000034u

#define BIND_DONE 0x00u
#define BIND_DYLIB_IMM 0x10u
#define BIND_DYLIB_ULEB 0x20u
#define BIND_DYLIB_SPECIAL 0x30u
#define BIND_SYMBOL 0x40u
#define BIND_TYPE 0x50u
#define BIND_ADDEND 0x60u
#define BIND_SEGMENT 0x70u
#define BIND_ADD_ADDR 0x80u
#define BIND_DO 0x90u
#define BIND_DO_ADD_ADDR 0xa0u
#define BIND_DO_ADD_IMM 0xb0u
#define BIND_DO_REPEAT 0xc0u
#define BIND_POINTER 1u

static int fail(char *error, size_t capacity, const char *reason) {
    if (error && capacity)
        (void)snprintf(error, capacity, "%s", reason);
    return -1;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > UINT64_MAX - a) return -1;
    *out = a + b;
    return 0;
}

static int uleb(const uint8_t *stream, size_t length,
                size_t *position, uint64_t *out) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 10u; ++i) {
        uint8_t c;
        unsigned shift = 7u * i;
        if (*position >= length) return -1;
        c = stream[(*position)++];
        if (shift == 63u && (c & 0x7fu) > 1u) return -1;
        value |= (uint64_t)(c & 0x7fu) << shift;
        if (!(c & 0x80u)) {
            *out = value;
            return 0;
        }
    }
    return -1;
}

static int sleb(const uint8_t *stream, size_t length,
                size_t *position, int64_t *out) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 10u; ++i) {
        uint8_t c;
        unsigned shift = i * 7u;
        if (*position >= length) return -1;
        c = stream[(*position)++];
        if (shift == 63u) {
            if (c != 0u && c != 0x7fu) return -1;
            if (c == 0x7fu) value |= UINT64_C(1) << 63u;
        } else {
            value |= (uint64_t)(c & 0x7fu) << shift;
        }
        if (!(c & 0x80u)) {
            unsigned total = shift + 7u;
            if (total < 64u && (c & 0x40u))
                value |= (~UINT64_C(0)) << total;
            memcpy(out, &value, sizeof(value));
            return 0;
        }
    }
    return -1;
}

typedef struct {
    int32_t ordinal;
    char symbol[IOS27_BIND_SYMBOL_MAX];
    int64_t addend;
    uint64_t segment_offset;
    size_t segment_index;
    unsigned type;
    unsigned has_symbol : 1;
    unsigned has_segment : 1;
    unsigned has_ordinal : 1;
} bind_state;

static int emit(const ios27_macho_info *info, bind_state *state,
                ios27_bind_plan *out, char *err, size_t cap) {
    const ios27_segment *s;
    ios27_bind *entry;
    if (!state->has_symbol || !state->has_segment || !state->has_ordinal ||
        state->type != BIND_POINTER || out->count >= IOS27_MAX_BINDS)
        return fail(err, cap, "incomplete bind state or too many bindings");
    if (state->segment_index >= info->segment_count)
        return fail(err, cap, "invalid bind segment index");
    s = &info->segments[state->segment_index];
    if (!(s->initprot & 2u) || (s->initprot & 4u) ||
        state->segment_offset > s->filesize ||
        s->filesize - state->segment_offset < 8u ||
        state->segment_offset > s->vmsize ||
        s->vmsize - state->segment_offset < 8u)
        return fail(err, cap, "bind targets non-writable or non-file-backed segment");
    entry = &out->entries[out->count++];
    memcpy(entry->symbol, state->symbol, sizeof(entry->symbol));
    entry->ordinal = state->ordinal;
    entry->addend = state->addend;
    entry->segment_offset = state->segment_offset;
    entry->segment_index = state->segment_index;
    if (add_u64(state->segment_offset, 8u, &state->segment_offset))
        return fail(err, cap, "bind address overflow");
    return 0;
}

int ios27_binds_parse(const uint8_t *file, size_t size,
                      const ios27_macho_info *info,
                      ios27_bind_plan *out, char *error, size_t error_size) {
    ios27_macho_info verified;
    size_t cursor, end;
    uint32_t count;
    uint32_t bind_off = 0u, bind_size = 0u;
    unsigned info_seen = 0u;
    bind_state state;
    size_t pos = 0u;
    const uint8_t *stream;

    if (!file || !out || !info)
        return fail(error, error_size, "null bind input");
    memset(out, 0, sizeof(*out));
    if (ios27_macho_parse(file, size, &verified, error, error_size))
        return -1;
    if (verified.encrypted || verified.segment_count != info->segment_count)
        return fail(error, error_size, "encrypted or mismatched Mach-O");
    count = rd32(file + 16u);
    cursor = 32u;
    end = cursor + (size_t)rd32(file + 20u);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t cmd = rd32(file + cursor);
        uint32_t cmd_size = rd32(file + cursor + 4u);
        if ((size_t)cmd_size > end - cursor)
            return fail(error, error_size, "invalid load command");
        if (cmd == LC_DYLD_CHAINED_FIXUPS)
            return fail(error, error_size, "modern chained fixups not supported");
        if (cmd == LC_DYLD_INFO || cmd == LC_DYLD_INFO_ONLY) {
            if (info_seen++ || cmd_size < 48u)
                return fail(error, error_size, "duplicate/truncated dyld info");
            if (rd32(file + cursor + 8u) || rd32(file + cursor + 12u) ||
                rd32(file + cursor + 24u) || rd32(file + cursor + 28u) ||
                rd32(file + cursor + 32u) || rd32(file + cursor + 36u))
                return fail(error, error_size, "rebase/weak/lazy bind streams not supported");
            bind_off = rd32(file + cursor + 16u);
            bind_size = rd32(file + cursor + 20u);
        }
        cursor += (size_t)cmd_size;
    }
    if (!bind_size) {
        if (error && error_size) error[0] = '\0';
        return 0;
    }
    if ((size_t)bind_off > size || (size_t)bind_size > size - bind_off)
        return fail(error, error_size, "bind stream outside Mach-O");
    stream = file + bind_off;
    memset(&state, 0, sizeof(state));
    state.type = BIND_POINTER;
    while (pos < (size_t)bind_size) {
        uint8_t instruction = stream[pos++];
        unsigned opcode = instruction & 0xf0u;
        unsigned imm = instruction & 0x0fu;
        uint64_t n;
        switch (opcode) {
            case BIND_DONE:
                if (imm)
                    return fail(error, error_size, "invalid bind DONE opcode");
                while (pos < bind_size) {
                    if (stream[pos++] != 0u)
                        return fail(error, error_size, "unexpected bytes after bind DONE");
                }
                if (error && error_size) error[0] = '\0';
                return 0;
            case BIND_DYLIB_IMM:
                if (!imm) return fail(error, error_size, "invalid library ordinal");
                state.ordinal = (int32_t)imm;
                state.has_ordinal = 1u;
                break;
            case BIND_DYLIB_ULEB:
                if (uleb(stream, bind_size, &pos, &n) || n > INT32_MAX || !n)
                    return fail(error, error_size, "invalid extended library ordinal");
                state.ordinal = (int32_t)n;
                state.has_ordinal = 1u;
                break;
            case BIND_DYLIB_SPECIAL:
                state.ordinal = imm & 8u ? (int32_t)imm - 16 : (int32_t)imm;
                if (state.ordinal > 0)
                    return fail(error, error_size, "invalid special ordinal");
                state.has_ordinal = 1u;
                break;
            case BIND_SYMBOL: {
                size_t begin = pos;
                if (imm != 0u)
                    return fail(error, error_size, "weak and other symbol flags unsupported");
                while (pos < bind_size && stream[pos] != 0u) ++pos;
                if (pos == bind_size || pos == begin ||
                    pos - begin >= sizeof(state.symbol))
                    return fail(error, error_size, "invalid bind symbol name");
                memset(state.symbol, 0, sizeof(state.symbol));
                memcpy(state.symbol, stream + begin, pos - begin);
                state.has_symbol = 1u;
                ++pos;
                break;
            }
            case BIND_TYPE:
                if (imm != BIND_POINTER)
                    return fail(error, error_size, "only pointer bind type supported");
                state.type = imm;
                break;
            case BIND_ADDEND:
                if (imm || sleb(stream, bind_size, &pos, &state.addend))
                    return fail(error, error_size, "invalid bind addend");
                break;
            case BIND_SEGMENT:
                if (imm >= verified.segment_count ||
                    uleb(stream, bind_size, &pos, &state.segment_offset))
                    return fail(error, error_size, "invalid segment/offset bind");
                state.segment_index = imm;
                state.has_segment = 1u;
                break;
            case BIND_ADD_ADDR:
                if (imm || uleb(stream, bind_size, &pos, &n) ||
                    add_u64(state.segment_offset, n, &state.segment_offset))
                    return fail(error, error_size, "invalid bind address advancement");
                break;
            case BIND_DO:
                if (imm || emit(&verified, &state, out, error, error_size))
                    return -1;
                break;
            case BIND_DO_ADD_ADDR:
                if (imm || uleb(stream, bind_size, &pos, &n) ||
                    emit(&verified, &state, out, error, error_size) ||
                    add_u64(state.segment_offset, n, &state.segment_offset))
                    return fail(error, error_size, "invalid bind/add instruction");
                break;
            case BIND_DO_ADD_IMM:
                if (emit(&verified, &state, out, error, error_size) ||
                    add_u64(state.segment_offset, (uint64_t)imm * 8u,
                            &state.segment_offset))
                    return fail(error, error_size, "invalid scaled bind");
                break;
            case BIND_DO_REPEAT: {
                uint64_t repeats, skip;
                if (imm || uleb(stream, bind_size, &pos, &repeats) ||
                    uleb(stream, bind_size, &pos, &skip) ||
                    repeats > IOS27_MAX_BINDS - out->count)
                    return fail(error, error_size, "invalid repeated bind");
                for (uint64_t i = 0u; i < repeats; ++i) {
                    if (emit(&verified, &state, out, error, error_size) ||
                        add_u64(state.segment_offset, skip,
                                &state.segment_offset))
                        return fail(error, error_size, "invalid repeated bind address");
                }
                break;
            }
            default:
                return fail(error, error_size, "unsupported classic bind opcode");
        }
    }
    return fail(error, error_size, "unterminated dyld bind stream");
}

int ios27_binds_apply(uint8_t *copy, size_t copy_size,
                      const ios27_macho_info *info,
                      const ios27_bind_plan *plan,
                      ios27_bind_resolver resolver, void *context,
                      char *error, size_t error_size) {
    struct patch { size_t off; uint64_t value; } staged[IOS27_MAX_BINDS];
    if (!copy || !info || !plan || !resolver ||
        plan->count > IOS27_MAX_BINDS ||
        info->segment_count > IOS27_MAX_SEGMENTS)
        return fail(error, error_size, "invalid bind application input");

    for (size_t i = 0; i < plan->count; ++i) {
        const ios27_bind *entry = &plan->entries[i];
        const ios27_segment *segment;
        uint64_t offset, value, addend;
        if (entry->segment_index >= info->segment_count ||
            !memchr(entry->symbol, '\0', IOS27_BIND_SYMBOL_MAX) ||
            entry->symbol[0] == '\0')
            return fail(error, error_size, "invalid symbolic bind entry");
        segment = &info->segments[entry->segment_index];
        if (!(segment->initprot & 2u) || (segment->initprot & 4u) ||
            segment->fileoff > copy_size ||
            segment->filesize > copy_size - segment->fileoff ||
            entry->segment_offset > segment->filesize ||
            segment->filesize - entry->segment_offset < 8u ||
            add_u64(segment->fileoff, entry->segment_offset, &offset) ||
            offset > copy_size || copy_size - (size_t)offset < 8u)
            return fail(error, error_size, "bind patch outside writable file-backed segment");
        if (resolver(context, entry->symbol, entry->ordinal, &value))
            return fail(error, error_size, "unresolved symbol or library ordinal");
        if (entry->addend >= 0) {
            addend = (uint64_t)entry->addend;
            if (value > UINT64_MAX - addend)
                return fail(error, error_size, "positive bind addend overflow");
            value += addend;
        } else {
            addend = (uint64_t)(-(entry->addend + 1)) + 1u;
            if (value < addend)
                return fail(error, error_size, "negative bind addend underflow");
            value -= addend;
        }
        staged[i].off = (size_t)offset;
        staged[i].value = value;
        for (size_t j = 0; j < i; ++j) {
            if (staged[j].off < staged[i].off + 8u &&
                staged[i].off < staged[j].off + 8u)
                return fail(error, error_size, "overlapping bind patches");
        }
    }
    /* All validity/resolution checks have passed. Write only to the caller's buffer. */
    for (size_t i = 0; i < plan->count; ++i)
        for (unsigned byte = 0u; byte < 8u; ++byte)
            copy[staged[i].off + byte] =
                (uint8_t)(staged[i].value >> (byte * 8u));
    if (error && error_size) error[0] = '\0';
    return 0;
}
