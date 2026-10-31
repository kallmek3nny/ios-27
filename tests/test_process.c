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
#include <sys/stat.h>
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

#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 0x0008U
#endif
#ifndef F_SEAL_EXEC
#define F_SEAL_EXEC 0x0020
#endif
#define TEST_LAUNCH_SEALS (F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)
#define TEST_PAYLOAD_CAPACITY 1024u
#define TEST_WIDE_STRINGS (IOS27_LAUNCH_MAX_STRINGS / IOS27_LAUNCH_MAX_STRING)

/* A memfd holding bytes, created like the launcher's, with the given seals. */
static int sealed_memfd(const void *bytes, size_t size, int seals) {
    int fd = memfd_create("ios27-test-payload",
                          MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL);
    if (fd < 0 && errno == EINVAL)
        fd = memfd_create("ios27-test-payload", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    assert(fd >= 0);
    if (size != 0u) assert(write(fd, bytes, size) == (ssize_t)size);
    if (seals != 0) assert(!fcntl(fd, F_ADD_SEALS, seals));
    return fd;
}

/* A refused payload leaves the output structure completely cleared. */
static void payload_rejected(int fd, int expected_errno) {
    ios27_launch_payload payload;
    memset(&payload, 0xff, sizeof(payload));
    errno = 0;
    assert(ios27_launch_payload_open(fd, &payload) == -1);
    assert(errno == expected_errno);
    assert(payload.mapping == NULL && payload.mapping_size == 0u);
    assert(payload.image == NULL && payload.image_size == 0u);
    assert(payload.context.argc == 0 && payload.context.argv == NULL);
    assert(payload.context.envp == NULL && payload.context.apple == NULL);
}

static void bytes_rejected(const uint8_t *bytes, size_t size) {
    int fd = sealed_memfd(bytes, size, TEST_LAUNCH_SEALS);
    payload_rejected(fd, EINVAL);
    assert(!close(fd));
}

typedef struct { size_t offset; size_t width; uint64_t value; } header_patch;

static void patched_rejected(const uint8_t *valid, size_t size, header_patch patch) {
    uint8_t copy[TEST_PAYLOAD_CAPACITY];
    assert(size <= sizeof(copy) && patch.offset + patch.width <= size);
    memcpy(copy, valid, size);
    if (patch.width == sizeof(uint32_t)) {
        uint32_t narrow = (uint32_t)patch.value;
        memcpy(copy + patch.offset, &narrow, sizeof(narrow));
    } else {
        assert(patch.width == sizeof(uint64_t));
        memcpy(copy + patch.offset, &patch.value, sizeof(patch.value));
    }
    bytes_rejected(copy, size);
}

static void create_rejected(const uint8_t *data, size_t size,
                            const ios27_startup_context *context) {
    char error[160] = {0};
    int fd = 123;
    size_t descriptors = fd_count();
    assert(ios27_launch_payload_create(data, size, context, &fd,
                                       error, sizeof(error)) == -1);
    assert(fd == -1 && error[0] && fd_count() == descriptors);
}

/* Each opened string must lie in the payload's read-only strings area. */
static void same_strings(const ios27_launch_payload *payload,
                         const char *const *actual,
                         const char *const *expected, size_t count) {
    for (size_t i = 0u; i < count; ++i) {
        const uint8_t *at = (const uint8_t *)actual[i];
        assert(actual[i] && !strcmp(actual[i], expected[i]));
        assert(at >= payload->image + payload->image_size &&
               at < payload->mapping + payload->mapping_size);
    }
    assert(actual[count] == NULL);
}

static size_t payload_bytes(int fd, uint8_t *bytes, ios27_launch_header *header) {
    struct stat st;
    assert(!fstat(fd, &st) && st.st_size >= (off_t)sizeof(*header));
    assert((uint64_t)st.st_size <= TEST_PAYLOAD_CAPACITY);
    assert(pread(fd, bytes, (size_t)st.st_size, 0) == (ssize_t)st.st_size);
    memcpy(header, bytes, sizeof(*header));
    return (size_t)st.st_size;
}

/* Launch payload creation, sealing and loader-side validation. These run on
 * both CI architectures; no guest code is mapped executable or entered. */
static void launch_payload_checks(void) {
    static const uint8_t image[] = {0xcf, 0xfa, 0xed, 0xfe, 0x0c, 0x00, 0x00, 0x01, 0x5a};
    static char wide[TEST_WIDE_STRINGS + 1u][IOS27_LAUNCH_MAX_STRING];
    static char too_long[IOS27_LAUNCH_MAX_STRING + 1u];
    static uint8_t crafted[sizeof(ios27_launch_header) + 1u + IOS27_LAUNCH_MAX_STRING + 1u];
    const char *argv[] = {"fixture", "--flag", NULL};
    const char *envp[] = {"HOME=/guest", NULL};
    const char *apple[] = {"executable_path=/guest/fixture", "", NULL};
    const char *many[IOS27_LAUNCH_MAX_VECTOR + 2u];
    const char *wide_vector[TEST_WIDE_STRINGS + 2u];
    const char *broken[] = {"fixture", NULL, NULL};
    const char *single[] = {NULL, NULL};
    ios27_startup_context context = {.argc = 2, .argv = argv, .envp = envp, .apple = apple};
    ios27_startup_context empty = {.argc = 0};
    ios27_startup_context bulk = {.argc = (int)TEST_WIDE_STRINGS, .argv = wide_vector};
    ios27_launch_payload payload;
    ios27_launch_header header;
    uint8_t bytes[TEST_PAYLOAD_CAPACITY];
    char error[160], path[64];
    size_t descriptors = fd_count(), size, strings = 0u;
    struct stat st;
    int fd, other, flags, seals, pipes[2];

    for (size_t i = 0u; argv[i]; ++i) strings += strlen(argv[i]) + 1u;
    for (size_t i = 0u; envp[i]; ++i) strings += strlen(envp[i]) + 1u;
    for (size_t i = 0u; apple[i]; ++i) strings += strlen(apple[i]) + 1u;
    error[0] = 'x';
    assert(!ios27_launch_payload_create(image, sizeof(image), &context, &fd,
                                        error, sizeof(error)));
    assert(fd >= 0 && error[0] == '\0');
    flags = fcntl(fd, F_GETFD);
    assert(flags >= 0 && (flags & FD_CLOEXEC));
    seals = fcntl(fd, F_GET_SEALS);
    assert(seals >= 0 && (seals & TEST_LAUNCH_SEALS) == TEST_LAUNCH_SEALS);
    assert(!fstat(fd, &st) && S_ISREG(st.st_mode));
    if (seals & F_SEAL_EXEC) assert((st.st_mode & 0111) == 0);
    size = payload_bytes(fd, bytes, &header);
    assert(size == sizeof(header) + sizeof(image) + strings);
    assert(header.magic == IOS27_LAUNCH_MAGIC && header.version == IOS27_LAUNCH_VERSION);
    assert(header.argc == 2u && header.envc == 1u && header.applec == 2u);
    assert(header.reserved == 0u && header.image_size == sizeof(image));
    assert(header.strings_size == strings);
    assert(!memcmp(bytes + sizeof(header), image, sizeof(image)));

    /* The seal set is final: no write, resize or new seal by any route. */
    errno = 0;
    assert(write(fd, "x", 1u) == -1 && errno == EPERM);
    errno = 0;
    assert(pwrite(fd, "x", 1u, 0) == -1 && errno == EPERM);
    errno = 0;
    assert(ftruncate(fd, 0) == -1 && errno == EPERM);
    errno = 0;
    assert(ftruncate(fd, (off_t)size + 4096) == -1 && errno == EPERM);
    errno = 0;
    assert(fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) == -1 && errno == EPERM);
    errno = 0;
    assert(mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) == MAP_FAILED &&
           errno == EPERM);
    assert(snprintf(path, sizeof(path), "/proc/self/fd/%d", fd) > 0);
    other = open(path, O_RDWR | O_CLOEXEC);
    assert(other >= 0);
    errno = 0;
    assert(write(other, "x", 1u) == -1 && errno == EPERM);
    assert(!close(other));

    memset(&payload, 0xff, sizeof(payload));
    assert(!ios27_launch_payload_open(fd, &payload));
    assert(payload.mapping && payload.mapping_size == size);
    assert(payload.image == payload.mapping + sizeof(header));
    assert(payload.image_size == sizeof(image));
    assert(!memcmp(payload.image, image, sizeof(image)));
    assert(payload.context.argc == 2);
    same_strings(&payload, payload.context.argv, argv, 2u);
    same_strings(&payload, payload.context.envp, envp, 1u);
    same_strings(&payload, payload.context.apple, apple, 2u);
    {
        ios27_launch_payload again;
        assert(!ios27_launch_payload_open(fd, &again));
        assert(again.mapping != payload.mapping && again.image_size == sizeof(image));
        ios27_launch_payload_close(&again);
    }
    ios27_launch_payload_close(&payload);
    assert(payload.mapping == NULL && payload.image == NULL);
    assert(payload.context.argc == 0 && payload.context.argv == NULL);
    ios27_launch_payload_close(&payload);
    ios27_launch_payload_close(NULL);

    /* A faithful resealed copy opens; any missing seal is refused. */
    other = sealed_memfd(bytes, size, TEST_LAUNCH_SEALS);
    assert(!ios27_launch_payload_open(other, &payload));
    assert(payload.context.argc == 2 && payload.image_size == sizeof(image));
    ios27_launch_payload_close(&payload);
    assert(!close(other));
    {
        static const int missing[] = {F_SEAL_SEAL, F_SEAL_SHRINK, F_SEAL_GROW,
                                      F_SEAL_WRITE, TEST_LAUNCH_SEALS};
        for (size_t i = 0u; i < sizeof(missing) / sizeof(missing[0]); ++i) {
            other = sealed_memfd(bytes, size, TEST_LAUNCH_SEALS & ~missing[i]);
            payload_rejected(other, EPERM);
            assert(!close(other));
        }
    }
    assert(!pipe(pipes));
    payload_rejected(pipes[0], EINVAL);
    assert(!close(pipes[0]) && !close(pipes[1]));
    payload_rejected(-1, EBADF);
    errno = 0;
    assert(ios27_launch_payload_open(fd, NULL) == -1 && errno == EINVAL);

    /* Every corrupted header or strings area on a correctly sealed memfd is
     * refused before any vector is exposed. */
    {
        const header_patch patches[] = {
            {offsetof(ios27_launch_header, magic), 4u, IOS27_LAUNCH_MAGIC ^ 1u},
            {offsetof(ios27_launch_header, version), 4u, 0u},
            {offsetof(ios27_launch_header, version), 4u, IOS27_LAUNCH_VERSION + 1u},
            {offsetof(ios27_launch_header, reserved), 4u, 1u},
            {offsetof(ios27_launch_header, argc), 4u, IOS27_LAUNCH_MAX_VECTOR + 1u},
            {offsetof(ios27_launch_header, envc), 4u, IOS27_LAUNCH_MAX_VECTOR + 1u},
            {offsetof(ios27_launch_header, applec), 4u, IOS27_LAUNCH_MAX_VECTOR + 1u},
            {offsetof(ios27_launch_header, argc), 4u, UINT32_MAX},
            {offsetof(ios27_launch_header, argc), 4u, header.argc + 1u},
            {offsetof(ios27_launch_header, argc), 4u, header.argc - 1u},
            {offsetof(ios27_launch_header, envc), 4u, header.envc + 1u},
            {offsetof(ios27_launch_header, applec), 4u, header.applec - 1u},
            {offsetof(ios27_launch_header, image_size), 8u, 0u},
            {offsetof(ios27_launch_header, image_size), 8u, header.image_size - 1u},
            {offsetof(ios27_launch_header, image_size), 8u, header.image_size + 1u},
            {offsetof(ios27_launch_header, image_size), 8u,
             (uint64_t)IOS27_LAUNCH_MAX_IMAGE + 1u},
            {offsetof(ios27_launch_header, image_size), 8u, UINT64_MAX},
            {offsetof(ios27_launch_header, strings_size), 8u, header.strings_size - 1u},
            {offsetof(ios27_launch_header, strings_size), 8u, header.strings_size + 1u},
            {offsetof(ios27_launch_header, strings_size), 8u,
             (uint64_t)IOS27_LAUNCH_MAX_STRINGS + 1u},
            {offsetof(ios27_launch_header, strings_size), 8u, UINT64_MAX}
        };
        uint8_t copy[TEST_PAYLOAD_CAPACITY];
        for (size_t i = 0u; i < sizeof(patches) / sizeof(patches[0]); ++i)
            patched_rejected(bytes, size, patches[i]);
        memcpy(copy, bytes, size);
        copy[size - 1u] = 'x'; /* unterminated final string */
        bytes_rejected(copy, size);
        memcpy(copy, bytes, size);
        copy[sizeof(header) + sizeof(image) + 1u] = '\0'; /* extra string */
        bytes_rejected(copy, size);
        bytes_rejected(bytes, 0u);
        bytes_rejected(bytes, 2u * sizeof(uint32_t));
        bytes_rejected(bytes, sizeof(header));
        bytes_rejected(bytes, size - 1u);
    }
    other = sealed_memfd(bytes, size, 0);
    assert(!ftruncate(other, (off_t)(sizeof(header) + IOS27_LAUNCH_MAX_IMAGE +
                                     IOS27_LAUNCH_MAX_STRINGS + 1u)));
    assert(!fcntl(other, F_ADD_SEALS, TEST_LAUNCH_SEALS));
    payload_rejected(other, EINVAL);
    assert(!close(other));

    /* The loader enforces the 4096-byte string limit independently. */
    {
        ios27_launch_header crafted_header = {
            .magic = IOS27_LAUNCH_MAGIC, .version = IOS27_LAUNCH_VERSION,
            .argc = 1u, .image_size = 1u, .strings_size = IOS27_LAUNCH_MAX_STRING
        };
        uint8_t *string = crafted + sizeof(crafted_header) + 1u;
        memset(crafted, 0, sizeof(crafted));
        memcpy(crafted, &crafted_header, sizeof(crafted_header));
        memset(string, 'z', IOS27_LAUNCH_MAX_STRING - 1u);
        other = sealed_memfd(crafted, sizeof(crafted) - 1u, TEST_LAUNCH_SEALS);
        assert(!ios27_launch_payload_open(other, &payload));
        assert(payload.context.argc == 1 && payload.context.argv[1] == NULL);
        assert(strlen(payload.context.argv[0]) == IOS27_LAUNCH_MAX_STRING - 1u);
        assert(payload.context.envp[0] == NULL && payload.context.apple[0] == NULL);
        ios27_launch_payload_close(&payload);
        assert(!close(other));
        crafted_header.strings_size = IOS27_LAUNCH_MAX_STRING + 1u;
        memcpy(crafted, &crafted_header, sizeof(crafted_header));
        string[IOS27_LAUNCH_MAX_STRING - 1u] = 'z';
        bytes_rejected(crafted, sizeof(crafted));
    }

    /* Empty startup context: three empty NULL-terminated vectors. */
    assert(!ios27_launch_payload_create(image, sizeof(image), &empty, &other,
                                        error, sizeof(error)));
    assert(!ios27_launch_payload_open(other, &payload));
    assert(payload.mapping_size == sizeof(header) + sizeof(image));
    assert(payload.context.argc == 0 && payload.context.argv[0] == NULL);
    assert(payload.context.envp[0] == NULL && payload.context.apple[0] == NULL);
    ios27_launch_payload_close(&payload);
    assert(!close(other));

    /* Creation limits match the guest stack and leak no descriptor. */
    for (size_t i = 0u; i <= IOS27_LAUNCH_MAX_VECTOR; ++i) many[i] = "e=1";
    many[IOS27_LAUNCH_MAX_VECTOR + 1u] = NULL;
    context.envp = many;
    create_rejected(image, sizeof(image), &context);
    many[IOS27_LAUNCH_MAX_VECTOR] = NULL;
    assert(!ios27_launch_payload_create(image, sizeof(image), &context, &other,
                                        error, sizeof(error)));
    assert(!ios27_launch_payload_open(other, &payload));
    same_strings(&payload, payload.context.envp, many, IOS27_LAUNCH_MAX_VECTOR);
    same_strings(&payload, payload.context.apple, apple, 2u);
    ios27_launch_payload_close(&payload);
    assert(!close(other));
    context.envp = envp;
    context.argc = (int)IOS27_LAUNCH_MAX_VECTOR + 1;
    context.argv = many;
    create_rejected(image, sizeof(image), &context);
    context.argc = -1;
    create_rejected(image, sizeof(image), &context);
    context.argc = 1;
    context.argv = NULL;
    create_rejected(image, sizeof(image), &context);
    context.argc = 2;
    context.argv = broken;
    create_rejected(image, sizeof(image), &context);
    context.argv = argv;
    memset(too_long, 'q', IOS27_LAUNCH_MAX_STRING);
    too_long[IOS27_LAUNCH_MAX_STRING] = '\0';
    single[0] = too_long;
    context.apple = single;
    create_rejected(image, sizeof(image), &context);
    too_long[IOS27_LAUNCH_MAX_STRING - 1u] = '\0';
    assert(!ios27_launch_payload_create(image, sizeof(image), &context, &other,
                                        error, sizeof(error)));
    assert(!ios27_launch_payload_open(other, &payload));
    same_strings(&payload, payload.context.apple, single, 1u);
    assert(strlen(payload.context.apple[0]) == IOS27_LAUNCH_MAX_STRING - 1u);
    ios27_launch_payload_close(&payload);
    assert(!close(other));
    context.apple = apple;
    for (size_t i = 0u; i <= TEST_WIDE_STRINGS; ++i) {
        memset(wide[i], 'a' + (int)i, IOS27_LAUNCH_MAX_STRING - 1u);
        wide[i][IOS27_LAUNCH_MAX_STRING - 1u] = '\0';
        wide_vector[i] = wide[i];
    }
    wide_vector[TEST_WIDE_STRINGS] = NULL;
    assert(!ios27_launch_payload_create(image, sizeof(image), &bulk, &other,
                                        error, sizeof(error)));
    assert(!ios27_launch_payload_open(other, &payload));
    assert(payload.mapping_size ==
           sizeof(header) + sizeof(image) + IOS27_LAUNCH_MAX_STRINGS);
    same_strings(&payload, payload.context.argv, wide_vector, TEST_WIDE_STRINGS);
    ios27_launch_payload_close(&payload);
    assert(!close(other));
    wide_vector[TEST_WIDE_STRINGS] = wide[TEST_WIDE_STRINGS];
    wide_vector[TEST_WIDE_STRINGS + 1u] = NULL;
    bulk.argc = (int)TEST_WIDE_STRINGS + 1;
    create_rejected(image, sizeof(image), &bulk);
    create_rejected(NULL, sizeof(image), &context);
    create_rejected(image, 0u, &context);
    create_rejected(image, (size_t)IOS27_LAUNCH_MAX_IMAGE + 1u, &context);
    create_rejected(image, sizeof(image), NULL);
    assert(ios27_launch_payload_create(image, sizeof(image), &context, NULL,
                                       error, sizeof(error)) == -1);
    assert(!close(fd));
    assert(fd_count() == descriptors);
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
    launch_payload_checks();
    assert(getpid() == parent && fd_count() == descriptors);
    assert(prctl(PR_GET_NO_NEW_PRIVS, 0ul, 0ul, 0ul, 0ul) == no_new_privs);
    assert(prctl(PR_GET_SECCOMP, 0ul, 0ul, 0ul, 0ul) == seccomp);
    puts("contained process: full results, syscall denial, reset signals, setup refusal, EINTR deadline, private memory, reap/FD lifecycle and sealed launch payloads passed");
    return 0;
}
