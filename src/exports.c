#include "ios27/exports.h"
#include "ios27/macho.h"

#include <stdio.h>
#include <string.h>

#define LC_DYLD_INFO 0x22u
#define LC_DYLD_INFO_ONLY 0x80000022u
#define LC_DYLD_EXPORTS_TRIE 0x80000033u
#define MAX_DEPTH 48u
#define MAX_VISITS 8192u

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int fail(char *error, size_t cap, const char *reason) {
    if (error && cap)
        (void)snprintf(error, cap, "%s", reason);
    return -1;
}

static int read_uleb(const uint8_t *data, size_t end,
                     size_t *cursor, uint64_t *value) {
    uint64_t output = 0;
    unsigned shift = 0;
    for (unsigned i = 0; i < 10u; ++i) {
        uint8_t byte;
        uint64_t part;
        if (*cursor >= end) return -1;
        byte = data[(*cursor)++];
        part = (uint64_t)(byte & 0x7fu);
        if (shift == 63u && part > 1u) return -1;
        if (shift > 63u) return -1;
        output |= part << shift;
        if ((byte & 0x80u) == 0u) {
            if (i != 0u && byte == 0u) return -1; /* nonminimal ULEB */
            *value = output;
            return 0;
        }
        shift += 7u;
    }
    return -1;
}

typedef struct {
    const uint8_t *trie;
    size_t size;
    size_t visits;
    size_t active[MAX_DEPTH];
    ios27_exports *out;
} walker;

static int walk(walker *ctx, size_t offset,
                char name[IOS27_EXPORT_NAME_MAX], size_t name_len,
                size_t depth) {
    size_t pos, terminal_end;
    uint64_t term_length;
    uint8_t children;

    if (depth >= MAX_DEPTH || offset >= ctx->size ||
        ++ctx->visits > MAX_VISITS)
        return -1;
    for (size_t i = 0; i < depth; ++i)
        if (ctx->active[i] == offset) return -1;
    ctx->active[depth] = offset;
    pos = offset;
    if (read_uleb(ctx->trie, ctx->size, &pos, &term_length))
        return -1;
    if (term_length > (uint64_t)(ctx->size - pos))
        return -1;
    terminal_end = pos + (size_t)term_length;
    if (term_length != 0u) {
        ios27_export *exp;
        uint64_t flags, addr;
        if (name_len == 0u || ctx->out->count == IOS27_MAX_EXPORTS ||
            read_uleb(ctx->trie, terminal_end, &pos, &flags))
            return -1;
        /* Reject unknown reserved flag bits and mutually exclusive forms. */
        if ((flags & ~UINT64_C(0x1f)) != 0u ||
            (flags & IOS27_EXPORT_KIND_MASK) == 3u ||
            ((flags & (IOS27_EXPORT_REEXPORT | IOS27_EXPORT_STUB_RESOLVER)) &&
             (flags & IOS27_EXPORT_KIND_MASK) != IOS27_EXPORT_KIND_REGULAR) ||
            ((flags & IOS27_EXPORT_REEXPORT) &&
             (flags & IOS27_EXPORT_STUB_RESOLVER)))
            return -1;
        if (ios27_exports_find(ctx->out, name))
            return -1;
        exp = &ctx->out->entries[ctx->out->count];
        memset(exp, 0, sizeof(*exp));
        memcpy(exp->name, name, name_len + 1u);
        exp->flags = flags;
        if ((flags & IOS27_EXPORT_REEXPORT) != 0u) {
            size_t start, length;
            if (read_uleb(ctx->trie, terminal_end, &pos, &exp->ordinal) ||
                exp->ordinal == 0u)
                return -1;
            start = pos;
            while (pos < terminal_end && ctx->trie[pos] != 0u) ++pos;
            if (pos == terminal_end) return -1;
            length = pos - start;
            if (length >= IOS27_EXPORT_NAME_MAX) return -1;
            memcpy(exp->imported_name, ctx->trie + start, length);
            exp->imported_name[length] = '\0';
            ++pos;
        } else {
            if (read_uleb(ctx->trie, terminal_end, &pos, &addr))
                return -1;
            exp->address = addr;
            if (flags & IOS27_EXPORT_STUB_RESOLVER) {
                if (read_uleb(ctx->trie, terminal_end, &pos, &exp->resolver))
                    return -1;
            }
        }
        if (pos != terminal_end) return -1;
        ++ctx->out->count;
    }
    pos = terminal_end;
    if (pos >= ctx->size) return -1;
    children = ctx->trie[pos++];
    for (unsigned i = 0; i < children; ++i) {
        size_t begin, edge_len;
        uint64_t child;
        begin = pos;
        while (pos < ctx->size && ctx->trie[pos] != 0u) ++pos;
        if (pos == ctx->size) return -1;
        edge_len = pos - begin;
        if (edge_len == 0u || edge_len >= IOS27_EXPORT_NAME_MAX - name_len)
            return -1;
        memcpy(name + name_len, ctx->trie + begin, edge_len);
        name[name_len + edge_len] = '\0';
        ++pos;
        if (read_uleb(ctx->trie, ctx->size, &pos, &child) ||
            child >= (uint64_t)ctx->size)
            return -1;
        if (walk(ctx, (size_t)child, name, name_len + edge_len, depth + 1u))
            return -1;
        name[name_len] = '\0';
    }
    return 0;
}

const ios27_export *ios27_exports_find(const ios27_exports *exports,
                                       const char *symbol_name) {
    if (!exports || !symbol_name || exports->count > IOS27_MAX_EXPORTS ||
        !memchr(symbol_name, '\0', IOS27_EXPORT_NAME_MAX)) return NULL;
    for (size_t i = 0; i < exports->count; ++i) {
        if (!memchr(exports->entries[i].name, '\0', IOS27_EXPORT_NAME_MAX))
            return NULL;
        if (strcmp(exports->entries[i].name, symbol_name) == 0)
            return &exports->entries[i];
    }
    return NULL;
}

