#include "ios27/darwin_object.h"
#include "ios27/macho.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <sys/wait.h>
#endif
#if defined(__aarch64__) && defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4u) << 32u);
}
static void wr32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0u; i < 4u; ++i)
        p[i] = (uint8_t)(v >> (i * 8u));
}
static void wr64(uint8_t *p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4u, (uint32_t)(v >> 32u));
}
static size_t command(const uint8_t *p, size_t size, uint32_t op,
                      const char *segment) {
    size_t pos = 32u;
    assert(size >= 32u);
    for (uint32_t i = 0u; i < rd32(p + 16u); ++i) {
        uint32_t cmdsize;
        assert(pos <= size && size - pos >= 8u);
        cmdsize = rd32(p + pos + 4u);
        assert(cmdsize >= 8u && cmdsize <= size - pos);
        if (rd32(p + pos) == op &&
            (!segment || (cmdsize >= 72u &&
                          strncmp((const char *)p + pos + 8u,
                                  segment, 16u) == 0)))
            return pos;
        pos += cmdsize;
    }
    assert(!"expected load command missing");
    return 0u;
}
static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    long length;
    uint8_t *data;
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    length = ftell(f);
    assert(length >= 32L && length <= 2L * 1024L * 1024L);
    assert(fseek(f, 0, SEEK_SET) == 0);
    data = malloc((size_t)length);
    assert(data);
    assert(fread(data, 1u, (size_t)length, f) == (size_t)length);
    assert(fclose(f) == 0);
    *size = (size_t)length;
    return data;
}
static void rejected(const uint8_t *bytes, size_t size) {
    ios27_darwin_executable image;
    char error[160] = {0};
    assert(ios27_darwin_executable_inspect(bytes, size, &image,
                                           error, sizeof(error)) == -1);
    assert(error[0] != '\0');
}

/*
 * This test intentionally uses the existing CTest executable rather than
 * editing CMakeLists.txt. Build a second genuine LLVM-linked ARM64 MH_EXECUTE
 * from original source. Its program reads __DATA_CONST, initializes/writes
 * __DATA and reads/writes a zero-filled __bss via compiler-generated ADRP.
 */
