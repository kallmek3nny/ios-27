#define _GNU_SOURCE
#include "ios27/phase22a_space.h"

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__x86_64__)
#define PHASE22A_NATIVE_ARCH AUDIT_ARCH_X86_64
#else
#define PHASE22A_NATIVE_ARCH AUDIT_ARCH_AARCH64
#endif

/* Only this compiled-in probe runs after fork. This is NOT a guest binary
 * executor. No execve, open, socket, read, or write to inherited descriptors
 * is allowed. Only write to the private pipe plus process exit are permitted.
 * Architecture verified before syscall number dispatch. */
static int restrict_child(int pipefd) {
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (uint32_t)offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, PHASE22A_NATIVE_ARCH, 1u, 0u),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (uint32_t)offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_exit, 5u, 0u),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_exit_group, 4u, 0u),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_rt_sigreturn, 3u, 0u),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_write, 0u, 4u),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 (uint32_t)offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)pipefd, 0u, 2u),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA))
    };
    struct sock_fprog program = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter
    };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1u, 0u, 0u, 0u) != 0) return -1;
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program);
}

int ios27_22a_restricted_probe(void) {
    int fds[2];
    pid_t pid;
    int status;
    char response = '?';
    ssize_t n;
    if (pipe(fds) != 0) return IOS27_22A_LIMIT;
    pid = fork();
    if (pid < 0) {
        (void)close(fds[0]);
        (void)close(fds[1]);
        return IOS27_22A_LIMIT;
    }
    if (pid == 0) {
        const char ok = 'Y';
        int denied_open, denied_socket, denied_stdout;
        (void)close(fds[0]);
        if (restrict_child(fds[1]) != 0) _exit(77);
        errno = 0;
        denied_open = syscall(SYS_openat, -100, "/etc/passwd", 0) == -1 && errno == EPERM;
        errno = 0;
        denied_socket = syscall(SYS_socket, 2, 1, 0) == -1 && errno == EPERM;
        errno = 0;
        denied_stdout = syscall(SYS_write, 1, &ok, 1u) == -1 && errno == EPERM;
        if (!(denied_open && denied_socket && denied_stdout)) _exit(2);
        if (syscall(SYS_write, fds[1], &ok, 1u) != 1) _exit(3);
        _exit(0);
    }
    (void)close(fds[1]);
    n = read(fds[0], &response, 1u);
    (void)close(fds[0]);
    if (waitpid(pid, &status, 0) != pid) return IOS27_22A_FAULT;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 77) return IOS27_22A_LIMIT;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || n != 1 || response != 'Y')
        return IOS27_22A_FAULT;
    return IOS27_22A_OK;
}
#else
int ios27_22a_restricted_probe(void) { return IOS27_22A_LIMIT; }
#endif
