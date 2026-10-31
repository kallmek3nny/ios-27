#define _GNU_SOURCE
#include "process_internal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

/* ios27-loader: trusted helper that ios27_run_executable_exec execveat()s
 * into a fresh address space. It inherits only the report pipe on fd 3, the
 * sealed launch payload on fd 4, an empty environment and a fully blocked
 * signal mask, so no launcher heap, stack or mapping is visible to the
 * guest. Like the fork runner this is trusted-fixture failure containment,
 * NOT a sandbox for untrusted Mach-O code. */

static ios27_launch_payload payload;

static _Noreturn void loader_exit(uint32_t kind, int value) {
    int sent = ios27_process_report(IOS27_LOADER_REPORT_FD, kind, value);
    _exit(!sent && kind == IOS27_REPORT_RETURN ? 0 : 125);
}

#if defined(__aarch64__)
/* Runs once the image has its final W^X mappings and the guarded guest stack
 * holds private copies of argv/envp/apple, so the sealed payload is no
 * longer needed. Same containment order as the fork child, then LC_MAIN. */
static _Noreturn void enter_guest(int (*entry)(void *), void *call) {
    sigset_t empty;
    ios27_launch_payload_close(&payload);
    if (sigemptyset(&empty) ||
        ios27_process_close_outside(IOS27_LOADER_REPORT_FD,
                                    IOS27_LOADER_REPORT_FD) ||
        prctl(PR_SET_NO_NEW_PRIVS, 1ul, 0ul, 0ul, 0ul) ||
        sigprocmask(SIG_SETMASK, &empty, NULL) ||
        ios27_process_install_filter(IOS27_LOADER_REPORT_FD))
        loader_exit(IOS27_REPORT_SETUP, errno);
    /* A fault before this packet is a setup failure, not guest execution. */
    if (ios27_process_report(IOS27_LOADER_REPORT_FD, IOS27_REPORT_READY, 0))
        _exit(125);
    loader_exit(IOS27_REPORT_RETURN, entry(call));
}
#endif

int main(int argc, char **argv) {
    struct stat report;
    struct rlimit core = {0u, 0u};
    (void)argv;
    /* Never adopt descriptors 3 and 4 unless started by the supervisor. */
    if (argc != 1 || fstat(IOS27_LOADER_REPORT_FD, &report) ||
        !S_ISFIFO(report.st_mode)) {
        fputs("ios27-loader: internal helper, started only by "
              "ios27_run_executable_exec\n", stderr);
        return 2;
    }
    if (setrlimit(RLIMIT_CORE, &core))
        loader_exit(IOS27_REPORT_SETUP, errno);
    if (ios27_launch_payload_open(IOS27_LOADER_PAYLOAD_FD, &payload))
        loader_exit(IOS27_REPORT_SETUP, IOS27_LOADER_E_PAYLOAD);
    /* The read-only private mapping outlives its descriptor. */
    if (close(IOS27_LOADER_PAYLOAD_FD))
        loader_exit(IOS27_REPORT_SETUP, errno);
#if !defined(__aarch64__)
    loader_exit(IOS27_REPORT_SETUP, IOS27_LOADER_E_ARCH);
#else
    {
        ios27_macho_info info;
        ios27_exec_layout layout;
        char error[192];
        long page = sysconf(_SC_PAGESIZE);
        /* Launcher metadata is never trusted across exec: re-parse and
         * re-check the layout of the sealed copy before mapping it. */
        if (page <= 0 ||
            ios27_macho_parse(payload.image, payload.image_size, &info,
                              error, sizeof(error)) ||
            ios27_macho_executable_layout(payload.image, payload.image_size,
                                          &info, (size_t)page, &layout,
                                          error, sizeof(error)))
            loader_exit(IOS27_REPORT_SETUP, IOS27_LOADER_E_IMAGE);
        (void)ios27_native_handoff(payload.image, payload.image_size, &info,
                                   &payload.context, enter_guest,
                                   error, sizeof(error));
        loader_exit(IOS27_REPORT_SETUP, IOS27_LOADER_E_PREPARE);
    }
#endif
}
