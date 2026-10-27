#include "ios27/exports.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FILE_BYTES 256u
#define DATA_OFF 128u

static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(v >> (8u * i));
}

static void fixture(uint8_t b[FILE_BYTES], uint32_t command) {
    static const uint8_t trie[] = {
        0x00, 0x02,
        '_', 'a', 'l', 'p', 'h', 'a', 0x00, 0x11,
        '_', 'b', 'e', 't', 'a', 0x00, 0x16,
        0x03, 0x00, 0xb4, 0x24, 0x00,
        0x0c, 0x08, 0x01,
        '_', 'o', 'r', 'i', 'g', 'i', 'n', 'a', 'l', 0x00, 0x00
    };
    memset(b, 0, FILE_BYTES);
    put32(b, 0xfeedfacfu);
    put32(b + 4u, 0x0100000cu);
    put32(b + 12u, 6u); /* MH_DYLIB */
    put32(b + 16u, 1u);
    put32(b + 20u, command == 0x80000033u ? 16u : 48u);
    put32(b + 32u, command);
    put32(b + 36u, command == 0x80000033u ? 16u : 48u);
    if (command == 0x80000033u) {
        put32(b + 40u, DATA_OFF);
        put32(b + 44u, (uint32_t)sizeof(trie));
    } else {
        put32(b + 72u, DATA_OFF); /* legacy export_off at command + 40 */
        put32(b + 76u, (uint32_t)sizeof(trie));
    }
    memcpy(b + DATA_OFF, trie, sizeof(trie));
}

/* Original, SDK-free single-symbol export trie. Every byte is built in test. */
static void one_export(uint8_t b[FILE_BYTES], const char *name,
                       uint8_t flags, uint8_t data, const char *alias) {
    size_t n = strlen(name), terminal, p;
    fixture(b, 0x80000033u);
    assert(n < 64u);
    memset(b + DATA_OFF, 0, FILE_BYTES - DATA_OFF);
    b[DATA_OFF] = 0u;
    b[DATA_OFF + 1u] = 1u;
    memcpy(b + DATA_OFF + 2u, name, n);
    terminal = 4u + n;
    b[DATA_OFF + 2u + n] = 0u;
    b[DATA_OFF + 3u + n] = (uint8_t)terminal;
    p = terminal + 1u;
    b[DATA_OFF + p++] = flags;
    b[DATA_OFF + p++] = data; /* one-byte ULEB address or ordinal */
    if (flags & IOS27_EXPORT_REEXPORT) {
        size_t alias_len = strlen(alias);
        assert(alias_len + p + 2u < FILE_BYTES - DATA_OFF);
        memcpy(b + DATA_OFF + p, alias, alias_len);
        p += alias_len;
        b[DATA_OFF + p++] = 0u;
    }
    b[DATA_OFF + terminal] = (uint8_t)(p - terminal - 1u);
    b[DATA_OFF + p++] = 0u; /* zero children */
    put32(b + 44u, (uint32_t)p);
}

