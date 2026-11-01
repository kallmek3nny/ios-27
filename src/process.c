#define _GNU_SOURCE
#include "process_internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__) && (defined(__aarch64__) || defined(__x86_64__))
#define IOS27_SUPERVISOR_HOST 1
#include <fcntl.h>
#include <limits.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#else
#define IOS27_SUPERVISOR_HOST 0
#endif

static int fail(char *error, size_t capacity, const char *message) {
    if (error && capacity) (void)snprintf(error, capacity, "%s", message);
    return -1;
}

static int valid_options(const ios27_process_options *options) {
    return options && options->timeout_ms &&
           options->timeout_ms <= IOS27_PROCESS_MAX_TIMEOUT_MS;
}

#if IOS27_SUPERVISOR_HOST
#if defined(__aarch64__)
#define IOS27_AUDIT_ARCH AUDIT_ARCH_AARCH64
#else
#define IOS27_AUDIT_ARCH AUDIT_ARCH_X86_64
#endif

/* Close-on-exec copy of the trusted loader, consumed by execveat itself. */
#define LOADER_EXEC_FD 5
/* Sources are lifted above the fixed layout before it is assembled. */
#define LOADER_SCRATCH_FD 10
#define LAUNCH_SEALS (F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)
/* Linux 6.3 uapi value; older libc headers may lack it. Older kernels
 * reject the unknown bit with EINVAL, handled at memfd creation. */
#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 0x0008U
#endif

typedef struct {
    uint32_t magic;
    uint32_t kind;
    int32_t value;
} child_report;
_Static_assert(sizeof(child_report) == 12u, "fixed child report size");
_Static_assert(sizeof(ios27_launch_header) == 40u, "fixed launch header size");
_Static_assert(IOS27_LOADER_PAYLOAD_FD == IOS27_LOADER_REPORT_FD + 1 &&
               LOADER_EXEC_FD == IOS27_LOADER_PAYLOAD_FD + 1 &&
               LOADER_SCRATCH_FD > LOADER_EXEC_FD,
               "contiguous loader descriptor layout");

static char loader_name[] = "ios27-loader";
static char *const loader_argv[] = {loader_name, NULL};
static char *const loader_envp[] = {NULL};

static void clear_error(char *error, size_t capacity) {
    if (error && capacity) error[0] = '\0';
}

/* A packet-sized write to the private report pipe is the only permitted I/O.
 * Check the syscall architecture and both halves of each 64-bit argument.
 * General write/read, files, sockets, fork, exec and protection changes die
 * with SIGSYS. A raw exit is allowed but is not a valid LC_MAIN report. */
int ios27_process_install_filter(int report_fd) {
    struct sock_filter code[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, IOS27_AUDIT_ARCH, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_exit, 13, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_exit_group, 12, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_rt_sigreturn, 11, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_write, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)report_fd, 0, 8),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0]) + 4u),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0u, 0, 6),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, sizeof(child_report), 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2]) + 4u),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0u, 1, 2),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS)
    };
    struct sock_fprog program = {
        (unsigned short)(sizeof(code) / sizeof(code[0])), code
    };
    if (report_fd < 0) {
        errno = EBADF;
        return -1;
    }
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program);
}

/* Stage-one guest loader allowlist; see process_internal.h. Instructions
 * are addressed by name: each macro below fills its own designated slot,
 * the compiler derives every jump offset, and a backward or out-of-range
 * jump is a build error (negative array size). */
#ifdef SYS_close_range
#define LF_SYS_CLOSE_RANGE SYS_close_range
#else
/* ios27_process_close_outside then fails with ENOSYS before guest entry,
 * so this slot only repeats an existing allowance. */
#define LF_SYS_CLOSE_RANGE SYS_exit
#endif
#define LF_TO(from, to) \
    ((to) - (from) - 1 + \
     0u * sizeof(char[(to) > (from) && (to) - (from) <= 256 ? 1 : -1]))
#define LF_ARG(n) offsetof(struct seccomp_data, args[n])
#define LF_ARG_HIGH(n) (offsetof(struct seccomp_data, args[n]) + 4u)
#define LF_LOAD(at, offset) [at] = BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offset))
#define LF_RET(at, action) [at] = BPF_STMT(BPF_RET | BPF_K, (action))
/* Equal: jump to target; otherwise fall through to the next test. */
#define LF_IS(at, value, target) \
    [at] = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (value), LF_TO(at, target), 0)
/* Equal: continue with the next instruction; otherwise kill. */
#define LF_NEED(at, value) \
    [at] = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (value), 0, LF_TO(at, LF_KILL))
