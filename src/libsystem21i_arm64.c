#include "ios27/libsystem21i_arm64.h"
#include "ios27/libsystem21i_errno.h"
#include <errno.h>
#include <stdint.h>

int ios27_21i_dispatch_arm64(ios27_21i_runtime *runtime,
                             const char *external_macho_symbol,
                             ios27_21i_arm64_registers *registers) {
    int rc;
    if (!runtime || !external_macho_symbol || !registers) return EINVAL;
    rc = ios27_21i_dispatch(runtime, external_macho_symbol,
                           registers->x[0], registers->x[1],
                           registers->x[2]);
    /* An integer ABI return occupies w0; do not expose arbitrary host int
     * width, errno storage, or pthread pointer representation to the guest. */
    registers->x[0] = (uint32_t)ios27_21i_errno_to_darwin(rc);
    return 0;
}

int ios27_21i_dispatch_arm64_n(ios27_21i_runtime *runtime,
                               const char *bytes, size_t length,
                               ios27_21i_arm64_registers *registers) {
    char symbol[64];
    if (!runtime || !bytes || !registers || !length ||
        length >= sizeof(symbol)) return EINVAL;
    for (size_t i = 0; i < length; ++i) {
        unsigned char value = (unsigned char)bytes[i];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_'))
            return EINVAL;
        symbol[i] = (char)value;
    }
    if (symbol[0] != '_') return EINVAL;
    symbol[length] = '\0';
    return ios27_21i_dispatch_arm64(runtime, symbol, registers);
}
