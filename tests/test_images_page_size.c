#define _GNU_SOURCE
#include "images_internal.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Original synthetic metadata only: no guest instructions are executed. An
 * injected 16 KiB page on a 4 KiB kernel is arithmetic/protection coverage,
 * not validation of a real 16 KiB host, guest stack or guest arena. */
#define IMAGE_SIZE 2048u
#define CODE_OFF 512u
#define FIXUPS_OFF 640u
#define EXPORTS_OFF 768u
#define DATA_OFF 1024u
#define LINKEDIT_OFF 1536u
#define BASE UINT64_C(0x100000000)
#define PTR_BIND (UINT64_C(0x8000000000000000) | (UINT64_C(2) << 51u))
#define PTR_REBASE UINT64_C(0x70)

static void le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}
static void le32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (i * 8u));
}
static void le64(uint8_t *p, uint64_t value) {
    le32(p, (uint32_t)value);
    le32(p + 4u, (uint32_t)(value >> 32u));
}
static uint64_t rd64(const uint8_t *p) {
    uint64_t value = 0u;
    for (unsigned i = 0u; i < 8u; ++i)
        value |= (uint64_t)p[i] << (i * 8u);
    return value;
}
static void header(uint8_t *p, uint32_t commands, uint32_t bytes, uint32_t type) {
    le32(p, 0xfeedfacfu);
    le32(p + 4u, 0x0100000cu);
    le32(p + 12u, type);
    le32(p + 16u, commands);
    le32(p + 20u, bytes);
}
static void segment(uint8_t *p, const char *name, uint64_t address,
                    uint64_t size, uint64_t fileoff, uint64_t filesize,
                    uint32_t prot) {
    le32(p, 0x19u);
    le32(p + 4u, 72u);
    memcpy(p + 8u, name, strlen(name));
    le64(p + 24u, address);
    le64(p + 32u, size);
    le64(p + 40u, fileoff);
    le64(p + 48u, filesize);
    le32(p + 56u, prot);
    le32(p + 60u, prot);
}
static void export_command(uint8_t *file, size_t command, const char *name) {
    uint8_t *trie = file + EXPORTS_OFF;
    size_t length = strlen(name), terminal = length + 4u;
    le32(file + command, 0x80000033u);
    le32(file + command + 4u, 16u);
    le32(file + command + 8u, EXPORTS_OFF);
    le32(file + command + 12u, (uint32_t)(length + 9u));
    trie[1] = 1u;
    memcpy(trie + 2u, name, length + 1u);
    trie[length + 3u] = (uint8_t)terminal;
    trie[terminal] = 3u;
    trie[terminal + 2u] = 0x80u;
    trie[terminal + 3u] = 0x04u; /* export offset 512 */
}
static void fixture(uint8_t provider[IMAGE_SIZE], uint8_t consumer[IMAGE_SIZE],
                    size_t page, size_t link_size, const char *dependency) {
    uint8_t *p;
    memset(provider, 0, IMAGE_SIZE);
    memset(consumer, 0, IMAGE_SIZE);
    header(provider, 3u, 160u, 6u);
    segment(provider + 32u, "__TEXT", BASE, 1024u, 0u, 1024u, 5u);
    export_command(provider, 104u, "_answer");
    segment(provider + 120u, "__PAGEZERO", 0u, BASE, 0u, 0u, 0u);
    provider[CODE_OFF] = 0x5au;

    header(consumer, 7u, 312u, 2u);
    segment(consumer + 32u, "__TEXT", BASE, 1024u, 0u, 1024u, 5u);
    /* One inaccessible page, two RW pages (the second mostly zero fill),
     * then a partial read-only final segment. File offsets are independent
     * of the VM spacing because the graph copies private staged bytes. */
    segment(consumer + 104u, "__DATA", BASE + 2u * page, page + 1u,
            DATA_OFF, 16u, 3u);
    segment(consumer + 176u, "__LINKEDIT", BASE + 4u * page, link_size,
            LINKEDIT_OFF, 17u, 1u);
    le32(consumer + 248u, 0x80000034u);
    le32(consumer + 252u, 16u);
    le32(consumer + 256u, FIXUPS_OFF);
    le32(consumer + 260u, 80u);
    le32(consumer + 264u, 0x0cu);
    le32(consumer + 268u, 40u);
    le32(consumer + 272u, 24u);
    assert(strlen(dependency) < 16u);
    memcpy(consumer + 288u, dependency, strlen(dependency) + 1u);
    export_command(consumer, 304u, "_call");
    le32(consumer + 320u, 0x80000028u);
    le32(consumer + 324u, 24u);
    le64(consumer + 328u, CODE_OFF);
    consumer[CODE_OFF] = 0xa5u;
    p = consumer + FIXUPS_OFF;
    le32(p + 4u, 28u);
    le32(p + 8u, 68u);
    le32(p + 12u, 72u);
    le32(p + 16u, 1u);
    le32(p + 20u, 1u);
    le32(p + 28u, 3u);
    le32(p + 36u, 16u); /* __DATA starts, relative to starts-in-image */
    le32(p + 44u, 24u);
    le16(p + 48u, page >= 16384u ? 16384u : 4096u);
    le16(p + 50u, 6u); /* DYLD_CHAINED_PTR_64_OFFSET */
    le64(p + 52u, 2u * page);
    le16(p + 64u, 1u);
    le32(p + 68u, 1u); /* dylib ordinal 1 */
    memcpy(p + 72u, "_answer", 8u);
    le64(consumer + DATA_OFF, PTR_BIND);
    le64(consumer + DATA_OFF + 8u, PTR_REBASE);
    memset(consumer + LINKEDIT_OFF, 0x3c, 17u);
}