/* Last argument test: equal allows the call; otherwise kill. */
#define LF_LAST(at, value) \
    [at] = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (value), \
                    LF_TO(at, LF_ALLOW), LF_TO(at, LF_KILL))

enum {
    LF_ARCH, LF_ARCH_IS, LF_NR,
    /* syscall number dispatch */
    LF_EXIT, LF_EXIT_GROUP, LF_SIGRETURN, LF_MUNMAP, LF_SIGPROCMASK,
    LF_CLOSE_RANGE, LF_WRITE, LF_MMAP, LF_MPROTECT, LF_PRCTL, LF_OTHER,
    /* write(fd, buffer, count): one report packet, as in the guest filter */
    LF_WRITE_FD, LF_WRITE_FD_IS, LF_WRITE_FD_HIGH, LF_WRITE_FD_HIGH_IS,
    LF_WRITE_SIZE, LF_WRITE_SIZE_IS, LF_WRITE_SIZE_HIGH, LF_WRITE_SIZE_HIGH_IS,
    /* mmap(addr, length, prot, ...): inaccessible reservations only */
    LF_MMAP_PROT, LF_MMAP_PROT_IS, LF_MMAP_PROT_HIGH, LF_MMAP_PROT_HIGH_IS,
    /* mprotect(addr, length, prot): never write and execute together */
    LF_MPROTECT_HIGH, LF_MPROTECT_HIGH_IS, LF_MPROTECT_PROT,
    LF_MPROTECT_WX, LF_MPROTECT_WX_IS,
    /* prctl(option, ...): option is an int, so only its low word counts */
    LF_PRCTL_OPTION, LF_PRCTL_NO_NEW_PRIVS, LF_PRCTL_SECCOMP,
    LF_ALLOW, LF_KILL, LF_LENGTH
};

int ios27_process_install_loader_filter(int report_fd) {
    struct sock_filter code[LF_LENGTH] = {
        LF_LOAD(LF_ARCH, offsetof(struct seccomp_data, arch)),
        LF_NEED(LF_ARCH_IS, IOS27_AUDIT_ARCH),
        LF_LOAD(LF_NR, offsetof(struct seccomp_data, nr)),
        LF_IS(LF_EXIT, SYS_exit, LF_ALLOW),
        LF_IS(LF_EXIT_GROUP, SYS_exit_group, LF_ALLOW),
        LF_IS(LF_SIGRETURN, SYS_rt_sigreturn, LF_ALLOW),
        LF_IS(LF_MUNMAP, SYS_munmap, LF_ALLOW),
        LF_IS(LF_SIGPROCMASK, SYS_rt_sigprocmask, LF_ALLOW),
        LF_IS(LF_CLOSE_RANGE, LF_SYS_CLOSE_RANGE, LF_ALLOW),
        LF_IS(LF_WRITE, SYS_write, LF_WRITE_FD),
        LF_IS(LF_MMAP, SYS_mmap, LF_MMAP_PROT),
        LF_IS(LF_MPROTECT, SYS_mprotect, LF_MPROTECT_HIGH),
        LF_IS(LF_PRCTL, SYS_prctl, LF_PRCTL_OPTION),
        LF_RET(LF_OTHER, SECCOMP_RET_KILL_PROCESS),
        LF_LOAD(LF_WRITE_FD, LF_ARG(0)),
        LF_NEED(LF_WRITE_FD_IS, (uint32_t)report_fd),
        LF_LOAD(LF_WRITE_FD_HIGH, LF_ARG_HIGH(0)),
        LF_NEED(LF_WRITE_FD_HIGH_IS, 0u),
        LF_LOAD(LF_WRITE_SIZE, LF_ARG(2)),
        LF_NEED(LF_WRITE_SIZE_IS, sizeof(child_report)),
        LF_LOAD(LF_WRITE_SIZE_HIGH, LF_ARG_HIGH(2)),
        LF_LAST(LF_WRITE_SIZE_HIGH_IS, 0u),
        LF_LOAD(LF_MMAP_PROT, LF_ARG(2)),
        LF_NEED(LF_MMAP_PROT_IS, PROT_NONE),
        LF_LOAD(LF_MMAP_PROT_HIGH, LF_ARG_HIGH(2)),
        LF_LAST(LF_MMAP_PROT_HIGH_IS, 0u),
        LF_LOAD(LF_MPROTECT_HIGH, LF_ARG_HIGH(2)),
        LF_NEED(LF_MPROTECT_HIGH_IS, 0u),
        LF_LOAD(LF_MPROTECT_PROT, LF_ARG(2)),
        [LF_MPROTECT_WX] = BPF_STMT(BPF_ALU | BPF_AND | BPF_K,
                                    PROT_WRITE | PROT_EXEC),
        [LF_MPROTECT_WX_IS] = BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                                       PROT_WRITE | PROT_EXEC,
                                       LF_TO(LF_MPROTECT_WX_IS, LF_KILL),
                                       LF_TO(LF_MPROTECT_WX_IS, LF_ALLOW)),
        LF_LOAD(LF_PRCTL_OPTION, LF_ARG(0)),
        LF_IS(LF_PRCTL_NO_NEW_PRIVS, PR_SET_NO_NEW_PRIVS, LF_ALLOW),
        LF_LAST(LF_PRCTL_SECCOMP, PR_SET_SECCOMP),
        LF_RET(LF_ALLOW, SECCOMP_RET_ALLOW),
        LF_RET(LF_KILL, SECCOMP_RET_KILL_PROCESS)
    };
    struct sock_fprog program = {(unsigned short)LF_LENGTH, code};
    if (report_fd < 0) {
        errno = EBADF;
        return -1;
    }
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program);
}

