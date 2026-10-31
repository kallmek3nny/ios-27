#ifndef IOS27_PROCESS_H
#define IOS27_PROCESS_H

#include "ios27/macho.h"

#define IOS27_PROCESS_MAX_TIMEOUT_MS 60000u

typedef struct {
    uint32_t timeout_ms; /* 1..60000, including child setup time */
} ios27_process_options;

typedef enum {
    IOS27_PROCESS_RETURNED = 1,
    IOS27_PROCESS_SIGNALED = 2,
    IOS27_PROCESS_TIMED_OUT = 3
} ios27_process_outcome;

typedef struct {
    ios27_process_outcome outcome;
    int exit_code;   /* full C int returned by LC_MAIN, not an 8-bit wait status */
    int signal;     /* terminating signal, or SIGKILL after a deadline */
} ios27_process_result;

/* Run a reviewed, freestanding ARM64 Mach-O in a supervised Linux child.
 * The existing parser, guarded Darwin argument stack and W^X loader preflight
 * run before fork. The child closes inherited descriptors, disables core
 * dumps, sets no_new_privs and installs a mandatory syscall filter before
 * entering LC_MAIN. Child signal dispositions/masks are reset; the launcher
 * keeps its original signal state. A readiness packet confirms containment
 * setup before entry. The parent enforces a monotonic deadline and reaps it.
 *
 * Return 0: result describes normal return, a signal after readiness, or a
 * timeout (including setup time). A signal between readiness and the first
 * guest instruction is also a signaled outcome.
 * Return -1: preflight, host setup, unsupported kernel/CPU or report failure;
 * result is cleared. The caller owns input buffers for the whole call.
 *
 * This is trusted-fixture failure containment, NOT an untrusted-code sandbox:
 * fork inherits host memory, shared mappings and the host address space ABI.
 * Only use from a single-threaded launcher with default SIGCHLD disposition
 * and no competing child reaper. No guest threads, Darwin syscalls, general
 * libc imports, dyld, Apple frameworks or device compatibility are provided.
 * Linux close_range and seccomp support are required; setup fails closed.
 */
int ios27_run_executable_contained(const uint8_t *data, size_t size,
                                    const ios27_macho_info *info,
                                    const ios27_startup_context *context,
                                    const ios27_process_options *options,
                                    ios27_process_result *result,
                                    char *error, size_t error_size);

/* Same contract, outcomes and status codes as
 * ios27_run_executable_contained, but the guest runs in a fresh address
 * space instead of a fork of the launcher. The launcher repeats the Mach-O
 * layout preflight, copies the image and startup strings into a memfd sealed
 * against write, grow, shrink and further sealing, and forks a child which
 * resets signals, disables core dumps, keeps only the report pipe and the
 * payload, sets no_new_privs and execveat()s the trusted loader with an
 * empty environment. The loader re-parses its sealed copy, maps it with
 * final W^X protections, unmaps the payload, installs the same mandatory
 * syscall filter and reports readiness before entering LC_MAIN.
 *
 * loader_path must name the project's own trusted ios27-loader build output;
 * it is opened once and executed by descriptor. Not inheriting launcher
 * memory narrows what a faulty fixture can observe, but this is still
 * trusted-fixture failure containment, NOT a sandbox for untrusted Mach-O
 * code. Requires ARM64 Linux with memfd sealing, execveat, close_range and
 * seccomp; setup fails closed otherwise. The single-threaded launcher and
 * default SIGCHLD requirements above also apply.
 */
int ios27_run_executable_exec(const char *loader_path,
                              const uint8_t *data, size_t size,
                              const ios27_macho_info *info,
                              const ios27_startup_context *context,
                              const ios27_process_options *options,
                              ios27_process_result *result,
                              char *error, size_t error_size);

#endif
