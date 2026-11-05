#define _GNU_SOURCE
#include "process_internal.h"
#include "ios27/darwin_object.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* These are live kernel CPU-expiry tests, separate from the fast clamp and
 * read-back tests. Every changed resource limit lives in a throwaway launcher.
 * The observer never changes its own CPU limit or signal dispositions. */
typedef struct {
    atomic_uint entered;
    int status;
    ios27_process_result result;
    char error[160];
} observation;

typedef struct {
    const char *loader; /* NULL selects native Mach-O fork mode. */
    uint8_t *bytes;
    size_t size;
    ios27_macho_info info;
} native_program;

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "guest readiness must be lock-free");

static uint64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) abort();
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void pause_briefly(void) {
    struct timespec pause = {0, 5000000};
    (void)nanosleep(&pause, NULL);
}

static int busy_guest(void *context) {
    observation *seen = context;
    atomic_store_explicit(&seen->entered, 1u, memory_order_release);
    /* No syscall, including a clock query, is needed to consume CPU time. */
    for (;;) atomic_signal_fence(memory_order_seq_cst);
    return 0;
}

static int set_cpu_limits(rlim_t soft, rlim_t hard) {
    struct rlimit requested = {soft, hard}, observed;
    return setrlimit(RLIMIT_CPU, &requested) ||
           getrlimit(RLIMIT_CPU, &observed) ||
           observed.rlim_cur != soft || observed.rlim_max != hard;
}

/* /proc exposes the only child of this deliberately single-threaded
 * launcher. A pidfd then observes its death without reaping somebody else's
 * child, sending a signal to the guest, or relying on PID reuse. */
static pid_t guest_pid(pid_t launcher) {
    char path[100];
    long child = -1;
    FILE *file;
    (void)snprintf(path, sizeof(path), "/proc/%ld/task/%ld/children",
                   (long)launcher, (long)launcher);
    file = fopen(path, "r");
    if (!file) return -1;
    if (fscanf(file, "%ld", &child) != 1) child = -1;
    (void)fclose(file);
    return (pid_t)child;
}

static int filter_count(pid_t process) {
    char path[64], line[256];
    FILE *file;
    int count = -1;
    (void)snprintf(path, sizeof(path), "/proc/%ld/status", (long)process);
    file = fopen(path, "r");
    if (!file) return -1;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "Seccomp_filters: %d", &count) == 1) break;
    }
    (void)fclose(file);
    return count;
}

static int wait_launcher(pid_t launcher, int *status, uint64_t deadline) {
    for (;;) {
        pid_t waited = waitpid(launcher, status, WNOHANG);
        if (waited == launcher) return 0;
        if (waited < 0 && errno != EINTR) return -1;
        if (milliseconds() >= deadline) return -1;
        pause_briefly();
    }
}

/* Inspect without reaping, so cleanup still owns a failed launcher's PID. */
static int launcher_alive(pid_t launcher) {
    siginfo_t state;
    memset(&state, 0, sizeof(state));
    return !waitid(P_PID, (id_t)launcher, &state,
                   WEXITED | WNOHANG | WNOWAIT) && state.si_pid == 0;
}

/* The process group and subreaper keep cleanup effective even if a tested
 * launcher aborts while its guest is running or stopped. */
static void cleanup_group(pid_t launcher) {
    int status;
    (void)kill(-launcher, SIGKILL);
    (void)kill(launcher, SIGKILL);
    while (waitpid(-launcher, &status, 0) > 0 || errno == EINTR) {}
    while (waitpid(launcher, &status, 0) < 0 && errno == EINTR) {}
}

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (errno=%d; %s)\n", \
                __FILE__, __LINE__, #condition, errno, seen->error); \
        goto cleanup; \
    } \
} while (0)

