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

    fixture(b, 0x80000033u);
    put32(b + 44u, 0u);
    assert(ios27_exports_parse(b, sizeof(b), &table, err, sizeof(err)) == 0);
    assert(table.count == 0u && !table.has_trie);

    /* An exports table is data, not a runnable Mach-O or an imported framework. */
    puts("dyld exports tests passed");
    return 0;
}
