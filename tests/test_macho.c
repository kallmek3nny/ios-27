#include "ios27/macho.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (i * 8u));
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

/* Minimal synthetic Mach-O containing ARM64 "mov w0,#42; ret". */
static void fixture(uint8_t b[264]) {
    memset(b, 0, 264);
    put32(b, 0xfeedfacf);
    put32(b + 4, 0x0100000c);
    put32(b + 12, 2);
    put32(b + 16, 2);
    put32(b + 20, 96);
    put32(b + 32, 0x19);
    put32(b + 36, 72);
    memcpy(b + 40, "__TEXT", 6);
    put64(b + 56, 0x100000000ull);
    put64(b + 64, 4096);
    put64(b + 72, 0);
    put64(b + 80, 264);
    put32(b + 88, 5);
    put32(b + 92, 5);
    put32(b + 104, 0x80000028);
    put32(b + 108, 24);
    put64(b + 112, 256);
    put32(b + 256, 0x52800540);
    put32(b + 260, 0xd65f03c0);
}


/* Pure preflight tests run on BOTH CI architectures; only aarch64 executes. */
static void executable_layout_tests(void) {
    uint8_t file[8192];
    ios27_macho_info image;
    ios27_exec_layout layout;
    char err[128];
    memset(file, 0, sizeof(file));
    put32(file, 0xfeedfacfu);
    put32(file + 4u, 0x0100000cu);
    put32(file + 12u, IOS27_MH_EXECUTE);
    put32(file + 16u, 3u);
    put32(file + 20u, 168u);
    put32(file + 32u, 0x19u);
    put32(file + 36u, 72u);
    memcpy(file + 40u, "__TEXT", 6u);
    put64(file + 56u, UINT64_C(0x100000000));
    put64(file + 64u, 4096u);
    put64(file + 72u, 0u);
    put64(file + 80u, 4096u);
    put32(file + 88u, 5u);
    put32(file + 92u, 5u);
    put32(file + 104u, 0x19u);
    put32(file + 108u, 72u);
    memcpy(file + 112u, "__DATA", 6u);
    put64(file + 128u, UINT64_C(0x100001000));
    put64(file + 136u, 4096u);
    put64(file + 144u, 4096u);
    put64(file + 152u, 4096u);
    put32(file + 160u, 3u);
    put32(file + 164u, 3u);
    put32(file + 176u, 0x80000028u);
    put32(file + 180u, 24u);
    put64(file + 184u, 256u);
    put32(file + 256u, 0x52800540u);
    put32(file + 260u, 0xd65f03c0u);

    assert(ios27_macho_parse(file, sizeof(file), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          4096u, &layout, err, sizeof(err)) == 0);
    assert(layout.span == 8192u);
    assert(layout.entry_vmaddr == UINT64_C(0x100000100));
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          16384u, &layout, err, sizeof(err)) != 0);
    assert(layout.span == 0u); /* transactional output on validation failure */
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          8192u, &layout, err, sizeof(err)) != 0);

    /* Move data out of the text's 16k host granule. */
    put64(file + 128u, UINT64_C(0x100004000));
    assert(ios27_macho_parse(file, sizeof(file), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          16384u, &layout, err, sizeof(err)) == 0);
    assert(layout.span == 32768u);
    {
        ios27_macho_info stale = image;
        stale.entryoff += 4u;
        assert(ios27_macho_executable_layout(file, sizeof(file), &stale,
                                              4096u, &layout, err, sizeof(err)) != 0);
        assert(layout.span == 0u);
    }
    put64(file + 184u, 257u); /* AArch64 entry must align to 4 bytes */
    assert(ios27_macho_parse(file, sizeof(file), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          4096u, &layout, err, sizeof(err)) != 0);
    put64(file + 184u, 4096u); /* entry maps outside executable file bytes */
    assert(ios27_macho_parse(file, sizeof(file), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          4096u, &layout, err, sizeof(err)) != 0);
    put64(file + 184u, 256u);
    put32(file + 160u, 7u); /* W+X in maxprot, even with RW initprot */
    assert(ios27_macho_parse(file, sizeof(file), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          4096u, &layout, err, sizeof(err)) != 0);
    put32(file + 160u, 3u);
    put64(file + 128u, UINT64_C(0x100000800)); /* shared host page */
    assert(ios27_macho_parse(file, sizeof(file), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          4096u, &layout, err, sizeof(err)) != 0);
    put64(file + 128u, UINT64_C(0x100004000));
    memcpy(file + 112u, "__PAGEZERO", 10u); /* cannot silently skip RW data */
    assert(ios27_macho_parse(file, sizeof(file), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          16384u, &layout, err, sizeof(err)) != 0);

    /* A malformed __PAGEZERO must not swallow a RW mapping at a nonzero VM. */
    put32(file + 160u, 0u);
    put32(file + 164u, 0u);
    put64(file + 152u, 0u);
    assert(ios27_macho_parse(file, sizeof(file), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          4096u, &layout, err, sizeof(err)) != 0);

    memset(file + 112u, 0, 16u);
    memcpy(file + 112u, "__DATA", 6u);
    put32(file + 160u, 3u);
    put32(file + 164u, 3u);
    put64(file + 152u, 4096u);
    put64(file + 184u, 32u); /* entry points into Mach-O command header */
    assert(ios27_macho_parse(file, sizeof(file), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          4096u, &layout, err, sizeof(err)) != 0);
    put64(file + 184u, 256u);
    put64(file + 144u, 4095u); /* in-file segment offset, but wrong VM phase */
    assert(ios27_macho_parse(file, sizeof(file), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(file, sizeof(file), &image,
                                          4096u, &layout, err, sizeof(err)) != 0);
}