int ios27_exports_parse(const uint8_t *image, size_t size,
                        ios27_exports *out,
                        char *error, size_t error_capacity) {
    ios27_macho_info info;
    size_t cursor;
    uint32_t count;
    uint32_t legacy_off = 0u, legacy_len = 0u;
    uint32_t modern_off = 0u, modern_len = 0u;
    unsigned seen_modern = 0u, seen_legacy = 0u;
    size_t trie_off, trie_len;

    if (!image || !out) return fail(error, error_capacity, "null exports input");
    memset(out, 0, sizeof(*out));
    if (ios27_macho_parse(image, size, &info, error, error_capacity))
        return -1;
    count = rd32(image + 16u);
    cursor = 32u;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t cmd = rd32(image + cursor);
        uint32_t cmdsize = rd32(image + cursor + 4u);
        if (cmd == LC_DYLD_EXPORTS_TRIE) {
            if (seen_modern++ || cmdsize < 16u)
                return fail(error, error_capacity, "invalid exports trie command");
            modern_off = rd32(image + cursor + 8u);
            modern_len = rd32(image + cursor + 12u);
        } else if (cmd == LC_DYLD_INFO || cmd == LC_DYLD_INFO_ONLY) {
            if (seen_legacy++ || cmdsize < 48u)
                return fail(error, error_capacity, "invalid dyld info command");
            legacy_off = rd32(image + cursor + 40u);
            legacy_len = rd32(image + cursor + 44u);
        }
        cursor += cmdsize;
    }
    trie_off = seen_modern ? modern_off : legacy_off;
    trie_len = seen_modern ? modern_len : legacy_len;
    if (trie_len == 0u) {
        if (error && error_capacity) error[0] = '\0';
        return 0;
    }
    if (trie_off > size || trie_len > size - trie_off)
        return fail(error, error_capacity, "exports trie exceeds Mach-O bounds");
    {
        walker ctx;
        char symbol[IOS27_EXPORT_NAME_MAX] = {0};
        memset(&ctx, 0, sizeof(ctx));
        ctx.trie = image + trie_off;
        ctx.size = trie_len;
        ctx.out = out;
        if (walk(&ctx, 0u, symbol, 0u, 0u)) {
            memset(out, 0, sizeof(*out));
            return fail(error, error_capacity, "malformed exports trie");
        }
    }
    out->has_trie = 1u;
    if (error && error_capacity) error[0] = '\0';
    return 0;
}


/* Color-marked DFS rejects cyclic and out-of-range dependency graphs before
 * any symbol lookup. At most 32 nodes bound the recursion and stack. */
static int check_graph(const ios27_export_library *libs, size_t count,
                       size_t node, uint8_t colors[IOS27_EXPORT_MAX_LIBRARIES]) {
    const ios27_export_library *lib = &libs[node];
    if (colors[node] == 1u) return -1;
    if (colors[node] == 2u) return 0;
    if (!lib->exports || lib->exports->count > IOS27_MAX_EXPORTS ||
        lib->dependency_count > IOS27_EXPORT_MAX_LIBRARIES ||
        (lib->dependency_count && !lib->dependencies))
        return -1;
    colors[node] = 1u;
    for (size_t i = 0; i < lib->dependency_count; ++i) {
        size_t child = lib->dependencies[i];
        if (child >= count || check_graph(libs, count, child, colors))
            return -1;
    }
    colors[node] = 2u;
    return 0;
}

int ios27_exports_resolve(const ios27_export_library *libraries,
                          size_t library_count, size_t library_index,
                          const char *symbol_name,
                          ios27_export_resolution *out,
                          char *error, size_t error_capacity) {
    uint8_t colors[IOS27_EXPORT_MAX_LIBRARIES] = {0};
    const char *name = symbol_name;
    size_t current = library_index;
    if (!out) return fail(error, error_capacity, "null resolution output");
    memset(out, 0, sizeof(*out));
    if (!libraries || library_count == 0u ||
        library_count > IOS27_EXPORT_MAX_LIBRARIES ||
        library_index >= library_count || !name ||
        !memchr(name, '\0', IOS27_EXPORT_NAME_MAX) || !name[0])
        return fail(error, error_capacity, "invalid export resolution input");
    for (size_t i = 0; i < library_count; ++i) {
        if (check_graph(libraries, library_count, i, colors))
            return fail(error, error_capacity, "invalid or cyclic dependency graph");
    }
    /* Each reexport follows one DAG edge, so hops cannot exceed node count. */
    for (size_t hop = 0; hop < library_count; ++hop) {
        const ios27_export_library *lib = &libraries[current];
        const ios27_export *entry = ios27_exports_find(lib->exports, name);
        if (!entry) {
            if (error && error_capacity) error[0] = '\0';
            return 1; /* exported symbol is absent */
        }
        if (!(entry->flags & IOS27_EXPORT_REEXPORT)) {
            out->symbol = entry;
            out->library_index = current;
            if (error && error_capacity) error[0] = '\0';
            return 0;
        }
        if (!entry->ordinal || entry->ordinal > lib->dependency_count ||
            !memchr(entry->imported_name, '\0', IOS27_EXPORT_NAME_MAX))
            return fail(error, error_capacity, "invalid reexport ordinal or alias");
        current = lib->dependencies[(size_t)entry->ordinal - 1u];
        if (entry->imported_name[0]) name = entry->imported_name;
    }
    return fail(error, error_capacity, "reexport hop limit exceeded");
}
