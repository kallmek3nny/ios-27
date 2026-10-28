#include "ios27/libsystem21i_arm64.h"
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
    registers->x[0] = (uint32_t)rc;
    return 0;
}