static void phase17a_compiler_linked_test(void) {
#if defined(__linux__)
    int status = system("timeout 75s python3 ../tools/phase17a-build.py "
                        "phase17a-multisegment");
    uint8_t *linked, *mutated;
    size_t size, rocmd, datacmd, fixcmd, maincmd, bsscmd = 0u;
    ios27_macho_info parsed;
    ios27_exec_layout layout;
    ios27_darwin_executable inspected;
    char error[160] = {0};
    int result = 999;
    assert(status >= 0 && WIFEXITED(status));
    if (WEXITSTATUS(status) == 77) {
        puts("phase17a clang+lld integration skipped: linker unavailable");
        return;
    }
    assert(WEXITSTATUS(status) == 0);
    linked = read_file("phase17a-multisegment", &size);
    mutated = malloc(size);
    assert(mutated);
    assert(ios27_macho_parse(linked, size, &parsed, error, sizeof(error)) == 0);
    assert(ios27_darwin_executable_inspect(linked, size, &inspected,
                                            error, sizeof(error)) == 0);
    assert(parsed.segment_count >= 5u);
    assert(ios27_macho_executable_layout(linked, size, &parsed, 4096u,
                                         &layout, error, sizeof(error)) == 0);
    assert(ios27_macho_executable_layout(linked, size, &parsed, 16384u,
                                         &layout, error, sizeof(error)) == 0);
    rocmd = command(linked, size, 0x19u, "__DATA_CONST");
    datacmd = command(linked, size, 0x19u, "__DATA");
    fixcmd = command(linked, size, UINT32_C(0x80000034), NULL);
    maincmd = command(linked, size, UINT32_C(0x80000028), NULL);
    assert(rd32(linked + rocmd + 60u) == 1u);
    assert(rd32(linked + datacmd + 60u) == 3u);
    for (uint32_t i = 0u; i < rd32(linked + datacmd + 64u); ++i) {
        size_t section = datacmd + 72u + (size_t)i * 80u;
        if (strncmp((const char *)linked + section, "__bss", 16u) == 0) {
            assert(!bsscmd);
            bsscmd = section;
        }
    }
    assert(bsscmd && (rd32(linked + bsscmd + 64u) & 0xffu) == 1u);
    /* An actual ARM64 C compiler must produce page-relative addressing. */
    {
        int adrp_seen = 0;
        for (size_t i = 0u; i + 4u <= (size_t)inspected.text_size; i += 4u) {
            uint32_t opcode = rd32(linked + (size_t)inspected.text_file_offset + i);
            if ((opcode & UINT32_C(0x9f000000)) == UINT32_C(0x90000000))
                ++adrp_seen;
        }
        assert(adrp_seen >= 1);
    }
#if defined(__aarch64__)
    assert(ios27_run_freestanding(linked, size, &parsed, &result,
                                  error, sizeof(error)) == 0);
    assert(result == 42);
    result = 999;
    assert(ios27_run_freestanding(linked, size, &parsed, &result,
                                  error, sizeof(error)) == 0);
    assert(result == 42); /* no leaked state across fresh reservations */
#else
    assert(ios27_run_freestanding(linked, size, &parsed, &result,
                                  error, sizeof(error)) != 0);
    assert(result == 999);
#endif
    memcpy(mutated, linked, size);
    wr32(mutated + rocmd + 60u, 7u); /* read-only code/data must never W+X */
    rejected(mutated, size);
    memcpy(mutated, linked, size);
    wr32(mutated + bsscmd + 64u, 0u); /* no file bytes back zero-fill */
    rejected(mutated, size);
    memcpy(mutated, linked, size);
    wr64(mutated + maincmd + 8u, UINT64_MAX);
    rejected(mutated, size);
    memcpy(mutated, linked, size);
    {
        uint32_t offset = rd32(linked + fixcmd + 8u);
        uint32_t starts = rd32(linked + offset + 4u);
        uint32_t length = rd32(linked + fixcmd + 12u);
        assert((uint64_t)offset + length <= size && length >= 28u);
        if (starts < length && length - starts >= 8u &&
            rd32(linked + offset + starts) != 0u) {
            wr32(mutated + offset + starts + 4u, 1u);
            rejected(mutated, size); /* an actual chained rebase needs dyld */
            memcpy(mutated, linked, size);
        }
        wr32(mutated + offset + 16u, 1u);
        rejected(mutated, size); /* an actual chained import needs dyld */
    }
    free(mutated);
    free(linked);
    puts("phase17a real compiler-linked multisegment/bss/adrp tests passed");
#endif
}


/* A second original Clang executable, linked as MH_EXECUTE by real LLVM
 * ld64.lld or the narrow verified fallback, checks actual Darwin main
 * registers, isolated writable stack, argv/envp copies and fresh lifecycle. */
