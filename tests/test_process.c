#define _GNU_SOURCE
#include "process_internal.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__aarch64__)
#define FAULT_SIGNAL SIGTRAP
#else
#define FAULT_SIGNAL SIGILL
#endif

static size_t fd_count(void) {
    DIR *dir = opendir("/proc/self/fd");
    struct dirent *entry;
    size_t count = 0u;
    assert(dir);
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) ++count;
    }
    assert(closedir(dir) == 0);
    return count;
}

static void no_children(void) {
    int status;
    errno = 0;
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
}

static int return_value(void *opaque) {
    int *value = opaque;
    int result = *value;
    *value = 999; /* fork-private memory must not modify the parent */
    return result;
}

static int spin(void *opaque) {
    volatile unsigned *value = opaque;
    for (;;) ++*value;
    return 0;
}

static int fault(void *opaque) {
    (void)opaque;
#if defined(__aarch64__)
    __asm__ volatile("brk #0");
#elif defined(__x86_64__)
    __asm__ volatile("ud2");
#endif
    return 0;
}

static int exit_without_report(void *opaque) {
    (void)opaque;
    _exit(0);
}

typedef struct { int operation; int fd; const char *path; } forbidden;
static int forbidden_call(void *opaque) {
    const forbidden *call = opaque;
    char bytes[12] = {0};
    switch (call->operation) {
        case 0: (void)syscall(SYS_getpid); break;
        case 1: (void)syscall(SYS_openat, AT_FDCWD, call->path, O_RDONLY, 0); break;
        case 2: (void)syscall(SYS_socket, AF_INET, SOCK_STREAM, 0); break;
        case 3: (void)syscall(SYS_read, call->fd, bytes, sizeof(bytes)); break;
        case 4: (void)syscall(SYS_write, call->fd, bytes, sizeof(bytes)); break;
        case 5: (void)syscall(SYS_mprotect, NULL, 4096u, PROT_READ); break;
        default: (void)syscall(SYS_execve, "", NULL, NULL); break;
    }
    return 0; /* any allowed operation is a test failure */
}

static volatile sig_atomic_t alarms;
static void alarm_handler(int signal_number) {
    (void)signal_number;
    ++alarms;
}

static void cleared(const ios27_process_result *result) {
    assert(result->outcome == 0 && result->exit_code == 0 && result->signal == 0);
}

/* These inherited host conditions must fail closed before guest entry. Run
 * in a separate launcher so its seccomp policy and closed stdio stay local. */
static void launcher_conditions(int deny_close_range) {
    pid_t launcher = fork();
    int status;
    assert(launcher >= 0);
    if (!launcher) {
        ios27_process_options options = {1000u};
        ios27_process_result result;
        char error[160];
        int value = 73;
        if (deny_close_range) {
            struct sock_filter code[] = {
                BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                         offsetof(struct seccomp_data, nr)),
                BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_close_range, 0, 1),
                BPF_STMT(BPF_RET | BPF_K, deny_close_range == 1
                         ? SECCOMP_RET_ERRNO | ENOSYS : SECCOMP_RET_KILL_PROCESS),
                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
            };
            struct sock_fprog program = {
                (unsigned short)(sizeof(code) / sizeof(code[0])), code
            };
            assert(!prctl(PR_SET_NO_NEW_PRIVS, 1ul, 0ul, 0ul, 0ul));
            assert(!prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program));
            memset(&result, 0xff, sizeof(result));
            assert(ios27_process_supervise(return_value, &value, &options,
                                            &result, error, sizeof(error)) == -1);
            cleared(&result);
            assert(strstr(error, deny_close_range == 1 ? "setup" : "ready") != NULL);
        } else {
            assert(!close(STDIN_FILENO));
            assert(!close(STDOUT_FILENO));
            assert(!close(STDERR_FILENO));
            assert(!ios27_process_supervise(return_value, &value, &options,
                                             &result, error, sizeof(error)));
            assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 73);
        }
        assert(value == 73);
        no_children();
        _exit(0);
    }
    assert(waitpid(launcher, &status, 0) == launcher);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    no_children();
}

static int ready_spin(void *opaque) {
    volatile unsigned *ready = opaque;
    *ready = 1u;
    return spin((void *)ready);
}

