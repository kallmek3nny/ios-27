/* Native posix_spawn lifecycle contract, also built for Apple's libSystem. */
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <sys/wait.h>
#include <spawn.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static unsigned checks, user_prepare, user_parent, user_child;
static atomic_int stop_allocations;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s errno=%d\n", __LINE__, #c, errno); exit(1); } } while (0)
static void before(void) { ++user_prepare; }
static void parent(void) { ++user_parent; }
static void child(void) { ++user_child; }

static void *allocate(void *unused) {
    (void)unused;
    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
    while (!atomic_load_explicit(&stop_allocations, memory_order_acquire)) {
        for (unsigned i = 0; i < 64; ++i) {
            volatile unsigned char *buffer = malloc(4096 + i);
            if (!buffer) return (void *)(uintptr_t)1;
            buffer[0] = (unsigned char)i;
            buffer[4095 + i] = (unsigned char)(i + 1);
            free((void *)buffer);
        }
        nanosleep(&delay, NULL);
    }
    return NULL;
}

int main(int argc, char **argv) {
    alarm(15);
    if (argc == 2 && strcmp(argv[1], "--child") == 0) {
        sigset_t mask;
        struct sigaction action;
        CHECK(getenv("IOS27_SPAWN_CHILD") && strcmp(getenv("IOS27_SPAWN_CHILD"), "native") == 0);
        CHECK(sigprocmask(SIG_SETMASK, NULL, &mask) == 0 && sigismember(&mask, SIGUSR1) == 1);
        CHECK(sigaction(SIGUSR2, NULL, &action) == 0 && action.sa_handler == SIG_DFL);
        const char marker[] = "SPAWN_CHILD_PIPE_OK\n";
        CHECK(write(STDOUT_FILENO, marker, sizeof(marker) - 1) == (ssize_t)(sizeof(marker) - 1));
        _exit(37);
    }
    char executable[4096];
    uint32_t capacity = sizeof(executable);
    CHECK(_NSGetExecutablePath(executable, &capacity) == 0 && executable[0] == '/');
    CHECK(setenv("IOS27_SPAWN_CHILD", "native", 1) == 0);
    CHECK(pthread_atfork(before, parent, child) == 0);
    struct sigaction ignore = {0};
    ignore.sa_handler = SIG_IGN;
    CHECK(sigemptyset(&ignore.sa_mask) == 0 && sigaction(SIGUSR2, &ignore, NULL) == 0);
    pthread_t allocator;
    CHECK(pthread_create(&allocator, NULL, allocate, NULL) == 0);
    const unsigned children = 8;
    for (unsigned i = 0; i < children; ++i) {
        int channel[2];
        CHECK(pipe(channel) == 0 && channel[0] > STDERR_FILENO && channel[1] > STDERR_FILENO);
        posix_spawn_file_actions_t actions;
        posix_spawnattr_t attributes;
        CHECK(posix_spawn_file_actions_init(&actions) == 0);
        CHECK(posix_spawn_file_actions_adddup2(&actions, channel[1], STDOUT_FILENO) == 0);
        CHECK(posix_spawn_file_actions_addclose(&actions, channel[0]) == 0);
        CHECK(posix_spawn_file_actions_addclose(&actions, channel[1]) == 0);
        CHECK(posix_spawnattr_init(&attributes) == 0);
        sigset_t mask, defaults;
        CHECK(sigemptyset(&mask) == 0 && sigaddset(&mask, SIGUSR1) == 0);
        CHECK(sigemptyset(&defaults) == 0 && sigaddset(&defaults, SIGUSR2) == 0);
        CHECK(posix_spawnattr_setsigmask(&attributes, &mask) == 0);
        CHECK(posix_spawnattr_setsigdefault(&attributes, &defaults) == 0);
        CHECK(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF) == 0);
        char *arguments[] = {executable, "--child", NULL};
        pid_t pid = -1;
        int result = posix_spawn(&pid, executable, &actions, &attributes, arguments, *_NSGetEnviron());
        if (result) fprintf(stderr, "posix_spawn returned %d\n", result);
        CHECK(result == 0 && pid > 0);
        CHECK(posix_spawn_file_actions_destroy(&actions) == 0 && posix_spawnattr_destroy(&attributes) == 0);
        CHECK(close(channel[1]) == 0);
        char output[64] = {0};
        size_t used = 0;
        ssize_t amount;
        while ((amount = read(channel[0], output + used, sizeof(output) - 1 - used)) != 0) {
            if (amount < 0 && errno == EINTR) continue;
            CHECK(amount > 0 && used + (size_t)amount < sizeof(output));
            used += (size_t)amount;
        }
        CHECK(close(channel[0]) == 0);
        int status;
        pid_t waited;
        do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
        CHECK(waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 37);
        CHECK(strcmp(output, "SPAWN_CHILD_PIPE_OK\n") == 0);
    }
    char *missing_args[] = {"missing-ios27-probe", NULL};
    pid_t missing_pid = -1;
    CHECK(posix_spawn(&missing_pid, "/ios27-probe-does-not-exist", NULL, NULL, missing_args, *_NSGetEnviron()) == ENOENT);
    atomic_store_explicit(&stop_allocations, 1, memory_order_release);
    void *allocation_result;
    CHECK(pthread_join(allocator, &allocation_result) == 0 && allocation_result == NULL);
    CHECK(user_prepare == 0 && user_parent == 0 && user_child == 0);
    printf("DARWIN_SPAWN_LIFECYCLE_OK children=%u checks=%u failed_exec=ENOENT user_atfork=0\n", children, checks);
    return 0;
}