static void phase18a_compiler_linked_test(void) {
#if defined(__linux__)
    int status = system("timeout 75s python3 ../tools/phase18a-build.py "
                        "phase18a-startup");
    uint8_t *bytes, *mutated;
    size_t length, maincmd, textcmd;
    ios27_macho_info info;
    ios27_exec_layout layout;
    ios27_darwin_executable exe;
    char error[160] = {0};
    int result = 777;
    const char *args[] = {"ios27", "oak", "pine"};
    const char *env[] = {"MODE=test", NULL};
    assert(status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    bytes = read_file("phase18a-startup", &length);
    mutated = malloc(length);
    assert(mutated);
    assert(ios27_darwin_executable_inspect(bytes, length, &exe,
                                           error, sizeof(error)) == 0);
    assert(ios27_macho_parse(bytes, length, &info,
                             error, sizeof(error)) == 0);
    assert(ios27_macho_executable_layout(bytes, length, &info, 4096u,
                                         &layout, error, sizeof(error)) == 0);
    assert(ios27_macho_executable_layout(bytes, length, &info, 16384u,
                                         &layout, error, sizeof(error)) == 0);
    assert(info.segment_count >= 5u);
    maincmd = command(bytes, length, UINT32_C(0x80000028), NULL);
    textcmd = command(bytes, length, 0x19u, "__TEXT");
    memcpy(mutated, bytes, length);
    wr64(mutated + maincmd + 16u, 131072u);
    assert(ios27_macho_parse(mutated, length, &info,
                             error, sizeof(error)) == 0);
    assert(info.stacksize == 131072u);
    assert(ios27_macho_executable_layout(mutated, length, &info, 4096u,
                                         &layout, error, sizeof(error)) == 0);
    assert(ios27_macho_executable_layout(mutated, length, &info, 16384u,
                                         &layout, error, sizeof(error)) == 0);
#if defined(__aarch64__)
    assert(ios27_run_executable_argv(mutated, length, &info, 3, args, env,
                                     &result, error, sizeof(error)) == 0);
    assert(result == 42);
    assert(strcmp(args[1], "oak") == 0 && strcmp(env[0], "MODE=test") == 0);
    result = 777;
    assert(ios27_run_executable_argv(mutated, length, &info, 3, args, env,
                                     &result, error, sizeof(error)) == 0);
    assert(result == 42); /* no state or mappings persist across calls */
    result = 777;
    assert(ios27_run_executable_argv(mutated, length, &info, 0, NULL, NULL,
                                     &result, error, sizeof(error)) == 0);
    assert(result == 101); /* entry genuinely receives the ABI arguments */
    result = 777;
    assert(ios27_run_executable_argv(mutated, length, &info, -1, args, env,
                                     &result, error, sizeof(error)) != 0);
    assert(result == 777);
    assert(ios27_run_executable_argv(mutated, length, &info, 129, args, env,
                                     &result, error, sizeof(error)) != 0);
    assert(result == 777);
    assert(ios27_run_executable_argv(mutated, length, &info, 3, NULL, env,
                                     &result, error, sizeof(error)) != 0);
    assert(result == 777);
#else
    assert(ios27_run_executable_argv(mutated, length, &info, 3, args, env,
                                     &result, error, sizeof(error)) != 0);
    assert(result == 777);
#endif
    /* Reject oversized process stacks on both architectures at preflight. */
    wr64(mutated + maincmd + 16u, UINT64_C(8) * 1024u * 1024u + 1u);
    assert(ios27_macho_parse(mutated, length, &info,
                             error, sizeof(error)) == 0);
    assert(ios27_macho_executable_layout(mutated, length, &info, 4096u,
                                         &layout, error, sizeof(error)) != 0);
    assert(layout.span == 0u);
    assert(ios27_macho_executable_layout(mutated, length, &info, 16384u,
                                         &layout, error, sizeof(error)) != 0);
    assert(layout.span == 0u);
    assert(ios27_run_executable_argv(mutated, length, &info, 3, args, env,
                                     &result, error, sizeof(error)) != 0);
#if defined(__aarch64__)
    assert(result == 777);
#endif
    memcpy(mutated, bytes, length);
    wr32(mutated + textcmd + 60u, 7u);
    assert(ios27_macho_parse(mutated, length, &info,
                             error, sizeof(error)) == 0);
    assert(ios27_macho_executable_layout(mutated, length, &info, 4096u,
                                         &layout, error, sizeof(error)) != 0);
    assert(layout.span == 0u);
    free(mutated);
    free(bytes);
    puts("phase18a native Darwin main/argv/envp/guest stack tests passed");
#endif
}

int main(int argc, char **argv) {
    uint8_t *bytes, *bad;
    size_t length, textcmd, editcmd, pagecmd, maincmd;
    size_t symcmd, dysymcmd, fixcmd, textsec;
    size_t branch_count = 0u;
    ios27_darwin_executable exe;
    char error[160] = {0};

    assert(argc == 2);
    bytes = read_file(argv[1], &length);
    bad = malloc(length);
    assert(bad);
    assert(ios27_darwin_executable_inspect(bytes, length, &exe,
                                           error, sizeof(error)) == 0);
    assert(exe.platform == 1u && exe.minos != 0u);
    assert(exe.entry_file_offset >= exe.text_file_offset);
    assert(exe.text_size >= 8u && exe.text_size < 4096u);
    assert(exe.entry_file_offset < length);
    assert(exe.segment_count >= 3u);
    /* The cross-object call must have been *linked*, not left as a
     * relocation, with a genuine ARM64 BL landing inside __text. */
    for (size_t off = 0u; off + 4u <= (size_t)exe.text_size; off += 4u) {
        uint32_t insn = rd32(bytes + exe.text_file_offset + off);
        if ((insn & UINT32_C(0xfc000000)) == UINT32_C(0x94000000)) {
            int64_t imm = (int64_t)(insn & UINT32_C(0x03ffffff));
            int64_t target;
            if ((imm & (INT64_C(1) << 25u)) != 0)
                imm -= INT64_C(1) << 26u;
            target = (int64_t)off + imm * 4;
            assert(target >= 0 && target + 4 <= (int64_t)exe.text_size);
            ++branch_count;
        }
    }
    assert(branch_count >= 1u);

    textcmd = command(bytes, length, 0x19u, "__TEXT");
    editcmd = command(bytes, length, 0x19u, "__LINKEDIT");
    pagecmd = command(bytes, length, 0x19u, "__PAGEZERO");
    maincmd = command(bytes, length, UINT32_C(0x80000028), NULL);
    symcmd = command(bytes, length, 0x02u, NULL);
    dysymcmd = command(bytes, length, 0x0bu, NULL);
    fixcmd = command(bytes, length, UINT32_C(0x80000034), NULL);
    textsec = textcmd + 72u;
    assert(rd32(bytes + textcmd + 64u) == 1u);
    assert(rd32(bytes + textsec + 60u) == 0u);
    assert(rd32(bytes + dysymcmd + 28u) == 0u);
    assert(rd32(bytes + maincmd + 8u) == (uint32_t)exe.entry_file_offset);

    /*
     * A real linked LLVM Mach-O can carry strictly inert legacy dyld info
     * instead of an inert chained-fixup command. Rewrite only that header
     * command, leaving all original Clang-produced instructions and
     * actual loadable segment bytes unchanged.
     */
    {
        const size_t cmd_end = 32u + rd32(bytes + 20u);
        const size_t old_fix_size = rd32(bytes + fixcmd + 4u);
        const size_t text_start = (size_t)exe.text_file_offset;
        const size_t text_bytes = (size_t)exe.text_size;
        const size_t text_section = textcmd + 72u;
        const size_t sym_start = rd32(bytes + symcmd + 8u);
        const uint32_t symbol_count = rd32(bytes + symcmd + 12u);
        const size_t shifted_main = maincmd > fixcmd ? maincmd + 32u : maincmd;
        assert(old_fix_size == 16u);
        assert(fixcmd + old_fix_size <= cmd_end && cmd_end <= text_start);
        assert(text_bytes <= 4096u && text_start <= 4096u &&
               text_start + text_bytes + 32u <= 4096u);
        assert(sym_start <= length && (uint64_t)symbol_count * 16u <=
               length - sym_start);
        memcpy(bad, bytes, length);
        /*
         * The phase15a fallback has no unused command padding. Move the
         * original compiler-linked text together by 32 bytes *inside*
         * its existing RX page before expanding the command table.
         * Its relative branches keep exactly the same displacements.
         */
        memmove(bad + text_start + 32u, bad + text_start, text_bytes);
        memmove(bad + fixcmd + 48u, bad + fixcmd + old_fix_size,
                cmd_end - fixcmd - old_fix_size);
        /* Clear the old chained-fixup offset and size as well: those
         * two fields otherwise masquerade as legacy rebase metadata. */
        memset(bad + fixcmd + 8u, 0, 40u);
        wr32(bad + fixcmd, UINT32_C(0x80000022));
        wr32(bad + fixcmd + 4u, 48u);
        wr32(bad + 20u, rd32(bytes + 20u) + 32u);
        wr64(bad + text_section + 32u, rd64(bytes + text_section + 32u) + 32u);
        wr32(bad + text_section + 48u, rd32(bytes + text_section + 48u) + 32u);
        wr64(bad + shifted_main + 8u, rd64(bytes + maincmd + 8u) + 32u);
        for (uint32_t j = 0u; j < symbol_count; ++j) {
            const size_t sym = sym_start + (size_t)j * 16u;
            if ((bad[sym + 4u] & 0x0eu) == 0x0eu && bad[sym + 5u] == 1u)
                wr64(bad + sym + 8u, rd64(bytes + sym + 8u) + 32u);
        }
        assert(ios27_darwin_executable_inspect(bad, length, &exe,
                                               error, sizeof(error)) == 0);
        {
            ios27_macho_info legacy;
            ios27_exec_layout mapped;
            assert(ios27_macho_parse(bad, length, &legacy,
                                     error, sizeof(error)) == 0);
            assert(ios27_macho_executable_layout(bad, length, &legacy, 4096u,
                                                 &mapped, error, sizeof(error)) == 0);
        }
        wr32(bad + fixcmd + 12u, 1u); /* actual dyld rebase is never inert */
        rejected(bad, length);
        memcpy(bad, bytes, length);
    }

    memcpy(bad, bytes, length);
    wr32(bad + 12u, 1u); /* MH_OBJECT is not executable */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + 8u, 2u); /* unsupported ARM64e subtype */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr64(bad + maincmd + 8u, UINT64_MAX); /* LC_MAIN offset overflow */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr64(bad + maincmd + 8u, 0u); /* points to header, not __text */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + maincmd, 0x777u); /* LC_MAIN absent */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + textcmd + 60u, 7u); /* W+X __TEXT */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + editcmd + 60u, 5u); /* executable __LINKEDIT */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + pagecmd + 56u, 7u); /* __PAGEZERO must be inaccessible */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr64(bad + editcmd + 24u,
         ((uint64_t)rd32(bytes + textcmd + 28u) << 32u) |
          rd32(bytes + textcmd + 24u)); /* overlapping virtual segments */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + textsec + 60u, 1u); /* unlinked section relocation */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + textsec + 48u, UINT32_MAX); /* truncated __text */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + dysymcmd + 28u, 1u); /* undefined dynamic symbol */
    rejected(bad, length);
    memcpy(bad, bytes, length);
    {
        uint32_t fixup_off = rd32(bytes + fixcmd + 8u);
        assert((uint64_t)fixup_off + 28u <= length);
        wr32(bad + fixup_off + 16u, 1u); /* chained import */
    }
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + symcmd + 8u, UINT32_MAX);
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + fixcmd + 12u, UINT32_MAX);
    rejected(bad, length);
    memcpy(bad, bytes, length);
    wr32(bad + textcmd + 4u, 8u);
    rejected(bad, length);
    rejected(bytes, 31u);
    assert(ios27_darwin_executable_inspect(bytes, length, &exe,
                                           error, sizeof(error)) == 0);
#if defined(__aarch64__) && defined(__linux__)
    {
        long page = sysconf(_SC_PAGESIZE);
        uint8_t *map;
        uintptr_t address;
        int (*fn)(void);
        assert(page >= 4096L && (uint64_t)page >= exe.text_size);
        map = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(map != MAP_FAILED);
        memcpy(map, bytes + exe.text_file_offset, (size_t)exe.text_size);
        __builtin___clear_cache((char *)map,
                                (char *)map + (size_t)exe.text_size);
        assert(mprotect(map, (size_t)page, PROT_READ | PROT_EXEC) == 0);
        address = (uintptr_t)map + (uintptr_t)
                  (exe.entry_file_offset - exe.text_file_offset);
        memcpy(&fn, &address, sizeof(fn));
        assert(fn() == 43);
        assert(munmap(map, (size_t)page) == 0);
    }
#else
    puts("native linked entry skipped (requires ARM64 Linux)");
#endif
    free(bad);
    free(bytes);
    phase17a_compiler_linked_test();
    phase18a_compiler_linked_test();
    puts("genuine Clang+LLD Darwin MH_EXECUTE fixture passed");
    return 0;
}
