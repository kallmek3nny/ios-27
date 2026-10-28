#ifndef IOS27_LIBSYSTEM21I_ARM64_H
#define IOS27_LIBSYSTEM21I_ARM64_H
#include "ios27/libsystem21i_abi.h"
#include <stdint.h>

/* AArch64 general-purpose argument snapshot, deliberately separate from a
 * guest CPU emulator. Only the low 32 bits of x0 (the w0 pthread int result)
 * are meaningful on return. Other registers are left untouched. */
typedef struct {
    uint64_t x[8];
} ios27_21i_arm64_registers;

/* Successful bridge dispatch returns 0 and puts pthread's errno result in
 * guest w0 using verified Darwin errno numbers (never Linux errno codes).
 * API validation errors return host EINVAL without mutating registers.
 * The caller must resolve the Mach-O external symbol and supply guest x0-x2. */
int ios27_21i_dispatch_arm64(ios27_21i_runtime *runtime,
                             const char *external_macho_symbol,
                             ios27_21i_arm64_registers *registers);
#endif