int ios27_process_close_outside(int first, int last) {
    if (first < 0 || last < first) {
        errno = EINVAL;
        return -1;
    }
#ifdef SYS_close_range
    if (first > 0 && syscall(SYS_close_range, 0u, (unsigned)first - 1u, 0u))
        return -1;
    return syscall(SYS_close_range, (unsigned)last + 1u, UINT_MAX, 0u) ? -1 : 0;
#else
    errno = ENOSYS;
    return -1;
#endif
}

int ios27_process_report(int fd, uint32_t kind, int value) {
    child_report report = {IOS27_REPORT_MAGIC, kind, (int32_t)value};
    ssize_t written;
    do {
        written = write(fd, &report, sizeof(report));
    } while (written < 0 && errno == EINTR);
    return written == (ssize_t)sizeof(report) ? 0 : -1;
}

static int write_all(int fd, const void *buffer, size_t size) {
    const uint8_t *cursor = buffer;
    while (size != 0u) {
        size_t chunk = size > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : size;
        ssize_t written = write(fd, cursor, chunk);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return -1;
        cursor += (size_t)written;
        size -= (size_t)written;
    }
    return 0;
}

static int count_vector(const char *const *vector, size_t *count) {
    size_t n = 0u;
    if (vector) {
        while (n <= IOS27_LAUNCH_MAX_VECTOR && vector[n]) ++n;
        if (n > IOS27_LAUNCH_MAX_VECTOR) return -1;
    }
    *count = n;
    return 0;
}

static const char *startup_string(const ios27_startup_context *context,
                                  size_t argc, size_t envc, size_t index) {
    if (index < argc) return context->argv[index];
    if (index < argc + envc) return context->envp[index - argc];
    return context->apple[index - argc - envc];
}

