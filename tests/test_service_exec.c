#define _GNU_SOURCE
#include "process_internal.h"
#include "service_bridge_internal.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

int ios27_service_fixture_entry(int argc, char **argv, char **envp, char **apple,
                                 const ios27_service_api *api);

static int hosted_entry(void *opaque) {
    return ios27_service_fixture_entry(0, NULL, NULL, NULL, opaque);
}

static void no_children(void) {
    int status;
    errno = 0;
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
}

static void callbacks(void) {
    const ios27_service_api *api = ios27_service_bridge_initialize();
    ios27_service_api changed = *api;
    ios27_process_options options = {1000u};
    ios27_process_result result;
    char error[192];
    for (unsigned i = 0; i < 8u; ++i) {
        assert(!ios27_process_supervise(hosted_entry, (void *)api, &options,
                                         &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 42);
        assert(!error[0]);
        /* Child model writes must not reach the launcher's arena. */
        const uint8_t *span = api->memory(1024, 1, 0);
        assert(span && !span[0]);
        no_children();
    }
    changed.version += 1u;
    assert(!ios27_process_supervise(hosted_entry, &changed, &options,
                                     &result, error, sizeof(error)));
    assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 202);
    assert(!ios27_process_supervise(hosted_entry, NULL, &options,
                                     &result, error, sizeof(error)));
    assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 201);
    no_children();
    puts("service callbacks under mandatory seccomp: real compiled C client, version refusal and fork-private resources passed");
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    assert(file && !fseek(file, 0, SEEK_END));
    length = ftell(file);
    assert(length >= 32 && length <= 2 * 1024 * 1024);
    assert(!fseek(file, 0, SEEK_SET));
    bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(!fclose(file));
    *size = (size_t)length;
    return bytes;
}

#if defined(__aarch64__)
static void native_cases(const uint8_t *bytes, size_t size,
                          const ios27_macho_info *info, char **paths) {
    ios27_process_options options = {1000u};
    ios27_startup_context startup = {0, NULL, NULL, NULL};
    ios27_process_result result;
    char error[192];
    /* Ordinary APIs explicitly deliver no extension, rather than leaking an
     * unspecified register. The service fixture must refuse cleanly. */
    assert(!ios27_run_executable_contained(bytes, size, info, &startup,
        &options, &result, error, sizeof(error)));
    assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 201);
    assert(!ios27_run_executable_exec(paths[2], bytes, size, info, &startup,
        &options, &result, error, sizeof(error)));
    assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 201);
    for (unsigned i = 0; i < 8u; ++i) {
        assert(!ios27_run_executable_exec(paths[3], bytes, size, info, &startup,
            &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 42);
        no_children();
    }
    {
        const char *args[] = {"service-fixture", "vectors"};
        const char *env[] = {"MODE=guest", NULL};
        const char *apple[] = {"service=explicit", NULL};
        ios27_startup_context vectors = {2, args, env, apple};
        assert(!ios27_run_executable_exec(paths[3], bytes, size, info, &vectors,
            &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 42);
        const char *wrong[] = {"MODE=wrong", NULL};
        vectors.envp = wrong;
        assert(!ios27_run_executable_exec(paths[3], bytes, size, info, &vectors,
            &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 205);
    }
    {
        const char *modes[] = {"trap", "loop", "raw-syscall"};
        const int signals[] = {SIGTRAP, SIGKILL, SIGSYS};
        for (unsigned i = 0; i < 3u; ++i) {
            const char *args[] = {modes[i]};
            ios27_startup_context selected = {1, args, NULL, NULL};
            options.timeout_ms = i == 1u ? 100u : 1000u;
            assert(!ios27_run_executable_exec(paths[3], bytes, size, info, &selected,
                &options, &result, error, sizeof(error)));
            assert(result.outcome == (i == 1u ? IOS27_PROCESS_TIMED_OUT :
                                               IOS27_PROCESS_SIGNALED));
            assert(result.signal == signals[i]);
            options.timeout_ms = 1000u;
            assert(!ios27_run_executable_exec(paths[3], bytes, size, info, &startup,
                &options, &result, error, sizeof(error)));
            assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 42);
            no_children();
        }
    }
    /* A normal four-argument Darwin fixture also works with the opt-in
     * helper: the private fifth argument does not change its usual ABI. */
    {
        size_t old_size;
        uint8_t *old = read_file(paths[4], &old_size);
        ios27_macho_info old_info;
        assert(!ios27_macho_parse(old, old_size, &old_info, error, sizeof(error)));
        assert(!ios27_run_executable_exec(paths[3], old, old_size, &old_info,
            &startup, &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 101);
        free(old);
    }
    puts("native Mach-O service exec: virtual file read/write/seek/close, mapping protection, errno, startup vectors, failure teardown and fresh launches passed");
}
#endif

int main(int argc, char **argv) {
    size_t size;
    uint8_t *bytes;
    ios27_macho_info info;
    ios27_exec_layout layout;
    char error[192];
    if (argc == 1) { callbacks(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--no-linker")) {
        puts("named Mach-O service shim requires LLVM ld64.lld");
        return 77;
    }
    assert(argc == 5);
    bytes = read_file(argv[1], &size);
    assert(!ios27_macho_parse(bytes, size, &info, error, sizeof(error)));
    assert(!ios27_macho_executable_layout(bytes, size, &info, 4096u,
                                          &layout, error, sizeof(error)));
    assert(!ios27_macho_executable_layout(bytes, size, &info, 16384u,
                                          &layout, error, sizeof(error)));
#if defined(__aarch64__)
    native_cases(bytes, size, &info, argv);
    free(bytes);
    no_children();
    return 0;
#else
    free(bytes);
    puts("service Mach-O fixture validated at 4/16 KiB; native execution requires ARM64 Linux");
    return 77;
#endif
}
