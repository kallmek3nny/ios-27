#include "ios27/macho.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
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
#define MAX_SPAN (256ull * 1024ull * 1024ull)
static uint64_t down(uint64_t x, uint64_t a) { return x - (x % a); }
static int up(uint64_t x, uint64_t a, uint64_t *out) {
    uint64_t remainder = x % a;
    if (remainder != 0u) {
        if (x > UINT64_MAX - (a - remainder))
            return -1;
        x += a - remainder;
    }
    *out = x;
    return 0;
}

/* VM_PROT_READ, WRITE, EXECUTE in Darwin and PROT_* in Linux use 1,2,4. */
static int protection(uint32_t p) {
    return ((p & 1u) ? PROT_READ : 0) |
           ((p & 2u) ? PROT_WRITE : 0) |
           ((p & 4u) ? PROT_EXEC : 0);
}
#endif

int ios27_run_freestanding(const uint8_t *data, size_t size,
                          const ios27_macho_info *info,
                          int *exit_code, char *error, size_t error_size) {
#if !defined(__aarch64__) || (!defined(__linux__) && !defined(__ANDROID__))
    (void)data; (void)size; (void)info; (void)exit_code;
    return fail(error, error_size, "execution requires ARM64 Linux/Android");
#else
    uint64_t page, low = UINT64_MAX, high = 0u, entry_address = 0u;
    size_t span;
    uint8_t *mapping;
    long page_size = sysconf(_SC_PAGESIZE);
    int found_entry = 0;

    if (data == NULL || info == NULL || exit_code == NULL || page_size <= 0)
        return fail(error, error_size, "invalid runtime input");
    if (info->cputype != 0x0100000cu || info->filetype != IOS27_MH_EXECUTE ||
        !info->has_main || info->needs_dyld || info->has_dylibs ||
        info->has_fixups || info->encrypted)
        return fail(error, error_size, "only freestanding ARM64 MH_EXECUTE images with LC_MAIN are supported");
    page = (uint64_t)page_size;
    for (size_t i = 0; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        uint64_t segend, segstart;
        if (strcmp(s->name, "__PAGEZERO") == 0 && s->filesize == 0u)
            continue;
        if (s->vmsize == 0u)
            continue;
        if ((s->initprot & ~7u) || (s->maxprot & ~7u) ||
            (s->initprot & ~s->maxprot) ||
            (s->initprot & 6u) == 6u ||
            s->fileoff > (uint64_t)size ||
            s->filesize > (uint64_t)size - s->fileoff ||
            s->filesize > s->vmsize ||
            s->vmsize > UINT64_MAX - s->vmaddr ||
            up(s->vmaddr + s->vmsize, page, &segend) != 0)
            return fail(error, error_size, "unsupported segment protection or bounds");
        segstart = down(s->vmaddr, page);
        if (segstart < low) low = segstart;
        if (segend > high) high = segend;

        if (info->entryoff >= s->fileoff &&
            info->entryoff - s->fileoff < s->filesize) {
            if ((s->initprot & 4u) == 0u || found_entry)
                return fail(error, error_size, "invalid executable entry point");
            entry_address = s->vmaddr + (info->entryoff - s->fileoff);
            found_entry = 1;
        }
    }
    if (!found_entry || low == UINT64_MAX || high <= low ||
        high - low > MAX_SPAN || high - low > (uint64_t)SIZE_MAX)
        return fail(error, error_size, "invalid image span or LC_MAIN entry");
    span = (size_t)(high - low);

    /* Reject page sharing between segments; necessary for unambiguous W^X. */
    for (size_t i = 0; i < info->segment_count; ++i) {
        const ios27_segment *a = &info->segments[i];
        uint64_t ae;
        if (a->vmsize == 0u || (strcmp(a->name, "__PAGEZERO") == 0 && a->filesize == 0u))
            continue;
        if (up(a->vmaddr + a->vmsize, page, &ae) != 0)
            return fail(error, error_size, "segment alignment overflow");
        for (size_t j = i + 1; j < info->segment_count; ++j) {
            const ios27_segment *b = &info->segments[j];
            uint64_t be;
            if (b->vmsize == 0u || (strcmp(b->name, "__PAGEZERO") == 0 && b->filesize == 0u))
                continue;
            if (up(b->vmaddr + b->vmsize, page, &be) != 0)
                return fail(error, error_size, "segment alignment overflow");
            if (down(a->vmaddr, page) < be && down(b->vmaddr, page) < ae)
                return fail(error, error_size, "overlapping segment pages unsupported");
        }
    }

    mapping = mmap(NULL, span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED)
        return fail(error, error_size, "mmap reservation failed");
    for (size_t i = 0; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        uint64_t start, end;
        uint8_t *target;
        if (s->vmsize == 0u || (strcmp(s->name, "__PAGEZERO") == 0 && s->filesize == 0u))
            continue;
        start = down(s->vmaddr, page) - low;
        if (up(s->vmaddr + s->vmsize, page, &end) != 0 ||
            mprotect(mapping + (size_t)start, (size_t)(end - low - start),
                     PROT_READ | PROT_WRITE) != 0)
            goto mapping_failed;
        target = mapping + (size_t)(s->vmaddr - low);
        if (s->filesize != 0u)
            memcpy(target, data + (size_t)s->fileoff, (size_t)s->filesize);
        __builtin___clear_cache((char *)target, (char *)(target + (size_t)s->vmsize));
        if (mprotect(mapping + (size_t)start, (size_t)(end - low - start),
                     protection(s->initprot)) != 0)
            goto mapping_failed;
    }
    {
        /* Explicitly limited to a trusted test entry with signature int(void). */
        int (*entry)(void);
        uintptr_t entry_ptr = (uintptr_t)(mapping + (size_t)(entry_address - low));
        memcpy(&entry, &entry_ptr, sizeof(entry));
        *exit_code = entry();
    }
    (void)munmap(mapping, span);
    if (error != NULL && error_size != 0u) error[0] = '\0';
    return 0;

mapping_failed:
    (void)munmap(mapping, span);
    return fail(error, error_size, "segment mapping/protection failed");
#endif
}