int ios27_launch_payload_create(const uint8_t *data, size_t size,
                                const ios27_startup_context *context,
                                int *fd, char *error, size_t error_size) {
    ios27_launch_header header;
    struct stat st;
    size_t argc, envc, applec, total, strings = 0u;
    int memfd;
    if (!fd) return fail(error, error_size, "null launch payload descriptor");
    *fd = -1;
    if (!data || size == 0u || size > IOS27_LAUNCH_MAX_IMAGE)
        return fail(error, error_size, "invalid launch image size");
    if (!context || context->argc < 0 ||
        context->argc > (int)IOS27_LAUNCH_MAX_VECTOR ||
        (context->argc != 0 && !context->argv))
        return fail(error, error_size, "invalid guest argc/argv");
    argc = (size_t)context->argc;
    if (count_vector(context->envp, &envc))
        return fail(error, error_size, "too many guest environment variables");
    if (count_vector(context->apple, &applec))
        return fail(error, error_size, "too many guest apple arguments");
    total = argc + envc + applec;
    /* Same limits as the guarded guest stack, checked before any I/O. */
    for (size_t i = 0u; i < total; ++i) {
        const char *value = startup_string(context, argc, envc, i);
        const char *end;
        size_t n;
        if (!value) return fail(error, error_size, "null guest argument");
        end = memchr(value, '\0', IOS27_LAUNCH_MAX_STRING);
        if (!end)
            return fail(error, error_size, "guest argument exceeds string limit");
        n = (size_t)(end - value) + 1u;
        if (n > IOS27_LAUNCH_MAX_STRINGS - strings)
            return fail(error, error_size, "guest argument payload too large");
        strings += n;
    }
    memset(&header, 0, sizeof(header));
    header.magic = IOS27_LAUNCH_MAGIC;
    header.version = IOS27_LAUNCH_VERSION;
    header.argc = (uint32_t)argc;
    header.envc = (uint32_t)envc;
    header.applec = (uint32_t)applec;
    header.image_size = (uint64_t)size;
    header.strings_size = (uint64_t)strings;
    /* The payload is only read and copied, never executed in place. A
     * non-executable memfd is accepted under vm.memfd_noexec=2; pre-6.3
     * kernels reject the flag with EINVAL and get the original request. */
    memfd = memfd_create("ios27-launch",
                         MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL);
    if (memfd < 0 && errno == EINVAL)
        memfd = memfd_create("ios27-launch", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (memfd < 0)
        return fail(error, error_size, "launch payload memfd failed");
    if (write_all(memfd, &header, sizeof(header)) ||
        write_all(memfd, data, size))
        goto failed;
    for (size_t i = 0u; i < total; ++i) {
        const char *value = startup_string(context, argc, envc, i);
        if (write_all(memfd, value, strlen(value) + 1u))
            goto failed;
    }
    /* Seal before the payload is shared: neither the launcher nor the child
     * can change, grow or truncate it, and the seal set itself is final. */
    if (fstat(memfd, &st) ||
        (uint64_t)st.st_size != sizeof(header) + (uint64_t)size + strings ||
        fcntl(memfd, F_ADD_SEALS, LAUNCH_SEALS))
        goto failed;
    *fd = memfd;
    clear_error(error, error_size);
    return 0;
failed:
    (void)close(memfd);
    return fail(error, error_size, "launch payload write/seal failed");
}

static int take_strings(const char **cursor, const char *limit,
                        uint32_t count, const char **out) {
    for (uint32_t i = 0u; i < count; ++i) {
        size_t remaining = (size_t)(limit - *cursor);
        size_t span = remaining < IOS27_LAUNCH_MAX_STRING ?
                      remaining : IOS27_LAUNCH_MAX_STRING;
        const char *end = span ? memchr(*cursor, '\0', span) : NULL;
        if (!end) return -1;
        out[i] = *cursor;
        *cursor = end + 1;
    }
    out[count] = NULL;
    return 0;
}

int ios27_launch_payload_open(int fd, ios27_launch_payload *payload) {
    ios27_launch_header header;
    struct stat st;
    const char *cursor, *limit;
    const char **argv, **envp, **apple;
    void *mapped;
    int seals;
    if (!payload) {
        errno = EINVAL;
        return -1;
    }
    memset(payload, 0, sizeof(*payload));
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    /* Only a completely sealed memfd is accepted: its contents cannot change
     * between validation and use, even through another descriptor. */
    seals = fcntl(fd, F_GET_SEALS);
    if (seals < 0) return -1;
    if ((seals & LAUNCH_SEALS) != LAUNCH_SEALS) {
        errno = EPERM;
        return -1;
    }
    if (fstat(fd, &st)) return -1;
    if (!S_ISREG(st.st_mode) || st.st_size < (off_t)sizeof(header) ||
        (uint64_t)st.st_size > (uint64_t)sizeof(header) +
                               IOS27_LAUNCH_MAX_IMAGE + IOS27_LAUNCH_MAX_STRINGS) {
        errno = EINVAL;
        return -1;
    }
    mapped = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) return -1;
    payload->mapping = mapped;
    payload->mapping_size = (size_t)st.st_size;
    memcpy(&header, payload->mapping, sizeof(header));
    /* Bound every field before the exact-size sum, so it cannot overflow. */
    if (header.magic != IOS27_LAUNCH_MAGIC ||
        header.version != IOS27_LAUNCH_VERSION || header.reserved != 0u ||
        header.argc > IOS27_LAUNCH_MAX_VECTOR ||
        header.envc > IOS27_LAUNCH_MAX_VECTOR ||
        header.applec > IOS27_LAUNCH_MAX_VECTOR ||
        header.image_size == 0u || header.image_size > IOS27_LAUNCH_MAX_IMAGE ||
        header.strings_size > IOS27_LAUNCH_MAX_STRINGS ||
        (uint64_t)sizeof(header) + header.image_size + header.strings_size !=
            (uint64_t)st.st_size)
        goto invalid;
    payload->image = payload->mapping + sizeof(header);
    payload->image_size = (size_t)header.image_size;
    cursor = (const char *)(payload->image + payload->image_size);
    limit = cursor + (size_t)header.strings_size;
    argv = payload->vectors;
    envp = argv + header.argc + 1u;
    apple = envp + header.envc + 1u;
    if (take_strings(&cursor, limit, header.argc, argv) ||
        take_strings(&cursor, limit, header.envc, envp) ||
        take_strings(&cursor, limit, header.applec, apple) ||
        cursor != limit)
        goto invalid;
    payload->context.argc = (int)header.argc;
    payload->context.argv = argv;
    payload->context.envp = envp;
    payload->context.apple = apple;
    return 0;
invalid:
    ios27_launch_payload_close(payload);
    errno = EINVAL;
    return -1;
}

