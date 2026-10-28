#include "ios27/macho.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
#include <sys/mman.h>
#include <unistd.h>
#endif

static int fail(char *error, size_t capacity, const char *message) {
    if (error != NULL && capacity != 0u)
        (void)snprintf(error, capacity, "%s", message);
    return -1;
}

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
/* Darwin VM_PROT_* and Linux PROT_* share bits 1,2,4. */
static int protection(uint32_t p) {
    return ((p & 1u) ? PROT_READ : 0) |
           ((p & 2u) ? PROT_WRITE : 0) |
           ((p & 4u) ? PROT_EXEC : 0);
}

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t read64(const uint8_t *p) {
    return (uint64_t)read32(p) | ((uint64_t)read32(p + 4u) << 32u);
}

/* S_ZEROFILL has no meaningful file bytes, even when the segment's
 * filesize is rounded up across the BSS virtual range by ld64.lld. */
static void initialize_zerofill(const uint8_t *file,
                                const ios27_exec_layout *layout,
                                uint8_t *mapping) {
    size_t cursor = 32u;
    uint32_t commands = read32(file + 16u);
    for (uint32_t i = 0u; i < commands; ++i) {
        const uint8_t *cmd = file + cursor;
        if (read32(cmd) == 0x19u) {
            uint32_t sections = read32(cmd + 64u);
            for (uint32_t j = 0u; j < sections; ++j) {
                const uint8_t *sec = cmd + 72u + (size_t)j * 80u;
                if ((read32(sec + 64u) & 0xffu) == 1u) {
                    uint64_t addr = read64(sec + 32u);
                    uint64_t length = read64(sec + 40u);
                    if (length)
                        memset(mapping + (size_t)(addr - layout->min_vmaddr),
                               0, (size_t)length);
                }
            }
        }
        cursor += (size_t)read32(cmd + 4u);
    }
}
#endif

int ios27_run_freestanding(const uint8_t *data, size_t size,
                          const ios27_macho_info *info,
                          int *exit_code, char *error, size_t error_size) {
#if !defined(__aarch64__) || (!defined(__linux__) && !defined(__ANDROID__))
    (void)data; (void)size; (void)info; (void)exit_code;
    return fail(error, error_size, "execution requires ARM64 Linux/Android");
#else
    ios27_exec_layout layout;
    long os_page = sysconf(_SC_PAGESIZE);
    uint8_t *mapping;
    if (!data || !info || !exit_code || os_page <= 0)
        return fail(error, error_size, "invalid native runtime input");
    /* Fail closed on unsupported host pages, stale metadata or W^X ambiguity.
     * All checks complete before any executable memory becomes accessible. */
    if (ios27_macho_executable_layout(data, size, info, (size_t)os_page,
                                      &layout, error, error_size))
        return -1;
    if (info->stacksize != 0u)
        return fail(error, error_size, "nonzero LC_MAIN stacksize not implemented");
    mapping = mmap(NULL, layout.span, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED)
        return fail(error, error_size, "native VM reservation failed");

    for (size_t i = 0u; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        uint64_t aligned_start, aligned_end;
        uint8_t *staging, *target;
        size_t bytes;
        if (s->vmsize == 0u || strcmp(s->name, "__PAGEZERO") == 0)
            continue;
        aligned_start = s->vmaddr & ~((uint64_t)layout.page_size - 1u);
        aligned_end = (s->vmaddr + s->vmsize +
                       (uint64_t)layout.page_size - 1u) &
                      ~((uint64_t)layout.page_size - 1u);
        bytes = (size_t)(aligned_end - aligned_start);
        staging = mapping + (size_t)(aligned_start - layout.min_vmaddr);
        target = mapping + (size_t)(s->vmaddr - layout.min_vmaddr);
        /* A private anonymous reservation provides zero-filled tails and gaps.
         * Writable staging is never executable; final pages are never W+X. */
        if (mprotect(staging, bytes, PROT_READ | PROT_WRITE) != 0)
            goto mapping_failed;
        if (s->filesize != 0u)
            memcpy(target, data + (size_t)s->fileoff, (size_t)s->filesize);
        if (s->initprot & 4u)
            __builtin___clear_cache((char *)target,
                                    (char *)(target + (size_t)s->filesize));
    }
    initialize_zerofill(data, &layout, mapping);
    /*
     * Commit the complete image only after every segment has been populated.
     * No guest code is executable during staging, and all pages get their
     * final protection before LC_MAIN is called. A failed seal unmaps the
     * entire reservation, including any segments already made executable.
     */
    for (size_t i = 0u; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        uint64_t start, end;
        if (!s->vmsize || strcmp(s->name, "__PAGEZERO") == 0)
            continue;
        start = s->vmaddr & ~((uint64_t)layout.page_size - 1u);
        end = (s->vmaddr + s->vmsize +
               (uint64_t)layout.page_size - 1u) &
              ~((uint64_t)layout.page_size - 1u);
        if (mprotect(mapping + (size_t)(start - layout.min_vmaddr),
                     (size_t)(end - start), protection(s->initprot)) != 0)
            goto mapping_failed;
    }
    {
        /* Trusted fixture ABI: int entry(void); no Darwin services or stack. */
        int (*entry)(void);
        uintptr_t address = (uintptr_t)(mapping +
                              (size_t)(layout.entry_vmaddr - layout.min_vmaddr));
        memcpy(&entry, &address, sizeof(entry));
        *exit_code = entry();
    }
    (void)munmap(mapping, layout.span);
    if (error && error_size) error[0] = '\0';
    return 0;

mapping_failed:
    (void)munmap(mapping, layout.span); /* unwind the entire reservation */
    return fail(error, error_size, "native segment mapping/protection failed");
#endif
}
