#ifndef IOS27_PHASE22D_METADATA_H
#define IOS27_PHASE22D_METADATA_H
#include "ios27/phase22d_runtime.h"
#include <stddef.h>
#include <stdint.h>

/* Inspects real compiler-produced arm64 Mach-O MH_OBJECT metadata with the
 * preexisting read-only objc_metadata parser, then verifies every class,
 * instance method, encoding and size against trusted ABI-typed bindings.
 * Does not execute Mach-O code, relocate binaries, or authenticate IMPs.
 * Class methods require separately validated trusted registrations. */
int phase22d_register_compiled(phase22d_runtime *runtime,
                              const uint8_t *object_file, size_t file_size,
                              const phase22d_class_spec *trusted,
                              size_t count, char *diagnostic, size_t capacity);
#endif