void ios27_launch_payload_close(ios27_launch_payload *payload) {
    if (!payload) return;
    if (payload->mapping) (void)munmap(payload->mapping, payload->mapping_size);
    memset(payload, 0, sizeof(*payload));
}

static void report_and_exit(int fd, uint32_t kind, int value) {
    int sent = ios27_process_report(fd, kind, value);
    _exit(!sent && kind == IOS27_REPORT_RETURN ? 0 : 125);
}

/* Shared child prelude. No allocation, stdio, guest loading or host mutex
 * use after fork: everything the child needs was prepared beforehand. */
static void child_reset(int fd, pid_t parent) {
    struct sigaction action = {0};
    struct rlimit core = {0u, 0u};
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask))
        report_and_exit(fd, IOS27_REPORT_SETUP, errno);
    /* Signals stay blocked across fork and disposition reset. In particular,
     * never invoke a launcher callback or inherit SIG_IGN in the guest.
     * libc reserves internal signal numbers for which sigaction is EINVAL. */
    for (int signal = 1; signal < NSIG; ++signal) {
        if (signal == SIGKILL || signal == SIGSTOP) continue;
        if (sigaction(signal, &action, NULL) && errno != EINVAL)
            report_and_exit(fd, IOS27_REPORT_SETUP, errno);
    }
    /* Reset the persona to plain PER_LINUX (0). An inherited
     * READ_IMPLIES_EXEC lets PROT_READ in mmap and mprotect imply PROT_EXEC
     * after the syscall filter has checked the requested protection, and an
     * ordinary execveat keeps ADDR_NO_RANDOMIZE, ADDR_COMPAT_LAYOUT and
     * MMAP_PAGE_ZERO and applies them to the loader's new address space.
     * The loader reads the persona back. */
    if (setrlimit(RLIMIT_CORE, &core) ||
        prctl(PR_SET_PDEATHSIG, SIGKILL, 0ul, 0ul, 0ul) ||
        syscall(SYS_personality, 0ul) < 0)
        report_and_exit(fd, IOS27_REPORT_SETUP, errno);
    if (getppid() != parent) _exit(125);
}

/* Fork mode: executable/stack mappings and guest arguments already exist. */
static void child_run(int fd, pid_t parent, int (*entry)(void *), void *context) {
    sigset_t empty;
    child_reset(fd, parent);
    if (sigemptyset(&empty) || ios27_process_close_outside(fd, fd) ||
        prctl(PR_SET_NO_NEW_PRIVS, 1ul, 0ul, 0ul, 0ul) ||
        sigprocmask(SIG_SETMASK, &empty, NULL) ||
        ios27_process_install_filter(fd))
        report_and_exit(fd, IOS27_REPORT_SETUP, errno);
    /* A fault before this packet is a setup failure, not guest execution. */
    if (ios27_process_report(fd, IOS27_REPORT_READY, 0)) _exit(125);
    report_and_exit(fd, IOS27_REPORT_RETURN, entry(context));
}

/* execveat applies the RLIMIT_STACK value saved when the exec began
 * (bprm_mm_init in fs/exec.c) to the loader's new address space, and an
 * ordinary exec never lowers it; only a secureexec exec clamps the soft
 * limit to _STK_LIM. With an unlimited soft limit, the generic top-down
 * layout that arm64 uses (mmap_is_legacy in mm/util.c) falls back to the
 * legacy bottom-up mmap base, and x86_64 sizes the gap below the stack from
 * the limit. Apply the secureexec clamp here instead. Only the soft limit
 * is lowered and the hard limit is kept, so this needs no privilege. The
 * buffer starts as unlimited, so a getrlimit that claims success without
 * writing it still leads to the clamp. The loader reads the limit back. */
static int clamp_stack_limit(void) {
    struct rlimit stack = {RLIM_INFINITY, RLIM_INFINITY};
    if (getrlimit(RLIMIT_STACK, &stack)) return -1;
    if (stack.rlim_cur != RLIM_INFINITY &&
        stack.rlim_cur <= IOS27_LOADER_STACK_LIMIT)
        return 0;
    stack.rlim_cur = (rlim_t)IOS27_LOADER_STACK_LIMIT;
    return setrlimit(RLIMIT_STACK, &stack);
}