static int expiry_case(const native_program *program, rlim_t soft,
                        rlim_t hard, int stopped) {
    observation *seen = mmap(NULL, sizeof(*seen), PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint64_t deadline = milliseconds() + 15000u;
    pid_t launcher = -1;
    int status = 0, guest = -1, success = 0, reaped = 0;
    int inherited_filters = filter_count(getpid());
    const int expected = soft < hard ? SIGXCPU : SIGKILL;
    const char *mode = !program ? "fork callback" :
                        program->loader ? "native exec" : "native fork";
    if (seen == MAP_FAILED) return -1;
    memset(seen, 0, sizeof(*seen));
    atomic_init(&seen->entered, 0u);
    launcher = fork();
    REQUIRE(launcher >= 0);
    if (!launcher) {
        ios27_process_options options = {IOS27_PROCESS_MAX_TIMEOUT_MS};
        struct rlimit after;
        struct sigaction action = {0}, observed;
        sigset_t blocked, mask;
        if (setpgid(0, 0) || set_cpu_limits(soft, hard)) _exit(2);
        /* A launcher may ignore and block SIGXCPU. The child must reset
         * both states, or the soft-limit cases would die at the hard limit
         * with SIGKILL instead of the required SIGXCPU. */
        if (expected == SIGXCPU) {
            action.sa_handler = SIG_IGN;
            if (sigemptyset(&action.sa_mask) ||
                sigaction(SIGXCPU, &action, NULL) || sigemptyset(&blocked) ||
                sigaddset(&blocked, SIGXCPU) ||
                sigprocmask(SIG_BLOCK, &blocked, NULL)) _exit(2);
        }
        if (program) {
            ios27_startup_context context = {0, NULL, NULL, NULL};
            if (program->loader)
                seen->status = ios27_run_executable_exec(program->loader,
                    program->bytes, program->size, &program->info, &context,
                    &options, &seen->result, seen->error, sizeof(seen->error));
            else
                seen->status = ios27_run_executable_contained(program->bytes,
                    program->size, &program->info, &context, &options,
                    &seen->result, seen->error, sizeof(seen->error));
        } else {
            seen->status = ios27_process_supervise(busy_guest, seen, &options,
                                                &seen->result, seen->error,
                                                sizeof(seen->error));
        }
        if (getrlimit(RLIMIT_CPU, &after) || after.rlim_cur != soft ||
            after.rlim_max != hard) _exit(3);
        if (expected == SIGXCPU &&
            (sigaction(SIGXCPU, NULL, &observed) ||
             observed.sa_handler != SIG_IGN ||
             sigprocmask(SIG_SETMASK, NULL, &mask) ||
             sigismember(&mask, SIGXCPU) != 1)) _exit(3);
        _exit(0);
    }
    if (stopped) {
        struct pollfd exited;
        siginfo_t stopped_state;
        pid_t child = -1;
        REQUIRE(inherited_filters >= 0);
        for (;;) {
            child = guest_pid(launcher);
            if (!program) {
                if (atomic_load_explicit(&seen->entered, memory_order_acquire))
                    break;
            } else if (child > 0 && filter_count(child) >= inherited_filters +
                       (program->loader ? 2 : 1)) {
                /* Exec discards the shared readiness word. Observing the
                 * final mandatory filter proves image preparation completed.
                 * The guest still runs while only its launcher is stopped;
                 * its eventual signaled result also validates READY. */
                break;
            }
            REQUIRE(launcher_alive(launcher));
            REQUIRE(milliseconds() < deadline);
            pause_briefly();
        }
        REQUIRE(child > 0);
        {
            struct rlimit observed;
            rlim_t bounded_hard = hard == RLIM_INFINITY ||
                                 hard > (rlim_t)IOS27_LOADER_CPU_LIMIT ?
                                 (rlim_t)IOS27_LOADER_CPU_LIMIT : hard;
            REQUIRE(!prlimit(child, RLIMIT_CPU, NULL, &observed));
            REQUIRE(observed.rlim_cur == soft && observed.rlim_max == bounded_hard);
        }
        guest = (int)syscall(SYS_pidfd_open, child, 0u);
        REQUIRE(guest >= 0);
        exited.fd = guest;
        exited.events = POLLIN;
        exited.revents = 0;
        REQUIRE(poll(&exited, 1u, 0) == 0);
        REQUIRE(!kill(launcher, SIGSTOP));
        memset(&stopped_state, 0, sizeof(stopped_state));
        /* Keep even an unexpectedly exited launcher waitable, so failure
         * cleanup never signals a PID that this observer already reaped. */
        REQUIRE(!waitid(P_PID, (id_t)launcher, &stopped_state,
                        WSTOPPED | WEXITED | WNOWAIT));
        REQUIRE(stopped_state.si_code == CLD_STOPPED &&
                stopped_state.si_status == SIGSTOP);
        /* The launcher is stopped and the guest receives no test signal.
         * Only the kernel's CPU timer can end this syscall-free busy loop. */
        while (poll(&exited, 1u, 0) == 0) {
            REQUIRE(milliseconds() < deadline);
            pause_briefly();
        }
        REQUIRE(exited.revents & POLLIN);
        REQUIRE(launcher_alive(launcher));
        REQUIRE(!kill(launcher, SIGCONT));
    }
    REQUIRE(!wait_launcher(launcher, &status, deadline));
    reaped = 1;
    REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (!program)
        REQUIRE(atomic_load_explicit(&seen->entered, memory_order_acquire) == 1u);
    REQUIRE(seen->status == 0 && seen->error[0] == '\0');
    REQUIRE(seen->result.outcome == IOS27_PROCESS_SIGNALED);
    REQUIRE(seen->result.signal == expected && seen->result.exit_code == 0);
    success = 1;
cleanup:
    if (guest >= 0) (void)close(guest);
    if (launcher > 0 && !reaped) cleanup_group(launcher);
    (void)munmap(seen, sizeof(*seen));
    if (success)
        printf("%s CPU expiry: soft=%lu hard=%lu, launcher %s, signal=%d\n",
               mode, (unsigned long)soft, (unsigned long)hard,
               stopped ? "stopped until guest death" : "running", expected);
    return success ? 0 : -1;
}

/* Only the single instruction at the original fixture's LC_MAIN is changed:
 * B . is an explicitly synthetic infinite loop, not new Apple capability. */
static int prepare_native(const char *path, native_program *program) {
    FILE *file = fopen(path, "rb");
    ios27_darwin_executable executable;
    long length;
    char error[160];
    size_t entry;
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 32 ||
        length > 2 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        (void)fclose(file);
        return -1;
    }
    program->size = (size_t)length;
    program->bytes = malloc(program->size);
    if (!program->bytes) { (void)fclose(file); return -1; }
    if (fread(program->bytes, 1u, program->size, file) != program->size) {
        (void)fclose(file);
        return -1;
    }
    if (fclose(file) || ios27_macho_parse(program->bytes, program->size,
            &program->info, error, sizeof(error)) ||
        ios27_darwin_executable_inspect(program->bytes, program->size,
            &executable, error, sizeof(error))) return -1;
    if (executable.entry_file_offset > program->size - 4u) return -1;
    entry = (size_t)executable.entry_file_offset;
    program->bytes[entry] = 0x00;
    program->bytes[entry + 1u] = 0x00;
    program->bytes[entry + 2u] = 0x00;
    program->bytes[entry + 3u] = 0x14; /* little-endian 0x14000000 */
    return 0;
}

