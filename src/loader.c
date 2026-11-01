#define _GNU_SOURCE
#include "process_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

/* ios27-loader: trusted helper that ios27_run_executable_exec execveat()s
 * into a fresh address space. It inherits only the report pipe on fd 3, the
 * sealed launch payload on fd 4, an empty environment and a fully blocked
 * signal mask, so no launcher heap, stack or mapping is visible to the
 * guest. Before reading the payload it clears its dumpable attribute and
 * enters a deny-all Landlock domain when the kernel provides one. Once the
 * payload is mapped and its
 * descriptor closed, it installs a stage-one seccomp allowlist before the
 * image is re-parsed. Like the fork runner this is trusted-fixture failure
 * containment, NOT a sandbox for untrusted Mach-O code. */

static ios27_launch_payload payload;

/* Landlock uapi subset from linux/landlock.h, defined here so older headers
 * still build. x86_64 and the generic arm64 syscall table share these
 * numbers. */
#ifndef SYS_landlock_create_ruleset
#define SYS_landlock_create_ruleset 444
#endif
#ifndef SYS_landlock_restrict_self
#define SYS_landlock_restrict_self 446
#endif
#define LOADER_LANDLOCK_VERSION 1ul /* LANDLOCK_CREATE_RULESET_VERSION */
/* EXECUTE through MAKE_SYM (ABI 1), REFER (2), TRUNCATE (3), IOCTL_DEV (5);
 * TCP bind and connect (4); abstract UNIX socket and signal scopes (6).
 * Rights added by later ABI versions are not requested. */
#define LOADER_LANDLOCK_FS_ABI1 ((UINT64_C(1) << 13) - 1u)
#define LOADER_LANDLOCK_FS_REFER (UINT64_C(1) << 13)
#define LOADER_LANDLOCK_FS_TRUNCATE (UINT64_C(1) << 14)
#define LOADER_LANDLOCK_FS_IOCTL_DEV (UINT64_C(1) << 15)
#define LOADER_LANDLOCK_NET_TCP UINT64_C(0x3)
#define LOADER_LANDLOCK_SCOPES UINT64_C(0x3)

typedef struct {
    uint64_t handled_access_fs;
    uint64_t handled_access_net;
    uint64_t scoped;
} loader_landlock_ruleset_attr;
_Static_assert(sizeof(loader_landlock_ruleset_attr) == 24u,
               "Landlock ruleset attribute layout");

/* Enter a Landlock domain without rules, so every handled right is denied
 * to the loader and to the guest that later runs in this process.
 * Descriptors opened earlier (the report pipe and the sealed payload) stay
 * usable. Best effort, as the kernel documentation recommends: ENOSYS or
 * EOPNOTSUPP from the version query means Landlock is absent or disabled,
 * and the mandatory seccomp filter still applies before guest entry. Any
 * other failure, or a directory open that still succeeds afterwards, is a
 * setup error. */
static int enter_landlock_domain(void) {
    loader_landlock_ruleset_attr attr = {0};
    long abi, ruleset;
    int saved, probe;
    abi = syscall(SYS_landlock_create_ruleset, NULL, 0ul, LOADER_LANDLOCK_VERSION);
    if (abi < 0)
        return errno == ENOSYS || errno == EOPNOTSUPP ? 0 : -1;
    attr.handled_access_fs = LOADER_LANDLOCK_FS_ABI1;
    if (abi >= 2) attr.handled_access_fs |= LOADER_LANDLOCK_FS_REFER;
    if (abi >= 3) attr.handled_access_fs |= LOADER_LANDLOCK_FS_TRUNCATE;
    if (abi >= 4) attr.handled_access_net = LOADER_LANDLOCK_NET_TCP;
    if (abi >= 5) attr.handled_access_fs |= LOADER_LANDLOCK_FS_IOCTL_DEV;
    if (abi >= 6) attr.scoped = LOADER_LANDLOCK_SCOPES;
    ruleset = syscall(SYS_landlock_create_ruleset, &attr, sizeof(attr), 0ul);
    if (ruleset < 0) return -1;
    /* Unprivileged enforcement needs no_new_privs. The supervisor set it
     * before execveat and it survives exec; this only reasserts it. */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1ul, 0ul, 0ul, 0ul) ||
        syscall(SYS_landlock_restrict_self, ruleset, 0ul)) {
        saved = errno;
        (void)close((int)ruleset);
        errno = saved;
        return -1;
    }
    if (close((int)ruleset)) return -1;
    /* Confirm enforcement: reading any directory is now refused. */
    probe = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (probe >= 0) {
        (void)close(probe);
        errno = EPERM;
        return -1;
    }
    return errno == EACCES ? 0 : -1;
}

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

/* execveat of a readable program by a caller whose real and effective IDs
 * match resets the dumpable attribute to 1 (begin_new_exec in fs/exec.c).
 * While it is 1, same-UID processes pass the ptrace access checks for this
 * process, subject to Yama, and those checks govern PTRACE_ATTACH and
 * /proc/<pid>/mem. Clearing it makes the /proc/<pid> files root-owned and
 * refuses that access to callers without CAP_SYS_PTRACE, for the loader and
 * for the guest that later runs in this process. The stage-one filter
 * forbids this prctl option, so it runs first. The value is read back, and
 * a set that did not take effect is a setup error. */
static int clear_dumpable(void) {
    int dumpable;
    if (prctl(PR_SET_DUMPABLE, 0ul, 0ul, 0ul, 0ul)) return -1;
    dumpable = prctl(PR_GET_DUMPABLE, 0ul, 0ul, 0ul, 0ul);
    if (dumpable < 0) return -1;
    if (dumpable != 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

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
    /* Core dumps and same-UID ptrace access end before any payload byte
     * is read. */
    if (setrlimit(RLIMIT_CORE, &core) || clear_dumpable() ||
        enter_landlock_domain())
        loader_exit(IOS27_REPORT_SETUP, errno);
    if (ios27_launch_payload_open(IOS27_LOADER_PAYLOAD_FD, &payload))
        loader_exit(IOS27_REPORT_SETUP, IOS27_LOADER_E_PAYLOAD);
    /* The read-only private mapping outlives its descriptor. With only the
     * report pipe left, stage one of the syscall filter goes on before any
     * image byte is re-parsed; see ios27_process_install_loader_filter.
     * no_new_privs was set by the supervisor and survives exec, but
     * enter_landlock_domain reasserts it only when Landlock is present. */
    if (close(IOS27_LOADER_PAYLOAD_FD) ||
        prctl(PR_SET_NO_NEW_PRIVS, 1ul, 0ul, 0ul, 0ul) ||
        ios27_process_install_loader_filter(IOS27_LOADER_REPORT_FD))
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