/* Exec mode: assemble the fixed loader layout (report pipe on 3, sealed
 * payload on 4), drop every other descriptor and replace the address space
 * with the trusted loader, using an empty environment. The launcher's full
 * signal mask stays blocked across exec; the loader clears it immediately
 * before installing its own filter and reporting readiness. The stack-limit
 * clamp applies only here, because the fork child keeps the launcher's
 * existing address space. */
static void child_exec(int fd, pid_t parent, int payload, int loader) {
    int report, staged_payload = -1, staged_loader = -1;
    child_reset(fd, parent);
    if (clamp_stack_limit()) report_and_exit(fd, IOS27_REPORT_SETUP, errno);
    /* Lift every source above the layout first, so a descriptor already
     * occupying 3..5 cannot be replaced before it has been copied. */
    report = fcntl(fd, F_DUPFD_CLOEXEC, LOADER_SCRATCH_FD);
    if (report >= 0)
        staged_payload = fcntl(payload, F_DUPFD_CLOEXEC, LOADER_SCRATCH_FD);
    if (staged_payload >= 0)
        staged_loader = fcntl(loader, F_DUPFD_CLOEXEC, LOADER_SCRATCH_FD);
    if (staged_loader < 0 || dup3(report, IOS27_LOADER_REPORT_FD, 0) < 0)
        report_and_exit(fd, IOS27_REPORT_SETUP, errno);
    if (dup3(staged_payload, IOS27_LOADER_PAYLOAD_FD, 0) < 0 ||
        dup3(staged_loader, LOADER_EXEC_FD, O_CLOEXEC) < 0 ||
        ios27_process_close_outside(IOS27_LOADER_REPORT_FD, LOADER_EXEC_FD) ||
        prctl(PR_SET_NO_NEW_PRIVS, 1ul, 0ul, 0ul, 0ul))
        report_and_exit(IOS27_LOADER_REPORT_FD, IOS27_REPORT_SETUP, errno);
#ifdef SYS_execveat
    (void)syscall(SYS_execveat, LOADER_EXEC_FD, "", loader_argv, loader_envp,
                  AT_EMPTY_PATH);
#else
    errno = ENOSYS;
#endif
    report_and_exit(IOS27_LOADER_REPORT_FD, IOS27_REPORT_SETUP, errno);
}

static int now_ms(uint64_t *milliseconds) {
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) || time.tv_sec < 0 ||
        (uint64_t)time.tv_sec > (UINT64_MAX - 1000u) / 1000u)
        return -1;
    *milliseconds = (uint64_t)time.tv_sec * 1000u + (uint64_t)time.tv_nsec / 1000000u;
    return 0;
}

static int kill_and_reap(pid_t child, int *status) {
    pid_t waited;
    if (kill(child, SIGKILL) && errno != ESRCH) return -1;
    do {
        waited = waitpid(child, status, 0);
    } while (waited < 0 && errno == EINTR);
    return waited == child ? 0 : -1;
}

/* Positive SETUP values are errno; negative ones are loader stage codes. */
static void describe_setup(int32_t value, char *error, size_t error_size) {
    const char *stage;
    if (!error || !error_size) return;
    switch (value) {
    case IOS27_LOADER_E_PAYLOAD:
        stage = "guest loader rejected the launch payload";
        break;
    case IOS27_LOADER_E_IMAGE:
        stage = "guest loader rejected the Mach-O image";
        break;
    case IOS27_LOADER_E_ARCH:
        stage = "guest loader requires ARM64 Linux/Android";
        break;
    case IOS27_LOADER_E_PREPARE:
        stage = "guest loader image preparation failed";
        break;
    default:
        (void)snprintf(error, error_size,
                       "child containment setup failed: errno=%d", (int)value);
        return;
    }
    (void)snprintf(error, error_size, "%s", stage);
}

typedef struct {
    int (*entry)(void *); /* fork mode when non-NULL, otherwise exec mode */
    void *context;
    int payload_fd;
    int loader_fd;
} child_plan;

