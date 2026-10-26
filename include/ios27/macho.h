#ifndef IOS27_MACHO_H
#define IOS27_MACHO_H

#include <stddef.h>
#include <stdint.h>

#define IOS27_MAX_SEGMENTS 64u
#define IOS27_MH_EXECUTE 2u

typedef struct {
    char name[17];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
} ios27_segment;

typedef struct {
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t flags;
    uint32_t platform;
    uint32_t minos;
    uint32_t sdk;
    uint64_t entryoff;
    uint64_t stacksize;
    size_t segment_count;
    unsigned has_main : 1;
    unsigned needs_dyld : 1;
    unsigned has_dylibs : 1;
    unsigned has_fixups : 1;
    unsigned encrypted : 1;
    ios27_segment segments[IOS27_MAX_SEGMENTS];
} ios27_macho_info;

/* Parses only little-endian, thin ARM64 Mach-O images. No code is executed. */
int ios27_macho_parse(const uint8_t *data, size_t size,
                     ios27_macho_info *out, char *error, size_t error_size);

/*
 * Experimental in-process execution of freestanding ARM64 images only.
 * Requires ARM64 hardware and a trusted, specially constructed test image.
 * Does NOT implement iOS syscalls, dyld, ObjC, frameworks, or a sandbox.
 */
int ios27_run_freestanding(const uint8_t *data, size_t size,
                          const ios27_macho_info *info,
                          int *exit_code, char *error, size_t error_size);

#endif