static void test_dependency_resolution(void) {
    uint8_t image[FILE_BYTES];
    ios27_exports a, b, c;
    size_t dep_a[] = {1u}, dep_b[] = {2u}, dep_c[] = {0u};
    ios27_export_library graph[3];
    ios27_export_resolution resolved;
    char err[128];

    fixture(image, 0x80000033u);
    assert(ios27_exports_parse(image, sizeof(image), &a, err, sizeof(err)) == 0);
    one_export(image, "_original", IOS27_EXPORT_REEXPORT, 1u, "_target");
    assert(ios27_exports_parse(image, sizeof(image), &b, err, sizeof(err)) == 0);
    one_export(image, "_target", IOS27_EXPORT_WEAK_DEFINITION, 0x33u, NULL);
    assert(ios27_exports_parse(image, sizeof(image), &c, err, sizeof(err)) == 0);
    graph[0] = (ios27_export_library){ &a, dep_a, 1u };
    graph[1] = (ios27_export_library){ &b, dep_b, 1u };
    graph[2] = (ios27_export_library){ &c, NULL, 0u };

    assert(ios27_exports_resolve(graph, 3u, 0u, "_alpha",
                                &resolved, err, sizeof(err)) == 0);
    assert(resolved.library_index == 0u && resolved.symbol->address == 0x1234u);
    assert(ios27_exports_resolve(graph, 3u, 0u, "_beta",
                                &resolved, err, sizeof(err)) == 0);
    assert(resolved.library_index == 2u);
    assert(strcmp(resolved.symbol->name, "_target") == 0);
    assert(resolved.symbol->flags & IOS27_EXPORT_WEAK_DEFINITION);
    assert(resolved.symbol->address == 0x33u);
    assert(ios27_exports_resolve(graph, 3u, 1u, "_original",
                                &resolved, err, sizeof(err)) == 0);
    assert(resolved.library_index == 2u);
    assert(ios27_exports_resolve(graph, 3u, 0u, "_not_here",
                                &resolved, err, sizeof(err)) == 1);
    assert(resolved.symbol == NULL && err[0] == '\0');

    /* Cycles are rejected across the entire graph, not just visited symbols. */
    graph[2].dependencies = dep_c;
    graph[2].dependency_count = 1u;
    assert(ios27_exports_resolve(graph, 3u, 0u, "_alpha",
                                &resolved, err, sizeof(err)) == -1);
    assert(resolved.symbol == NULL);
    graph[2].dependency_count = 0u;
    graph[2].dependencies = NULL;

    dep_b[0] = 3u; /* invalid dependency index */
    assert(ios27_exports_resolve(graph, 3u, 0u, "_alpha",
                                &resolved, err, sizeof(err)) == -1);
    dep_b[0] = 2u;
    dep_a[0] = 0u; /* direct self cycle */
    assert(ios27_exports_resolve(graph, 3u, 0u, "_beta",
                                &resolved, err, sizeof(err)) == -1);
    dep_a[0] = 1u;

    {   /* Structurally valid DAG, but reexport ordinal absent. */
        const ios27_export *exported = ios27_exports_find(&a, "_beta");
        assert(exported != NULL);
        a.entries[1].ordinal = 2u;
        assert(ios27_exports_resolve(graph, 3u, 0u, "_beta",
                                    &resolved, err, sizeof(err)) == -1);
        assert(resolved.symbol == NULL);
        a.entries[1].ordinal = 1u;
    }

    /* A zero-length imported name means the current symbol name. */
    one_export(image, "_beta", 0u, 0x55u, NULL);
    assert(ios27_exports_parse(image, sizeof(image), &b, err, sizeof(err)) == 0);
    a.entries[1].imported_name[0] = '\0';
    assert(ios27_exports_resolve(graph, 3u, 0u, "_beta",
                                &resolved, err, sizeof(err)) == 0);
    assert(resolved.library_index == 1u && resolved.symbol->address == 0x55u);

    assert(ios27_exports_resolve(graph, 3u, 0u, "_beta",
                                NULL, err, sizeof(err)) == -1);
    assert(ios27_exports_resolve(graph, IOS27_EXPORT_MAX_LIBRARIES + 1u, 0u,
                                "_beta", &resolved, err, sizeof(err)) == -1);
    assert(ios27_exports_resolve(graph, 3u, 3u, "_beta",
                                &resolved, err, sizeof(err)) == -1);
}