static void executable_extra_rejections(void) {
    uint8_t b[8192] = {0};
    uint8_t small[264];
    ios27_macho_info info;
    ios27_exec_layout layout;
    char err[128];

    fixture(small);
    memcpy(b, small, sizeof(small));
    put64(b + 72u, 4096u); /* __TEXT is not based at file offset zero */
    put64(b + 80u, 1024u);
    put64(b + 112u, 4352u); /* file-relative LC_MAIN */
    memcpy(b + 4352u, small + 256u, 8u);
    assert(ios27_macho_parse(b, sizeof(b), &info, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(b, sizeof(b), &info, 4096u,
                                          &layout, err, sizeof(err)) == 0);
    assert(layout.entry_vmaddr == UINT64_C(0x100000100));
    {
        uint64_t entry = 0u;
        assert(ios27_macho_entry_vm(&info, &entry) == 0);
        assert(entry == layout.entry_vmaddr);
    }

    fixture(small);
    put32(small + 16u, 3u);
    put32(small + 20u, 104u);
    put32(small + 128u, 0x8000007fu);
    put32(small + 132u, 8u);
    assert(ios27_macho_parse(small, sizeof(small), &info,
                             err, sizeof(err)) == 0);
    assert(info.unsupported_required);
    assert(ios27_macho_executable_layout(small, sizeof(small), &info,
                                          4096u, &layout, err, sizeof(err)) != 0);

    fixture(small);
    memcpy(small + 176u, small + 104u, 24u); /* move LC_MAIN */
    memset(small + 104u, 0, 72u);
    put32(small + 16u, 3u);
    put32(small + 20u, 168u);
    put32(small + 104u, 0x19u);
    put32(small + 108u, 72u);
    memcpy(small + 112u, "__PAGEZERO", 10u);
    put64(small + 136u, UINT64_C(0x100000001));
    assert(ios27_macho_parse(small, sizeof(small), &info,
                             err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(small, sizeof(small), &info,
                                          4096u, &layout, err, sizeof(err)) != 0);
    put64(small + 136u, UINT64_C(0x100000000));
    assert(ios27_macho_parse(small, sizeof(small), &info,
                             err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(small, sizeof(small), &info,
                                          16384u, &layout, err, sizeof(err)) == 0);
}


/* This is the actual phase15a Clang-produced, linked MH_EXECUTE from the
 * existing ALL build target, not a synthetic replacement for the executable.
 * CTest runs in the build directory; no new CMake ownership is required. */
static size_t linked_command(const uint8_t *file, size_t size, uint32_t op,
                             const char *name) {
    size_t cursor = 32u;
    assert(size >= 32u && 32u + (size_t)
           ((uint32_t)file[20u] | ((uint32_t)file[21u] << 8u) |
            ((uint32_t)file[22u] << 16u) | ((uint32_t)file[23u] << 24u))
           <= size);
    for (uint32_t i = 0u; i < ((uint32_t)file[16u] |
             ((uint32_t)file[17u] << 8u) |
             ((uint32_t)file[18u] << 16u) |
             ((uint32_t)file[19u] << 24u)); ++i) {
        uint32_t cmd, bytes;
        assert(cursor <= size && size - cursor >= 8u);
        cmd = (uint32_t)file[cursor] |
              ((uint32_t)file[cursor + 1u] << 8u) |
              ((uint32_t)file[cursor + 2u] << 16u) |
              ((uint32_t)file[cursor + 3u] << 24u);
        bytes = (uint32_t)file[cursor + 4u] |
                ((uint32_t)file[cursor + 5u] << 8u) |
                ((uint32_t)file[cursor + 6u] << 16u) |
                ((uint32_t)file[cursor + 7u] << 24u);
        assert(bytes >= 8u && bytes <= size - cursor);
        if (cmd == op && (!name || (bytes >= 72u &&
              strncmp((const char *)file + cursor + 8u, name, 16u) == 0)))
            return cursor;
        cursor += bytes;
    }
    assert(!"required linked Mach-O command missing");
    return 0u;
}

static void linked_rejected(const uint8_t *bytes, size_t size) {
    ios27_macho_info parsed;
    ios27_exec_layout layout;
    char error[160] = {0};
    int result = 12345;
    if (ios27_macho_parse(bytes, size, &parsed, error, sizeof(error)) == 0) {
        assert(ios27_macho_executable_layout(bytes, size, &parsed, 4096u,
                                             &layout, error, sizeof(error)) != 0);
        assert(layout.span == 0u);
    }
    assert(ios27_run_freestanding(bytes, size, &parsed, &result,
                                  error, sizeof(error)) != 0);
    assert(result == 12345);
    assert(error[0] != '\0');
}

static void linked_phase15a_tests(void) {
    FILE *f = fopen("phase15a-executable", "rb");
    uint8_t *linked, *mutated;
    long length;
    size_t size, maincmd, textcmd, fixcmd, dyldcmd, dysymcmd, symcmd;
    ios27_macho_info info;
    ios27_exec_layout layout;
    char error[160];
    int result = -1;
    if (!f) {
        puts("phase15a linked fixture not present in cwd; optional integration skipped");
        return;
    }
    assert(fseek(f, 0L, SEEK_END) == 0);
    length = ftell(f);
    assert(length >= 32L && length <= 2L * 1024L * 1024L);
    assert(fseek(f, 0L, SEEK_SET) == 0);
    size = (size_t)length;
    linked = malloc(size);
    mutated = malloc(size);
    assert(linked && mutated);
    assert(fread(linked, 1u, size, f) == size);
    assert(fclose(f) == 0);
    assert(ios27_macho_parse(linked, size, &info, error, sizeof(error)) == 0);
    assert(info.filetype == IOS27_MH_EXECUTE && info.has_main);
    assert(info.needs_dyld && info.has_fixups && !info.has_dylibs);
    assert(ios27_macho_executable_layout(linked, size, &info, 4096u,
                                         &layout, error, sizeof(error)) == 0);
    assert(layout.entry_vmaddr != 0u && layout.span >= 4096u);
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    assert(ios27_run_freestanding(linked, size, &info, &result,
                                  error, sizeof(error)) == 0);
    assert(result == 43);
#else
    assert(ios27_run_freestanding(linked, size, &info, &result,
                                  error, sizeof(error)) != 0);
    assert(result == -1);
#endif
    maincmd = linked_command(linked, size, 0x80000028u, NULL);
    textcmd = linked_command(linked, size, 0x19u, "__TEXT");
    fixcmd = linked_command(linked, size, 0x80000034u, NULL);
    dyldcmd = linked_command(linked, size, 0x0eu, NULL);
    dysymcmd = linked_command(linked, size, 0x0bu, NULL);
    symcmd = linked_command(linked, size, 0x02u, NULL);

    memcpy(mutated, linked, size);
    put64(mutated + maincmd + 8u, 0u);
    linked_rejected(mutated, size); /* header is never an entry */
    memcpy(mutated, linked, size);
    put64(mutated + maincmd + 8u, UINT64_MAX);
    linked_rejected(mutated, size); /* overflow */
    memcpy(mutated, linked, size);
    put32(mutated + textcmd + 60u, 7u);
    linked_rejected(mutated, size); /* W+X */
    memcpy(mutated, linked, size);
    put32(mutated + 8u, 2u);
    linked_rejected(mutated, size); /* arm64e/PAC profile denied */
    memcpy(mutated, linked, size);
    put32(mutated + dysymcmd + 28u, 1u);
    linked_rejected(mutated, size); /* unresolved dynamic symbol */
    memcpy(mutated, linked, size);
    {
        size_t offset = (size_t)((uint32_t)linked[fixcmd + 8u] |
              ((uint32_t)linked[fixcmd + 9u] << 8u) |
              ((uint32_t)linked[fixcmd + 10u] << 16u) |
              ((uint32_t)linked[fixcmd + 11u] << 24u));
        assert(offset <= size && size - offset >= 28u);
        put32(mutated + offset + 16u, 1u);
    }
    linked_rejected(mutated, size); /* chained import now needs dyld */
    memcpy(mutated, linked, size);
    mutated[dyldcmd + 12u] = 'X';
    linked_rejected(mutated, size); /* unexpected interpreter */
    memcpy(mutated, linked, size);
    {
        size_t off = (size_t)((uint32_t)linked[symcmd + 8u] |
            ((uint32_t)linked[symcmd + 9u] << 8u) |
            ((uint32_t)linked[symcmd + 10u] << 16u) |
            ((uint32_t)linked[symcmd + 11u] << 24u));
        assert(off <= size && size - off >= 16u);
        mutated[off + 4u] = 1u; /* N_UNDF */
    }
    linked_rejected(mutated, size);
    memcpy(mutated, linked, size);
    {
        size_t sec = textcmd + 72u;
        uint32_t sections = (uint32_t)linked[textcmd + 64u];
        assert(sections >= 1u);
        put32(mutated + sec + 60u, 1u); /* unlinked relocation */
    }
    linked_rejected(mutated, size);

    free(mutated);
    free(linked);
    puts("phase15a linked native execution/layout tests passed");
}


/* A host-independent three-segment preflight with explicit section records.
 * Unlike the compiler fixture this one deliberately does not require LLD. */
static void phase17a_section_layout_tests(void) {
    uint8_t b[0xc000] = {0};
    ios27_macho_info image;
    ios27_exec_layout layout;
    char err[160];
    const uint64_t base = UINT64_C(0x100000000);
    const size_t text = 32u, ro = 184u, data = 336u, maincmd = 568u;
    const size_t textsec = text + 72u, rosec = ro + 72u;
    const size_t datasec = data + 72u, bsssec = datasec + 80u;

    put32(b, UINT32_C(0xfeedfacf));
    put32(b + 4u, UINT32_C(0x0100000c));
    put32(b + 12u, IOS27_MH_EXECUTE);
    put32(b + 16u, 4u);
    put32(b + 20u, 560u);
    put32(b + text, 0x19u);
    put32(b + text + 4u, 152u);
    memcpy(b + text + 8u, "__TEXT", 6u);
    put64(b + text + 24u, base);
    put64(b + text + 32u, 0x4000u);
    put64(b + text + 48u, 0x4000u);
    put32(b + text + 56u, 5u);
    put32(b + text + 60u, 5u);
    put32(b + text + 64u, 1u);
    memcpy(b + textsec, "__text", 6u);
    memcpy(b + textsec + 16u, "__TEXT", 6u);
    put64(b + textsec + 32u, base + 1024u);
    put64(b + textsec + 40u, 8u);
    put32(b + textsec + 48u, 1024u);
    put32(b + textsec + 64u, UINT32_C(0x80000400));

    put32(b + ro, 0x19u);
    put32(b + ro + 4u, 152u);
    memcpy(b + ro + 8u, "__DATA_CONST", 12u);
    put64(b + ro + 24u, base + 0x4000u);
    put64(b + ro + 32u, 0x4000u);
    put64(b + ro + 40u, 0x4000u);
    put64(b + ro + 48u, 0x4000u);
    put32(b + ro + 56u, 1u);
    put32(b + ro + 60u, 1u);
    put32(b + ro + 64u, 1u);
    memcpy(b + rosec, "__const", 7u);
    memcpy(b + rosec + 16u, "__DATA_CONST", 12u);
    put64(b + rosec + 32u, base + 0x4000u);
    put64(b + rosec + 40u, 4u);
    put32(b + rosec + 48u, 0x4000u);

    put32(b + data, 0x19u);
    put32(b + data + 4u, 232u);
    memcpy(b + data + 8u, "__DATA", 6u);
    put64(b + data + 24u, base + 0x8000u);
    put64(b + data + 32u, 0x4000u);
    put64(b + data + 40u, 0x8000u);
    put64(b + data + 48u, 0x4000u);
    put32(b + data + 56u, 3u);
    put32(b + data + 60u, 3u);
    put32(b + data + 64u, 2u);
    memcpy(b + datasec, "__data", 6u);
    memcpy(b + datasec + 16u, "__DATA", 6u);
    put64(b + datasec + 32u, base + 0x8000u);
    put64(b + datasec + 40u, 4u);
    put32(b + datasec + 48u, 0x8000u);
    memcpy(b + bsssec, "__bss", 5u);
    memcpy(b + bsssec + 16u, "__DATA", 6u);
    put64(b + bsssec + 32u, base + 0x8004u);
    put64(b + bsssec + 40u, 32u);
    put32(b + bsssec + 64u, 1u); /* S_ZEROFILL, no section file offset */

    put32(b + maincmd, UINT32_C(0x80000028));
    put32(b + maincmd + 4u, 24u);
    put64(b + maincmd + 8u, 1024u);
    put32(b + 1024u, UINT32_C(0x52800540)); /* mov w0,#42 */
    put32(b + 1028u, UINT32_C(0xd65f03c0)); /* ret */
    put32(b + 0x4000u, 37u);
    put32(b + 0x8000u, 5u);
    memset(b + 0x8004u, 0xa5, 32u); /* must be ignored by S_ZEROFILL */

    assert(ios27_macho_parse(b, sizeof(b), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(b, sizeof(b), &image,
                                         4096u, &layout, err, sizeof(err)) == 0);
    assert(layout.span == 0xc000u);
    assert(ios27_macho_executable_layout(b, sizeof(b), &image,
                                         16384u, &layout, err, sizeof(err)) == 0);
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    {
        int value = -1;
        assert(ios27_run_freestanding(b, sizeof(b), &image,
                                      &value, err, sizeof(err)) == 0);
        assert(value == 42);
    }
#endif
    put32(b + rosec + 48u, 0x4004u); /* incorrect file/VM correspondence */
    assert(ios27_macho_parse(b, sizeof(b), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(b, sizeof(b), &image,
                                         4096u, &layout, err, sizeof(err)) != 0);
    put32(b + rosec + 48u, 0x4000u);
    put32(b + bsssec + 64u, 0u); /* zero-fill treated as file backed */
    assert(ios27_macho_parse(b, sizeof(b), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(b, sizeof(b), &image,
                                         16384u, &layout, err, sizeof(err)) != 0);
    put32(b + bsssec + 64u, 1u);
    put32(b + rosec + 64u, 6u); /* dyld pointer section unsupported */
    assert(ios27_macho_parse(b, sizeof(b), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(b, sizeof(b), &image,
                                         4096u, &layout, err, sizeof(err)) != 0);
    put32(b + rosec + 64u, 0u);
    put64(b + bsssec + 32u, base + 0x8002u); /* overlaps __data */
    assert(ios27_macho_parse(b, sizeof(b), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(b, sizeof(b), &image,
                                         16384u, &layout, err, sizeof(err)) != 0);
    put64(b + bsssec + 32u, base + 0x8004u);
    put32(b + textsec + 60u, 1u); /* unresolved relocation */
    assert(ios27_macho_parse(b, sizeof(b), &image, err, sizeof(err)) == 0);
    assert(ios27_macho_executable_layout(b, sizeof(b), &image,
                                         4096u, &layout, err, sizeof(err)) != 0);
    puts("phase17a multisegment section and 4k/16k layout tests passed");
}

int main(void) {
    executable_layout_tests();
    phase17a_section_layout_tests();
    linked_phase15a_tests();
    executable_extra_rejections();
    uint8_t b[264], scratch[264];
    ios27_macho_info image;
    char err[128];
    int rc;
    fixture(b);
    assert(ios27_macho_parse(b, sizeof(b), &image, err, sizeof(err)) == 0);
    assert(image.segment_count == 1u);
    assert(image.has_main && image.entryoff == 256u);
    assert(image.filetype == IOS27_MH_EXECUTE);
    assert(ios27_macho_parse(b, 30u, &image, err, sizeof(err)) != 0);
    assert(ios27_macho_parse(b, 263u, &image, err, sizeof(err)) != 0);
    memcpy(scratch, b, sizeof(b));
    put32(scratch + 36, 0xffffffffu);
    assert(ios27_macho_parse(scratch, sizeof(scratch), &image, err, sizeof(err)) != 0);
    memcpy(scratch, b, sizeof(b));
    put32(scratch + 16, 3);
    assert(ios27_macho_parse(scratch, sizeof(scratch), &image, err, sizeof(err)) != 0);
    memcpy(scratch, b, sizeof(b));
    put64(scratch + 80, UINT64_MAX);
    assert(ios27_macho_parse(scratch, sizeof(scratch), &image, err, sizeof(err)) != 0);
    memcpy(scratch, b, sizeof(b));
    put32(scratch + 4, 0x01000007);
    assert(ios27_macho_parse(scratch, sizeof(scratch), &image, err, sizeof(err)) != 0);
    memcpy(scratch, b, sizeof(b));
    put32(scratch + 108, 16);
    assert(ios27_macho_parse(scratch, sizeof(scratch), &image, err, sizeof(err)) != 0);

    fixture(b);
    assert(ios27_macho_parse(b, sizeof(b), &image, err, sizeof(err)) == 0);
    if (sizeof(void *) == 8u) {
        /* On an ARM64 Linux/Android host, this entry must return 42. */
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
        rc = 0;
        assert(ios27_run_freestanding(b, sizeof(b), &image, &rc,
                                      err, sizeof(err)) == 0);
        assert(rc == 42);
#endif
    }
    rc = 0;
    image.needs_dyld = 1u;
    assert(ios27_run_freestanding(b, sizeof(b), &image, &rc,
                                  err, sizeof(err)) != 0);
    puts("macho tests passed");
    return 0;
}
