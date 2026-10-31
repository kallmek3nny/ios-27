#define _GNU_SOURCE
#include "process_internal.h"

#include <stdio.h>
#include <string.h>

#if defined(__linux__) && (defined(__aarch64__) || defined(__x86_64__))
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__aarch64__)
#define IOS27_AUDIT_ARCH AUDIT_ARCH_AARCH64
#else
#define IOS27_AUDIT_ARCH AUDIT_ARCH_X86_64
#endif

#define REPORT_MAGIC UINT32_C(0x49323750)
#define REPORT_RETURN 0u
#define REPORT_SETUP 1u
#define REPORT_READY 2u
typedef struct {
    uint32_t magic;
    uint32_t kind;
    int32_t value;
} child_report;
_Static_assert(sizeof(child_report) == 12u, "fixed child report size");

/* A packet-sized write to our private pipe is the only permitted I/O.
 * Check the syscall architecture and both halves of each 64-bit argument.
 * General write/read, files, sockets, fork, exec and protection changes die
 * with SIGSYS. A raw exit is allowed but is not a valid LC_MAIN report. */
static int install_filter(int report_fd) {
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
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program);
}

static int close_inherited(int keep) {
#ifdef SYS_close_range
    if (keep > 0 && syscall(SYS_close_range, 0u, (unsigned)keep - 1u, 0u))
        return -1;
    return (int)syscall(SYS_close_range, (unsigned)keep + 1u, UINT_MAX, 0u);
#else
    (void)keep;
    errno = ENOSYS;
    return -1;
#endif
}

static int write_report(int fd, uint32_t kind, int value) {
    child_report report = {REPORT_MAGIC, kind, (int32_t)value};
    ssize_t written;
    do {
        written = write(fd, &report, sizeof(report));
    } while (written < 0 && errno == EINTR);
    return written == (ssize_t)sizeof(report) ? 0 : -1;
}

static void report_and_exit(int fd, uint32_t kind, int value) {
    int sent = write_report(fd, kind, value);
    _exit(!sent && kind == REPORT_RETURN ? 0 : 125);
}

/* No allocation, stdio, guest loading or host mutex use after fork. All
 * executable/stack mappings and guest arguments have already been prepared. */
static void child_run(int fd, pid_t parent, int (*entry)(void *), void *context) {
    struct sigaction action = {0};
    struct rlimit core = {0u, 0u};
    sigset_t empty;
    action.sa_handler = SIG_DFL;
    if (sigemptyset(&action.sa_mask) || sigemptyset(&empty))
        report_and_exit(fd, REPORT_SETUP, errno);
    /* Signals stay blocked across fork and disposition reset. In particular,
     * never invoke a launcher callback or inherit SIG_IGN in the guest.
     * libc reserves internal signal numbers for which sigaction is EINVAL. */
    for (int signal = 1; signal < NSIG; ++signal) {
        if (signal == SIGKILL || signal == SIGSTOP) continue;
        if (sigaction(signal, &action, NULL) && errno != EINVAL)
            report_and_exit(fd, REPORT_SETUP, errno);
    }
    if (setrlimit(RLIMIT_CORE, &core) ||
        prctl(PR_SET_PDEATHSIG, SIGKILL, 0ul, 0ul, 0ul))
        report_and_exit(fd, REPORT_SETUP, errno);
    if (getppid() != parent) _exit(125);
    if (close_inherited(fd) ||
        prctl(PR_SET_NO_NEW_PRIVS, 1ul, 0ul, 0ul, 0ul) ||
        sigprocmask(SIG_SETMASK, &empty, NULL) || install_filter(fd))
        report_and_exit(fd, REPORT_SETUP, errno);
    /* A fault before this packet is a setup failure, not guest execution. */
    if (write_report(fd, REPORT_READY, 0)) _exit(125);
    report_and_exit(fd, REPORT_RETURN, entry(context));
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
#endif

static int fail(char *error, size_t capacity, const char *message) {
    if (error && capacity) (void)snprintf(error, capacity, "%s", message);
    return -1;
}

int ios27_process_supervise(int (*entry)(void *), void *context,
                            const ios27_process_options *options,
                            ios27_process_result *result,
                            char *error, size_t error_size) {
    if (!result) return fail(error, error_size, "null child result");
    memset(result, 0, sizeof(*result));
    if (!entry || !options || !options->timeout_ms ||
        options->timeout_ms > IOS27_PROCESS_MAX_TIMEOUT_MS)
        return fail(error, error_size, "invalid child entry or deadline");
#if !defined(__linux__) || (!defined(__aarch64__) && !defined(__x86_64__))
    (void)context;
    return fail(error, error_size, "child supervision requires Linux ARM64/x86_64");
#else
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
    if (child == 0) child_run(pipefd[1], parent, entry, context);
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
            if (error && error_size) error[0] = '\0';
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
        if (report.magic == REPORT_MAGIC && report.kind == REPORT_SETUP &&
            WIFEXITED(status) && WEXITSTATUS(status) == 125) {
            if (error && error_size)
                (void)snprintf(error, error_size, "child containment setup failed: errno=%d",
                               (int)report.value);
            return -1;
        }
    }
    if (received < (ssize_t)sizeof(ready))
        return fail(error, error_size, "child failed before containment was ready");
    memcpy(&ready, packet, sizeof(ready));
    if (ready.magic != REPORT_MAGIC || ready.kind != REPORT_READY || ready.value != 0)
        return fail(error, error_size, "invalid child readiness packet");
    if (received == (ssize_t)(2u * sizeof(report))) {
        memcpy(&report, packet + sizeof(ready), sizeof(report));
        if (report.magic != REPORT_MAGIC || report.kind != REPORT_RETURN)
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
        if (report.kind != REPORT_RETURN || WEXITSTATUS(status) != 0)
            return fail(error, error_size, "invalid child return status");
        result->outcome = IOS27_PROCESS_RETURNED;
        result->exit_code = (int)report.value;
    }
    if (error && error_size) error[0] = '\0';
    return 0;
#endif
}
