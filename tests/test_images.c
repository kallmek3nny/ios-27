#include "ios27/images.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IMAGE_SIZE 24576u
#define CODE_OFF 512u
#define DATA_OFF 4096u
#define FIXUPS_OFF 288u
#define EXPORTS_OFF 384u
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
static void header(uint8_t *p, uint32_t commands, uint32_t bytes,
                   uint32_t type) {
    le32(p, 0xfeedfacfu);
    le32(p + 4u, 0x0100000cu);
    le32(p + 12u, type);
    le32(p + 16u, commands);
    le32(p + 20u, bytes);
}
static void segment(uint8_t *p, const char *name, uint64_t vmaddr,
                    uint64_t size, uint64_t fileoff,
                    uint64_t filesize, uint32_t prot) {
    le32(p, 0x19u);
    le32(p + 4u, 72u);
    memcpy(p + 8u, name, strlen(name));
    le64(p + 24u, vmaddr);
    le64(p + 32u, size);
    le64(p + 40u, fileoff);
    le64(p + 48u, filesize);
    le32(p + 56u, prot);
    le32(p + 60u, prot);
}
static void main_command(uint8_t *p) {
    le32(p, 0x80000028u);
    le32(p + 4u, 24u);
    le64(p + 8u, CODE_OFF);
}
static void dylib(uint8_t *p, const char *name) {
    le32(p, 0x0cu);
    le32(p + 4u, 40u);
    le32(p + 8u, 24u);
    memcpy(p + 24u, name, strlen(name) + 1u);
}
static void export_trie(uint8_t *buffer, const char *name) {
    size_t len = strlen(name), offset = 4u + len;
    assert(len <= 20u);
    buffer[0] = 0u;
    buffer[1] = 1u;
    memcpy(buffer + 2u, name, len + 1u);
    buffer[2u + len + 1u] = (uint8_t)offset;
    buffer[offset] = 3u; /* flags 0, address 512 (ULEB 0x80 0x04) */
    buffer[offset + 1u] = 0u;
    buffer[offset + 2u] = 0x80u;
    buffer[offset + 3u] = 0x04u;
    buffer[offset + 4u] = 0u;
}
static size_t trie_len(const char *name) {
    return strlen(name) + 9u;
}
static void provider(uint8_t file[IMAGE_SIZE], const char *depends_on) {
    memset(file, 0, IMAGE_SIZE);
    header(file, depends_on ? 3u : 2u, depends_on ? 128u : 88u, 6u);
    segment(file + 32u, "__TEXT", UINT64_C(0x100000000), 4096u,
            0u, 1024u, 5u);
    le32(file + 104u, 0x80000033u);
    le32(file + 108u, 16u);
    le32(file + 112u, EXPORTS_OFF);
    le32(file + 116u, (uint32_t)trie_len("_answer"));
    if (depends_on) dylib(file + 120u, depends_on);
    export_trie(file + EXPORTS_OFF, "_answer");
    le32(file + CODE_OFF, 0x52800540u); /* mov w0,#42 */
    le32(file + CODE_OFF + 4u, 0xd65f03c0u); /* ret */
}
static void consumer(uint8_t file[IMAGE_SIZE], uint16_t page_size) {
    uint8_t *p;
    memset(file, 0, IMAGE_SIZE);
    header(file, 6u, 240u, 2u);
    segment(file + 32u, "__TEXT", UINT64_C(0x100000000), 4096u,
            0u, 1024u, 5u);
    segment(file + 104u, "__DATA", UINT64_C(0x100001000),
            page_size == 16384u ? 16384u : 4096u,
            DATA_OFF, 4096u, 3u);
    le32(file + 176u, 0x80000034u); /* LC_DYLD_CHAINED_FIXUPS */
    le32(file + 180u, 16u);
    le32(file + 184u, FIXUPS_OFF);
    le32(file + 188u, 80u);
    dylib(file + 192u, "provider.dylib");
    le32(file + 232u, 0x80000033u); /* LC_DYLD_EXPORTS_TRIE */
    le32(file + 236u, 16u);
    le32(file + 240u, EXPORTS_OFF);
    le32(file + 244u, (uint32_t)trie_len("_call"));
    export_trie(file + EXPORTS_OFF, "_call");
    main_command(file + 248u);
    p = file + FIXUPS_OFF;
    le32(p + 4u, 28u); /* starts offset */
    le32(p + 8u, 64u); /* imports offset */
    le32(p + 12u, 68u); /* symbols offset */
    le32(p + 16u, 1u); /* one import */
    le32(p + 20u, 1u); /* basic import format */
    le32(p + 28u, 2u); /* two segments */
    le32(p + 36u, 12u); /* __DATA starts */
    le32(p + 40u, 24u); /* segment starts size */
    le16(p + 44u, page_size);
    le16(p + 46u, 6u); /* 64_OFFSET */
    le64(p + 48u, 4096u); /* segment relative vm offset */
    le16(p + 60u, 1u); /* page count */
    le16(p + 62u, 0u); /* single chain at page start */
    le32(p + 64u, 1u); /* import index 0 -> dylib ordinal 1 */
    memcpy(p + 68u, "_answer", 8u);
    le64(file + DATA_OFF, PTR_BIND);
    le64(file + DATA_OFF + 8u, PTR_REBASE);
    le32(file + CODE_OFF, 0xb0000010u); /* ADRP x16,+1page */
    le32(file + CODE_OFF + 4u, 0xf9400210u); /* LDR x16,[x16] */
    le32(file + CODE_OFF + 8u, 0xd61f0200u); /* BR x16 */
}
static void classic_consumer(uint8_t file[IMAGE_SIZE]) {
    static const uint8_t binding[] = {
        0x11u, 0x40u, '_','a','n','s','w','e','r',0u,
        0x51u, 0x71u, 0x00u, 0x90u, 0x00u
    };
    memset(file, 0, IMAGE_SIZE);
    header(file, 6u, 272u, 2u);
    segment(file + 32u, "__TEXT", UINT64_C(0x100000000), 4096u,
            0u, 1024u, 5u);
    segment(file + 104u, "__DATA", UINT64_C(0x100001000), 4096u,
            DATA_OFF, 4096u, 3u);
    le32(file + 176u, 0x80000022u); /* LC_DYLD_INFO_ONLY */
    le32(file + 180u, 48u);
    le32(file + 192u, 352u); /* bind_off */
    le32(file + 196u, (uint32_t)sizeof(binding));
    dylib(file + 224u, "provider.dylib");
    le32(file + 264u, 0x80000033u);
    le32(file + 268u, 16u);
    le32(file + 272u, EXPORTS_OFF);
    le32(file + 276u, (uint32_t)trie_len("_call"));
    export_trie(file + EXPORTS_OFF, "_call");
    main_command(file + 280u);
    memcpy(file + 352u, binding, sizeof(binding));
    le32(file + CODE_OFF, 0xb0000010u);
    le32(file + CODE_OFF + 4u, 0xf9400210u);
    le32(file + CODE_OFF + 8u, 0xd61f0200u);
}
static void expect_failure(const ios27_image_spec *specs, size_t count) {
    ios27_images *images = NULL;
    char error[128] = {0};
    assert(ios27_images_load(specs, count, &images, error, sizeof(error)) != 0);
    assert(images == NULL);
    assert(error[0] != '\0');
}
static void check_graph(uint8_t provider_bytes[IMAGE_SIZE],
                        uint8_t consumer_bytes[IMAGE_SIZE],
                        int execute) {
    const char *deps[] = {"provider.dylib"};
    ios27_image_spec specs[] = {
        {"consumer.dylib", consumer_bytes, IMAGE_SIZE, deps, 1u},
        {"provider.dylib", provider_bytes, IMAGE_SIZE, NULL, 0u}
    };
    ios27_images *images = NULL;
    uintptr_t provider_fn, consumer_fn, entry_fn;
    ios27_image_state state;
    char error[128];
    assert(ios27_images_load(specs, 2u, &images, error, sizeof(error)) == 0);
    assert(images != NULL);
    assert(ios27_images_status(images, "provider.dylib", &state) == 0);
    assert(state == IOS27_IMAGE_READY);
    assert(ios27_images_status(images, "consumer.dylib", &state) == 0);
    assert(state == IOS27_IMAGE_READY);
    assert(ios27_images_status(images, "unknown.dylib", &state) != 0);
    assert(ios27_images_symbol(images, "provider.dylib",
                               "_answer", &provider_fn) == 0);
    assert(ios27_images_symbol(images, "consumer.dylib",
                               "_call", &consumer_fn) == 0);
    assert(ios27_images_entrypoint(images, "consumer.dylib", &entry_fn) == 0);
    assert(entry_fn == consumer_fn);
    assert(ios27_images_entrypoint(images, "provider.dylib", &entry_fn) != 0);
    assert(ios27_images_symbol(images, "provider.dylib", "_missing", &consumer_fn) != 0);
    /* A bound pointer now targets the actual provider VM, not a mock value. */
    assert(rd64((const uint8_t *)(consumer_fn - CODE_OFF + 4096u)) ==
           (uint64_t)provider_fn);
    if (rd64(consumer_bytes + DATA_OFF + 8u) == PTR_REBASE)
        assert(rd64((const uint8_t *)(consumer_fn - CODE_OFF + 4104u)) ==
               (uint64_t)(consumer_fn - CODE_OFF + 0x70u));
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    if (execute) {
        int (*caller)(void);
        memcpy(&caller, &entry_fn, sizeof(caller));
        assert(caller() == 42);
    }
#else
    (void)execute;
#endif
    ios27_images_unload(images);
}
int main(void) {
    static uint8_t provider_bytes[IMAGE_SIZE];
    static uint8_t consumer_bytes[IMAGE_SIZE];
    static uint8_t provider_original[IMAGE_SIZE];
    static uint8_t consumer_original[IMAGE_SIZE];
    const char *deps[] = {"provider.dylib"};
    ios27_image_spec specs[] = {
        {"consumer.dylib", consumer_bytes, IMAGE_SIZE, deps, 1u},
        {"provider.dylib", provider_bytes, IMAGE_SIZE, NULL, 0u}
    };

    provider(provider_bytes, NULL);
    consumer(consumer_bytes, 4096u);
    memcpy(provider_original, provider_bytes, IMAGE_SIZE);
    memcpy(consumer_original, consumer_bytes, IMAGE_SIZE);
    check_graph(provider_bytes, consumer_bytes, 1);
    assert(memcmp(provider_original, provider_bytes, IMAGE_SIZE) == 0);
    assert(memcmp(consumer_original, consumer_bytes, IMAGE_SIZE) == 0);

    consumer(consumer_bytes, 16384u);
    check_graph(provider_bytes, consumer_bytes, 1);
    classic_consumer(consumer_bytes);
    check_graph(provider_bytes, consumer_bytes, 1);

    specs[1].name = "consumer.dylib";
    expect_failure(specs, 2u); /* duplicate image */
    specs[1].name = "provider.dylib";
    deps[0] = "missing.dylib";
    expect_failure(specs, 2u);
    deps[0] = "consumer.dylib";
    expect_failure(specs, 2u); /* self dependency */
    deps[0] = "provider.dylib";

    provider(provider_bytes, "consumer.dylib");
    {
        const char *pdeps[] = {"consumer.dylib"};
        specs[1].dependencies = pdeps;
        specs[1].dependency_count = 1u;
        expect_failure(specs, 2u); /* cycle */
    }
    specs[1].dependencies = NULL;
    specs[1].dependency_count = 0u;
    provider(provider_bytes, NULL);

    {
        const char *dupdeps[] = {"provider.dylib", "provider.dylib"};
        specs[0].dependencies = dupdeps;
        specs[0].dependency_count = 2u;
        expect_failure(specs, 2u); /* duplicate dependencies */
    }
    specs[0].dependencies = deps;
    specs[0].dependency_count = 1u;
    specs[0].name = "../consumer";
    expect_failure(specs, 2u);
    specs[0].name = "consumer.dylib";

    /* Standard Mach-O __PAGEZERO must not consume host memory or shift code. */
    provider(provider_bytes, NULL);
    header(provider_bytes, 3u, 160u, 6u);
    segment(provider_bytes + 120u, "__PAGEZERO", 0u,
            UINT64_C(0x100000000), 0u, 0u, 0u);
    classic_consumer(consumer_bytes);
    check_graph(provider_bytes, consumer_bytes, 1);
    provider(provider_bytes, NULL);

    classic_consumer(consumer_bytes);
    memcpy(consumer_bytes + 224u + 24u, "unknown.dylib", 14u);
    expect_failure(specs, 2u); /* load command inconsistent with graph */

    classic_consumer(consumer_bytes);
    le64(consumer_bytes + 280u + 8u, DATA_OFF); /* LC_MAIN in RW data */
    expect_failure(specs, 2u);
    classic_consumer(consumer_bytes);
    le64(consumer_bytes + 280u + 8u, 256u); /* LC_MAIN within commands */
    expect_failure(specs, 2u);
    classic_consumer(consumer_bytes);
    le64(consumer_bytes + 104u + 40u, CODE_OFF); /* overlapping file ranges */
    expect_failure(specs, 2u);
    classic_consumer(consumer_bytes);
    le32(consumer_bytes + 104u + 56u, 1u); /* init prot exceeds max */
    expect_failure(specs, 2u);

    classic_consumer(consumer_bytes);
    le32(consumer_bytes + 104u + 60u, 7u); /* RWX data segment */
    expect_failure(specs, 2u);
    classic_consumer(consumer_bytes);
    le32(consumer_bytes + 104u + 56u, 1u); /* maxprot lacks WRITE */
    expect_failure(specs, 2u);
    classic_consumer(consumer_bytes);
    le32(consumer_bytes + 104u + 56u, 11u); /* unknown maxprot bits */
    expect_failure(specs, 2u);
    classic_consumer(consumer_bytes);
    le64(consumer_bytes + 104u + 24u, UINT64_C(0x100000000));
    expect_failure(specs, 2u); /* overlapping VM ranges */
    classic_consumer(consumer_bytes);
    le64(consumer_bytes + 104u + 24u, UINT64_C(0x100001001));
    expect_failure(specs, 2u); /* unaligned VM addresses */
    classic_consumer(consumer_bytes);
    le32(consumer_bytes + 192u, 65535u);
    expect_failure(specs, 2u); /* unsupported/malformed classic binds */

    consumer(consumer_bytes, 4096u);
    memcpy(consumer_bytes + FIXUPS_OFF + 68u, "_absent", 8u);
    expect_failure(specs, 2u); /* missing symbol aborts all */
    consumer(consumer_bytes, 4096u);
    le32(consumer_bytes + FIXUPS_OFF + 46u, 1u);
    expect_failure(specs, 2u); /* arm64e PAC pointer format denied */
    consumer(consumer_bytes, 4096u);
    le64(consumer_bytes + DATA_OFF, PTR_BIND | 1u);
    expect_failure(specs, 2u); /* invalid import index */

    /* PAGEZERO may not swallow a loadable VM page. */
    provider(provider_bytes, NULL);
    header(provider_bytes, 3u, 160u, 6u);
    segment(provider_bytes + 120u, "__PAGEZERO", 0u,
            UINT64_C(0x100000001), 0u, 0u, 0u);
    classic_consumer(consumer_bytes);
    expect_failure(specs, 2u);
    provider(provider_bytes, NULL);

    /* An earlier rejected graph may not poison a subsequent fresh load. */
    classic_consumer(consumer_bytes);
    check_graph(provider_bytes, consumer_bytes, 1);
    puts("native multi-image entry, W^X and rollback tests passed");
    return 0;
}