int main(int argc, char **argv) {
    struct rlimit original, after;
    native_program native = {0};
    int status, failed = 0;
    if (argc != 1 && argc != 3) return 2;
#if !defined(__aarch64__)
    if (argc == 3) {
        puts("native CPU expiry requires ARM64 Linux; no native execution claimed");
        return 77;
    }
#endif
    if (getrlimit(RLIMIT_CPU, &original)) return 1;
    if (original.rlim_max != RLIM_INFINITY && original.rlim_max < 2u) {
        fprintf(stderr, "CPU expiry tests require an inherited hard limit >= 2s\n");
        return 77;
    }
    if (prctl(PR_SET_CHILD_SUBREAPER, 1ul, 0ul, 0ul, 0ul)) return 1;
    if (argc == 3 && prepare_native(argv[1], &native)) {
        free(native.bytes);
        return 1;
    }
    for (int mode = 0; mode < (argc == 3 ? 2 : 1) && !failed; ++mode) {
        native.loader = mode ? argv[2] : NULL;
        for (int stopped = 0; stopped <= 1 && !failed; ++stopped) {
            const native_program *program = argc == 3 ? &native : NULL;
            failed = expiry_case(program, 1u, 2u, stopped) ||
                     expiry_case(program, 1u, 1u, stopped);
        }
        /* Unlike the equal lower limits above, this enters the production
         * clamp branch: retain a one-second soft limit while lowering a
         * larger (possibly unlimited) hard limit to the 61-second bound.
         * The observer reads the real limits, and expiry still occurs at
         * the preserved soft limit while the launcher is stopped. */
        if (!failed && (original.rlim_max == RLIM_INFINITY ||
                        original.rlim_max > (rlim_t)IOS27_LOADER_CPU_LIMIT))
            failed = expiry_case(argc == 3 ? &native : NULL, 1u,
                                  original.rlim_max, 1);
    }
    free(native.bytes);
    if (failed) return 1;
    if (getrlimit(RLIMIT_CPU, &after) ||
        after.rlim_cur != original.rlim_cur || after.rlim_max != original.rlim_max)
        return 1;
    errno = 0;
    if (waitpid(-1, &status, WNOHANG) != -1 || errno != ECHILD) return 1;
    puts("live CPU expiry: soft/hard enforcement, paused launcher, inherited SIGXCPU reset, unchanged parent state and reaping passed");
    return 0;
}