/* Signal a separate launcher group only after its guest callback starts.
 * The launcher retains its original handler/ignore state; the supervised
 * child must have restored the default for every catchable signal. */
static void inherited_signal(int signal_number, int ignored) {
    volatile unsigned *ready = mmap(NULL, sizeof(*ready), PROT_READ | PROT_WRITE,
                                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    pid_t launcher;
    int status;
    struct timespec started, current, pause = {0, 1000000};
    assert(ready != MAP_FAILED);
    *ready = 0u;
    assert(!clock_gettime(CLOCK_MONOTONIC, &started));
    launcher = fork();
    assert(launcher >= 0);
    if (!launcher) {
        ios27_process_options options = {1000u};
        ios27_process_result result;
        struct sigaction action = {0}, observed;
        char error[160];
        assert(!setpgid(0, 0));
        action.sa_handler = ignored ? SIG_IGN : alarm_handler;
        assert(!sigemptyset(&action.sa_mask));
        assert(!sigaction(signal_number, &action, NULL));
        assert(!ios27_process_supervise(ready_spin, (void *)ready, &options,
                                         &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_SIGNALED && result.signal == signal_number);
        assert(!sigaction(signal_number, NULL, &observed));
        assert(observed.sa_handler == action.sa_handler);
        no_children();
        _exit(0);
    }
    while (!*ready) {
        assert(waitpid(launcher, &status, WNOHANG) == 0);
        assert(!clock_gettime(CLOCK_MONOTONIC, &current));
        assert(current.tv_sec - started.tv_sec < 3);
        (void)nanosleep(&pause, NULL);
    }
    assert(!kill(-launcher, signal_number));
    assert(waitpid(launcher, &status, 0) == launcher);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(!munmap((void *)ready, sizeof(*ready)));
    no_children();
}

int main(void) {
    ios27_process_options options = {1000u};
    ios27_process_result result;
    char error[160];
    size_t descriptors = fd_count();
    int values[] = {42, -70000, INT_MAX, INT_MIN};
    int no_new_privs = prctl(PR_GET_NO_NEW_PRIVS, 0ul, 0ul, 0ul, 0ul);
    int seccomp = prctl(PR_GET_SECCOMP, 0ul, 0ul, 0ul, 0ul);
    pid_t parent = getpid();
    assert(no_new_privs >= 0 && seccomp >= 0);
    for (size_t i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
        int value = values[i];
        assert(!ios27_process_supervise(return_value, &value, &options,
                                         &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED);
        assert(result.exit_code == values[i] && result.signal == 0);
        assert(value == values[i] && error[0] == '\0');
        no_children();
    }
    {
        char path[] = "/tmp/ios27-contained-XXXXXX";
        int fd = mkstemp(path);
        char contents[12];
        forbidden call = {0, fd, path};
        assert(fd >= 0 && write(fd, "parent-owned", 12u) == 12);
        assert(lseek(fd, 0, SEEK_SET) == 0);
        for (int operation = 0; operation < 7; ++operation) {
            call.operation = operation;
            assert(!ios27_process_supervise(forbidden_call, &call, &options,
                                             &result, error, sizeof(error)));
            assert(result.outcome == IOS27_PROCESS_SIGNALED && result.signal == SIGSYS);
            assert(result.exit_code == 0 && error[0] == '\0');
            assert(fcntl(fd, F_GETFD) >= 0 && lseek(fd, 0, SEEK_CUR) == 0);
            no_children();
        }
        assert(read(fd, contents, sizeof(contents)) == (ssize_t)sizeof(contents));
        assert(!memcmp(contents, "parent-owned", sizeof(contents)));
        assert(close(fd) == 0 && unlink(path) == 0);
    }
    {
        struct sigaction action = {0}, previous, observed;
        sigset_t blocked, previous_mask, observed_mask;
        action.sa_handler = alarm_handler;
        assert(sigemptyset(&action.sa_mask) == 0);
        assert(!sigaction(FAULT_SIGNAL, &action, &previous));
        assert(!sigemptyset(&blocked) && !sigaddset(&blocked, FAULT_SIGNAL));
        assert(!sigprocmask(SIG_BLOCK, &blocked, &previous_mask));
        assert(!ios27_process_supervise(fault, NULL, &options,
                                         &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_SIGNALED);
        assert(result.signal == FAULT_SIGNAL);
        assert(!sigaction(FAULT_SIGNAL, NULL, &observed) && observed.sa_handler == alarm_handler);
        assert(!sigprocmask(SIG_SETMASK, NULL, &observed_mask));
        assert(sigismember(&observed_mask, FAULT_SIGNAL) == 1);
        assert(!sigprocmask(SIG_SETMASK, &previous_mask, NULL));
        assert(!sigaction(FAULT_SIGNAL, &previous, NULL));
        no_children();
    }
    assert(ios27_process_supervise(exit_without_report, NULL, &options,
                                    &result, error, sizeof(error)) == -1);
    assert(result.outcome == 0 && error[0]);
    no_children();
    launcher_conditions(0);
    launcher_conditions(1);
    launcher_conditions(2);
    inherited_signal(SIGUSR1, 1);
    inherited_signal(SIGUSR2, 0);
    {
        struct sigaction action = {0}, previous;
        struct itimerval timer = {{0, 1000}, {0, 1000}}, disabled = {{0, 0}, {0, 0}};
        volatile unsigned value = 0u;
        action.sa_handler = alarm_handler;
        assert(!sigemptyset(&action.sa_mask));
        assert(!sigaction(SIGALRM, &action, &previous));
        assert(!setitimer(ITIMER_REAL, &timer, NULL));
        options.timeout_ms = 40u;
        assert(!ios27_process_supervise(spin, (void *)&value, &options,
                                         &result, error, sizeof(error)));
        assert(!setitimer(ITIMER_REAL, &disabled, NULL));
        assert(!sigaction(SIGALRM, &previous, NULL));
        assert(result.outcome == IOS27_PROCESS_TIMED_OUT && result.signal == SIGKILL);
        assert(value == 0u && alarms > 0);
        no_children();
    }
    {
        struct sigaction ignored = {0}, previous;
        int value = 1;
        assert(!sigemptyset(&ignored.sa_mask));
        for (unsigned i = 0u; i < 3u; ++i) {
            ignored.sa_handler = i == 0u ? SIG_IGN : (i == 1u ? SIG_DFL : alarm_handler);
            ignored.sa_flags = i == 1u ? SA_NOCLDWAIT : 0;
            assert(!sigaction(SIGCHLD, &ignored, &previous));
            memset(&result, 0xff, sizeof(result));
            assert(ios27_process_supervise(return_value, &value, &options,
                                            &result, error, sizeof(error)) == -1);
            cleared(&result);
            assert(value == 1 && error[0]);
            assert(!sigaction(SIGCHLD, &previous, NULL));
            no_children();
        }
    }
    memset(&result, 0xff, sizeof(result));
    assert(ios27_process_supervise(NULL, NULL, &options,
                                    &result, error, sizeof(error)) == -1);
    cleared(&result);
    memset(&result, 0xff, sizeof(result));
    assert(ios27_process_supervise(return_value, NULL, NULL,
                                    &result, error, sizeof(error)) == -1);
    cleared(&result);
    assert(ios27_process_supervise(return_value, NULL, &options,
                                    NULL, error, sizeof(error)) == -1);
    options.timeout_ms = 0u;
    memset(&result, 0xff, sizeof(result));
    assert(ios27_process_supervise(return_value, NULL, &options,
                                    &result, error, sizeof(error)) == -1);
    cleared(&result);
    options.timeout_ms = IOS27_PROCESS_MAX_TIMEOUT_MS + 1u;
    assert(ios27_process_supervise(return_value, NULL, &options,
                                    &result, error, sizeof(error)) == -1);
    options.timeout_ms = 1000u;
    for (unsigned i = 0u; i < 32u; ++i) {
        int value = (int)i;
        assert(!ios27_process_supervise(return_value, &value, &options,
                                         &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == (int)i);
        assert(value == (int)i);
        no_children();
    }
    assert(getpid() == parent && fd_count() == descriptors);
    assert(prctl(PR_GET_NO_NEW_PRIVS, 0ul, 0ul, 0ul, 0ul) == no_new_privs);
    assert(prctl(PR_GET_SECCOMP, 0ul, 0ul, 0ul, 0ul) == seccomp);
    puts("contained process: full results, syscall denial, reset signals, setup refusal, EINTR deadline, private memory and reap/FD lifecycle passed");
    return 0;
}