int main(void) {
    uint8_t b[FILE_BYTES];
    ios27_exports table;
    const ios27_export *sym;
    char err[128];

    fixture(b, 0x80000033u);
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == 0);
    assert(table.has_trie == 1u && table.count == 2u);
    sym = ios27_exports_find(&table, "_alpha");
    assert(sym != NULL && sym->flags == 0u && sym->address == 0x1234u);
    sym = ios27_exports_find(&table, "_beta");
    assert(sym != NULL && sym->flags == IOS27_EXPORT_REEXPORT);
    assert(sym->ordinal == 1u && strcmp(sym->imported_name, "_original") == 0);
    assert(ios27_exports_find(&table, "_missing") == NULL);
    assert(ios27_exports_find(NULL, "_alpha") == NULL);

    fixture(b, 0x80000022u); /* legacy LC_DYLD_INFO_ONLY */
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == 0);
    assert(table.count == 2u && ios27_exports_find(&table, "_alpha") != NULL);

    fixture(b, 0x80000033u);
    put32(b + 44u, UINT32_MAX);
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) != 0);

    fixture(b, 0x80000033u);
    b[DATA_OFF + 9u] = 0x00u; /* self-referential trie edge */
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) != 0);

    fixture(b, 0x80000033u);
    b[DATA_OFF + 9u] = 0xffu; /* invalid / truncated edge offset */
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) != 0);

    fixture(b, 0x80000033u);
    b[DATA_OFF + 17u] = 0xffu; /* corrupt terminal uleb */
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) != 0);

    fixture(b, 0x80000033u);
    b[DATA_OFF + 18u] = 0x80u; /* invalid flags uleb */
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) != 0);

    /* Stub and resolver export: two image-relative ULEB addresses. */
    fixture(b, 0x80000033u);
    memset(b + DATA_OFF, 0, sizeof(b) - DATA_OFF);
    {
        static const uint8_t stub_trie[] = {
            0x00, 0x01, '_', 's', 't', 'u', 'b', 0x00, 0x09,
            0x03, 0x10, 0x11, 0x22, 0x00
        };
        memcpy(b + DATA_OFF, stub_trie, sizeof(stub_trie));
        put32(b + 44u, (uint32_t)sizeof(stub_trie));
    }
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == 0);
    sym = ios27_exports_find(&table, "_stub");
    assert(sym != NULL && sym->address == 0x11u && sym->resolver == 0x22u);
    assert((sym->flags & IOS27_EXPORT_STUB_RESOLVER) != 0u);

    /* Oversized ULEB and unrecognized flags are both rejected. */
    b[DATA_OFF + 10u] = 0xffu;
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) != 0);
    b[DATA_OFF + 10u] = 0x20u;
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) != 0);

    fixture(b, 0x80000033u);
    put32(b + 44u, 0u);
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == 0);
    assert(table.count == 0u && !table.has_trie);

    /* Modern flag metadata and strict malformed terminal rejection. */
    one_export(b, "_weak", IOS27_EXPORT_WEAK_DEFINITION, 0x21u, NULL);
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == 0);
    sym = ios27_exports_find(&table, "_weak");
    assert(sym && sym->flags == IOS27_EXPORT_WEAK_DEFINITION);
    one_export(b, "_abs", IOS27_EXPORT_KIND_ABSOLUTE, 0x25u, NULL);
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == 0);
    sym = ios27_exports_find(&table, "_abs");
    assert(sym && (sym->flags & IOS27_EXPORT_KIND_MASK) ==
           IOS27_EXPORT_KIND_ABSOLUTE);
    one_export(b, "_tls", IOS27_EXPORT_KIND_THREAD_LOCAL, 0x10u, NULL);
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == 0);
    one_export(b, "_bad", 3u, 1u, NULL); /* reserved kind */
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == -1);
    assert(table.count == 0u);
    one_export(b, "_bad", IOS27_EXPORT_REEXPORT, 0u, "_target");
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == -1);
    one_export(b, "_bad", IOS27_EXPORT_KIND_ABSOLUTE |
               IOS27_EXPORT_STUB_RESOLVER, 1u, NULL);
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == -1);
    one_export(b, "_bad", 0u, 1u, NULL);
    {   /* Nonminimal flags ULEB is rejected, not silently accepted. */
        size_t terminal = 4u + strlen("_bad");
        b[DATA_OFF + terminal] = 3u;
        b[DATA_OFF + terminal + 1u] = 0x80u;
        b[DATA_OFF + terminal + 2u] = 0u;
        b[DATA_OFF + terminal + 3u] = 1u;
        b[DATA_OFF + terminal + 4u] = 0u;
        put32(b + 44u, (uint32_t)(terminal + 5u));
        assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == -1);
    }
    test_dependency_resolution();

    /* An exports table is data, not a runnable Mach-O or an imported framework. */
    puts("dyld exports tests passed");
    return 0;
}