static int supervise(const child_plan *plan,
                     const ios27_process_options *options,
                     ios27_process_result *result,
                     char *error, size_t error_size) {
    struct sigaction sigchld;
    sigset_t blocked, previous;
    uint64_t started, deadline;
    int pipefd[2], status;
    pid_t child, parent = getpid();
    child_report ready, report;
    uint8_t packet[2u * sizeof(report) + 1u];
    ssize_t received;
    if (sigaction(SIGCHLD, NULL, &sigchld) ||
        sigchld.sa_handler != SIG_DFL || (sigchld.sa_flags & SA_NOCLDWAIT))
        return fail(error, error_size, "child supervision requires default SIGCHLD");
    if (now_ms(&started) || UINT64_MAX - started < options->timeout_ms)
        return fail(error, error_size, "monotonic child clock failed");
    deadline = started + options->timeout_ms;
    if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK))
        return fail(error, error_size, "child report pipe failed");
    if (sigfillset(&blocked) || sigprocmask(SIG_BLOCK, &blocked, &previous)) {
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        return fail(error, error_size, "launcher signal blocking failed");
    }
    child = fork();
    if (child == 0) {
        if (plan->entry)
            child_run(pipefd[1], parent, plan->entry, plan->context);
        else
            child_exec(pipefd[1], parent, plan->payload_fd, plan->loader_fd);
        _exit(125);
    }
    (void)close(pipefd[1]);
    if (sigprocmask(SIG_SETMASK, &previous, NULL)) {
        if (child > 0) (void)kill_and_reap(child, &status);
        (void)close(pipefd[0]);
        return fail(error, error_size, "launcher signal restoration failed");
    }
    if (child < 0) {
        (void)close(pipefd[0]);
        return fail(error, error_size, "native child fork failed");
    }
    for (;;) {
        uint64_t current, remaining;
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (waited < 0 && errno != EINTR) {
            /* A competing reaper may have consumed it: do not signal a PID
             * which is no longer our waitable child. */
            (void)close(pipefd[0]);
            return fail(error, error_size, "native child wait failed");
        }
        if (now_ms(&current)) {
            (void)kill_and_reap(child, &status);
            (void)close(pipefd[0]);
            return fail(error, error_size, "monotonic child clock failed");
        }
        if (current >= deadline) {
            int reaped = kill_and_reap(child, &status);
            (void)close(pipefd[0]);
            if (reaped) return fail(error, error_size, "child deadline cleanup failed");
            result->outcome = IOS27_PROCESS_TIMED_OUT;
            result->signal = SIGKILL;
            clear_error(error, error_size);
            return 0;
        }
        remaining = deadline - current;
        if (poll(NULL, 0u, (int)(remaining < 5u ? remaining : 5u)) < 0 && errno != EINTR) {
            (void)kill_and_reap(child, &status);
            (void)close(pipefd[0]);
            return fail(error, error_size, "native child polling failed");
        }
    }
    do {
        received = read(pipefd[0], packet, sizeof(packet));
    } while (received < 0 && errno == EINTR);
    (void)close(pipefd[0]);
    if (received == (ssize_t)sizeof(report)) {
        memcpy(&report, packet, sizeof(report));
        if (report.magic == IOS27_REPORT_MAGIC && report.kind == IOS27_REPORT_SETUP &&
            WIFEXITED(status) && WEXITSTATUS(status) == 125) {
            describe_setup(report.value, error, error_size);
            return -1;
        }
    }
    if (received < (ssize_t)sizeof(ready))
        return fail(error, error_size, "child failed before containment was ready");
    memcpy(&ready, packet, sizeof(ready));
    if (ready.magic != IOS27_REPORT_MAGIC || ready.kind != IOS27_REPORT_READY ||
        ready.value != 0)
        return fail(error, error_size, "invalid child readiness packet");
    if (received == (ssize_t)(2u * sizeof(report))) {
        memcpy(&report, packet + sizeof(ready), sizeof(report));
        if (report.magic != IOS27_REPORT_MAGIC || report.kind != IOS27_REPORT_RETURN)
            return fail(error, error_size, "invalid child result packet");
    }
    if (WIFSIGNALED(status)) {
        if (received != (ssize_t)sizeof(ready) && received != (ssize_t)(2u * sizeof(report)))
            return fail(error, error_size, "invalid signaled child result");
        result->outcome = IOS27_PROCESS_SIGNALED;
        result->signal = WTERMSIG(status);
    } else {
        if (!WIFEXITED(status) || received != (ssize_t)(2u * sizeof(report)))
            return fail(error, error_size, "child exited without a complete result");
        if (report.kind != IOS27_REPORT_RETURN || WEXITSTATUS(status) != 0)
            return fail(error, error_size, "invalid child return status");
        result->outcome = IOS27_PROCESS_RETURNED;
        result->exit_code = (int)report.value;
    }
    clear_error(error, error_size);
    return 0;
}

int ios27_process_supervise(int (*entry)(void *), void *context,
                            const ios27_process_options *options,
                            ios27_process_result *result,
                            char *error, size_t error_size) {
    child_plan plan = {entry, context, -1, -1};
    if (!result) return fail(error, error_size, "null child result");
    memset(result, 0, sizeof(*result));
    if (!entry || !valid_options(options))
        return fail(error, error_size, "invalid child entry or deadline");
    return supervise(&plan, options, result, error, error_size);
}

