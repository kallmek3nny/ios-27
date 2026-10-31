#ifndef IOS27_PROCESS_INTERNAL_H
#define IOS27_PROCESS_INTERNAL_H

#include "ios27/process.h"

/* Private supervisor boundary. entry must use only prepared private memory
 * and return normally; the child cannot allocate or call general host libc.
 * Exposed to native supervisor tests, not to guest import resolution. */
int ios27_process_supervise(int (*entry)(void *), void *context,
                            const ios27_process_options *options,
                            ios27_process_result *result,
                            char *error, size_t error_size);

#endif