/* Inspect permissions without faulting, installing signal handlers or
 * executing a synthetic address. Check each real page inside a simulated
 * granule so a 4 KiB mprotect cannot masquerade as 16 KiB coverage. */
static void permissions(uintptr_t address, const char *expected) {
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[512], observed[5];
    uintptr_t start, end;
    int found = 0;
    assert(maps);
    while (fgets(line, sizeof(line), maps)) {
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4s",
                   &start, &end, observed) == 3 &&
            address >= start && address < end) {
            if (strcmp(observed, expected))
                fprintf(stderr, "page permissions: got %s, expected %s\n",
                        observed, expected);
            assert(!strcmp(observed, expected));
            found = 1;
            break;
        }
    }
    assert(!fclose(maps));
    assert(found);
}
static void unmapped(uintptr_t base, size_t span, size_t host_page) {
    unsigned char residency;
    for (size_t offset = 0u; offset < span; offset += host_page) {
        errno = 0;
        assert(mincore((void *)(base + offset), host_page, &residency) == -1);
        assert(errno == ENOMEM);
    }
}
static void check_graph(size_t requested, size_t host_page, size_t link_size,
                        int paths_mode, int public_api) {
    uint8_t provider[IMAGE_SIZE], consumer[IMAGE_SIZE];
    uint8_t provider_before[IMAGE_SIZE], consumer_before[IMAGE_SIZE];
    const char *deps[] = {paths_mode ? "@loader_path/P" : "provider.dylib"};
    const char *paths[] = {"/App/C", "/App/P"};
    ios27_image_spec specs[] = {
        {"consumer", consumer, IMAGE_SIZE, deps, 1u},
        {"provider.dylib", provider, IMAGE_SIZE, NULL, 0u}
    };
    size_t page = requested ? requested : host_page;
    size_t rounded_link = (link_size + page - 1u) & ~(page - 1u);
    ios27_images *images = NULL;
    ios27_image_state state;
    uintptr_t caller, answer, entry, base, provider_base;
    const char *name;
    char error[160] = {0};
    int rc;
    fixture(provider, consumer, page, link_size, deps[0]);
    memcpy(provider_before, provider, IMAGE_SIZE);
    memcpy(consumer_before, consumer, IMAGE_SIZE);
    if (public_api)
        rc = ios27_images_load(specs, 2u, &images, error, sizeof(error));
    else
        rc = ios27_images_load_with_page_size(specs, 2u,
                    paths_mode ? paths : NULL, paths_mode ? paths[0] : NULL,
                    requested, &images, error, sizeof(error));
    if (rc) fprintf(stderr, "graph page %zu: %s\n", requested, error);
    assert(!rc && images && !error[0]);
    assert(!ios27_images_status(images, "consumer", &state));
    assert(state == IOS27_IMAGE_READY);
    assert(!ios27_images_symbol(images, "consumer", "_call", &caller));
    assert(!ios27_images_symbol(images, "provider.dylib", "_answer", &answer));
    assert(!ios27_images_entrypoint(images, "consumer", &entry) && entry == caller);
    base = caller - CODE_OFF;
    provider_base = answer - CODE_OFF;
    assert(base % page == 0u && provider_base % page == 0u);
    assert(*(const uint8_t *)caller == 0xa5u);
    assert(*(const uint8_t *)answer == 0x5au);
    assert(rd64((const uint8_t *)(base + 2u * page)) == answer);
    assert(rd64((const uint8_t *)(base + 2u * page + 8u)) == base + PTR_REBASE);
    for (size_t i = 16u; i < 2u * page; ++i)
        assert(*(const uint8_t *)(base + 2u * page + i) == 0u);
    for (size_t i = 0u; i < rounded_link; ++i)
        assert(*(const uint8_t *)(base + 4u * page + i) == (i < 17u ? 0x3cu : 0u));
    *(uint8_t *)(base + 2u * page + 16u) = 0x7eu;
    for (size_t i = 0u; i < page; i += host_page) {
        permissions(base + i, "r-xp");
        permissions(provider_base + i, "r-xp");
        permissions(base + page + i, "---p");
    }
    for (size_t i = 0u; i < 2u * page; i += host_page)
        permissions(base + 2u * page + i, "rw-p");
    for (size_t i = 0u; i < rounded_link; i += host_page)
        permissions(base + 4u * page + i, "r--p");
    assert(!ios27_images_initialization_image(images, 0u, &name));
    assert(!strcmp(name, "provider.dylib"));
    assert(!ios27_images_finalization_image(images, 0u, &name));
    assert(!strcmp(name, "consumer"));
    assert(!memcmp(provider, provider_before, IMAGE_SIZE));
    assert(!memcmp(consumer, consumer_before, IMAGE_SIZE));
    ios27_images_unload(images);
    unmapped(base, 4u * page + rounded_link, host_page);
    unmapped(provider_base, page, host_page);
}
static void rejected(const ios27_image_spec *specs, size_t requested) {
    uint8_t before[2][IMAGE_SIZE];
    ios27_images *images = (ios27_images *)(uintptr_t)1u;
    char error[160] = {0};
    for (size_t i = 0u; i < 2u; ++i) memcpy(before[i], specs[i].file, IMAGE_SIZE);
    assert(ios27_images_load_with_page_size(specs, 2u, NULL, NULL, requested,
                                            &images, error, sizeof(error)) == -1);
    assert(images == NULL && error[0]);
    for (size_t i = 0u; i < 2u; ++i)
        assert(!memcmp(before[i], specs[i].file, IMAGE_SIZE));
}
static void rejection_checks(size_t page, size_t host_page) {
    uint8_t provider[IMAGE_SIZE], consumer[IMAGE_SIZE];
    const char *deps[] = {"provider.dylib"};
    ios27_image_spec specs[] = {
        {"consumer", consumer, IMAGE_SIZE, deps, 1u},
        {"provider.dylib", provider, IMAGE_SIZE, NULL, 0u}
    };
    const size_t bad_pages[] = {1u, 2048u, 4095u, 4097u, 12288u, 65537u,
                                131072u, SIZE_MAX};
    fixture(provider, consumer, page, 17u, deps[0]);
    for (size_t i = 0u; i < sizeof(bad_pages) / sizeof(bad_pages[0]); ++i)
        rejected(specs, bad_pages[i]);
    for (size_t smaller = 4096u; smaller < host_page; smaller *= 2u)
        rejected(specs, smaller);
    /* Disjoint byte ranges can still share a simulated protection granule. */
    le64(consumer + 104u + 24u, BASE + page / 2u);
    rejected(specs, page);
    fixture(provider, consumer, page, 17u, deps[0]);
    le64(consumer + 32u + 32u, 2u * page + 1u);
    rejected(specs, page); /* text overlaps data by one byte */
    fixture(provider, consumer, page, 17u, deps[0]);
    le64(consumer + 176u + 24u, UINT64_MAX - (page - 1u));
    le64(consumer + 176u + 32u, page);
    rejected(specs, page); /* VM end wraps */
    fixture(provider, consumer, page, 17u, deps[0]);
    le64(consumer + 176u + 24u, BASE + 16u * 1024u * 1024u);
    rejected(specs, page); /* rounded span exceeds the bounded image */
    fixture(provider, consumer, page, 17u, deps[0]);
    le32(consumer + 104u + 56u, 7u);
    le32(consumer + 104u + 60u, 7u);
    rejected(specs, page); /* W+X remains forbidden */
    fixture(provider, consumer, page, 17u, deps[0]);
    memcpy(consumer + FIXUPS_OFF + 72u, "_absent", 8u);
    rejected(specs, page); /* failure after all graph mappings were reserved */
    check_graph(page, host_page, 17u, 0, 0); /* fresh load after rollback */
}

int main(void) {
    const size_t pages[] = {4096u, 8192u, 16384u, 32768u, 65536u};
    long measured = sysconf(_SC_PAGESIZE);
    size_t host_page, exercised = 0u;
    assert(measured >= 4096 && measured <= 65536);
    host_page = (size_t)measured;
    assert((host_page & (host_page - 1u)) == 0u);
    check_graph(0u, host_page, 17u, 0, 1);
    check_graph(0u, host_page, 17u, 0, 0);
    check_graph(0u, host_page, 17u, 1, 0);
    for (size_t i = 0u; i < sizeof(pages) / sizeof(pages[0]); ++i) {
        size_t page = pages[i];
        if (page < host_page) continue;
        ++exercised;
        check_graph(page, host_page, 17u, 0, 0);
        check_graph(page, host_page, page, 1, 0);
        check_graph(page, host_page, page + 1u, 0, 0);
        rejection_checks(page, host_page);
    }
    printf("image graph page seam: %zu sizes, host %zu; bind/rebase, W^X, "
           "holes, zero fill, rollback and unmap passed; simulated sizes "
           "are not hardware validation\n", exercised, host_page);
    return 0;
}