int ios27_process_supervise_exec(int loader_fd, int payload_fd,
                                 const ios27_process_options *options,
                                 ios27_process_result *result,
                                 char *error, size_t error_size) {
    child_plan plan = {NULL, NULL, payload_fd, loader_fd};
    if (!result) return fail(error, error_size, "null child result");
    memset(result, 0, sizeof(*result));
    if (loader_fd < 0 || payload_fd < 0 || loader_fd == payload_fd ||
        !valid_options(options))
        return fail(error, error_size, "invalid guest loader descriptors or deadline");
    return supervise(&plan, options, result, error, error_size);
}
#else
int ios27_process_report(int fd, uint32_t kind, int value) {
    (void)fd; (void)kind; (void)value;
    errno = ENOSYS;
    return -1;
}

int ios27_process_close_outside(int first, int last) {
    (void)first; (void)last;
    errno = ENOSYS;
    return -1;
}

int ios27_process_install_filter(int report_fd) {
    (void)report_fd;
    errno = ENOSYS;
    return -1;
}

int ios27_process_install_loader_filter(int report_fd) {
    (void)report_fd;
    errno = ENOSYS;
    return -1;
}

int ios27_launch_payload_create(const uint8_t *data, size_t size,
                                const ios27_startup_context *context,
                                int *fd, char *error, size_t error_size) {
    (void)data; (void)size; (void)context;
    if (fd) *fd = -1;
    return fail(error, error_size, "launch payloads require Linux ARM64/x86_64");
}

int ios27_launch_payload_open(int fd, ios27_launch_payload *payload) {
    (void)fd;
    if (payload) memset(payload, 0, sizeof(*payload));
    errno = ENOSYS;
    return -1;
}

void ios27_launch_payload_close(ios27_launch_payload *payload) {
    if (payload) memset(payload, 0, sizeof(*payload));
}

int ios27_process_supervise(int (*entry)(void *), void *context,
                            const ios27_process_options *options,
                            ios27_process_result *result,
                            char *error, size_t error_size) {
    if (!result) return fail(error, error_size, "null child result");
    memset(result, 0, sizeof(*result));
    if (!entry || !valid_options(options))
        return fail(error, error_size, "invalid child entry or deadline");
    (void)context;
    return fail(error, error_size, "child supervision requires Linux ARM64/x86_64");
}

int ios27_process_supervise_exec(int loader_fd, int payload_fd,
                                 const ios27_process_options *options,
                                 ios27_process_result *result,
                                 char *error, size_t error_size) {
    if (!result) return fail(error, error_size, "null child result");
    memset(result, 0, sizeof(*result));
    if (loader_fd < 0 || payload_fd < 0 || loader_fd == payload_fd ||
        !valid_options(options))
        return fail(error, error_size, "invalid guest loader descriptors or deadline");
    return fail(error, error_size, "child supervision requires Linux ARM64/x86_64");
}
#endif

int ios27_run_executable_exec(const char *loader_path,
                              const uint8_t *data, size_t size,
                              const ios27_macho_info *info,
                              const ios27_startup_context *context,
                              const ios27_process_options *options,
                              ios27_process_result *result,
                              char *error, size_t error_size) {
    if (!result) return fail(error, error_size, "null child result");
    memset(result, 0, sizeof(*result));
    if (!valid_options(options))
        return fail(error, error_size, "invalid child deadline");
    if (!loader_path || !loader_path[0] || !data || !info || !context)
        return fail(error, error_size, "invalid native runtime input");
#if !IOS27_SUPERVISOR_HOST || !defined(__aarch64__)
    (void)size;
    return fail(error, error_size, "execution requires ARM64 Linux/Android");
#else
    ios27_exec_layout layout;
    struct stat st;
    long page = sysconf(_SC_PAGESIZE);
    int loader, payload, status;
    if (page <= 0)
        return fail(error, error_size, "invalid native runtime input");
    /* Reject stale metadata or unsupported layouts before spawning; the
     * loader repeats parsing and layout checks on its own sealed copy. */
    if (ios27_macho_executable_layout(data, size, info, (size_t)page,
                                      &layout, error, error_size))
        return -1;
    loader = open(loader_path, O_RDONLY | O_CLOEXEC);
    if (loader < 0)
        return fail(error, error_size, "guest loader open failed");
    if (fstat(loader, &st) || !S_ISREG(st.st_mode)) {
        (void)close(loader);
        return fail(error, error_size, "guest loader is not a regular file");
    }
    if (ios27_launch_payload_create(data, size, context, &payload,
                                    error, error_size)) {
        (void)close(loader);
        return -1;
    }
    status = ios27_process_supervise_exec(loader, payload, options, result,
                                          error, error_size);
    (void)close(payload);
    (void)close(loader);
    return status;
#endif
}
