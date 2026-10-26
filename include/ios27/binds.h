#ifndef IOS27_BINDS_H
#define IOS27_BINDS_H

#include "ios27/macho.h"
#include <stddef.h>
#include <stdint.h>

#define IOS27_MAX_BINDS 256u
#define IOS27_BIND_SYMBOL_MAX 256u

typedef struct {
    char symbol[IOS27_BIND_SYMBOL_MAX];
    int32_t ordinal;
    int64_t addend;
    uint64_t segment_offset;
    size_t segment_index;
} ios27_bind;

typedef struct {
    size_t count;
    ios27_bind entries[IOS27_MAX_BINDS];
} ios27_bind_plan;

/* Read a thin ARM64 Mach-O's *regular* classic dyld bind opcodes.
 * Does NOT support chained fixups, weak/lazy bindings, or rebasing. */
int ios27_binds_parse(const uint8_t *file, size_t size,
                      const ios27_macho_info *info,
                      ios27_bind_plan *out, char *error, size_t error_size);

/* Resolve a symbol in a *trusted, explicitly selected* synthetic provider.
 * Return 0 on success with an absolute 64-bit address, nonzero on failure. */
typedef int (*ios27_bind_resolver)(void *context, const char *symbol,
                                  int32_t ordinal, uint64_t *address);

/* Applies all bindings atomically to a caller-owned writable COPY of the
 * file bytes. NEVER opens/writes host files or memory outside that buffer.
 * Not a production dyld or VM loader; no Apple frameworks are executed. */
int ios27_binds_apply(uint8_t *copy, size_t copy_size,
                      const ios27_macho_info *info,
                      const ios27_bind_plan *plan,
                      ios27_bind_resolver resolver, void *context,
                      char *error, size_t error_size);

#endif
