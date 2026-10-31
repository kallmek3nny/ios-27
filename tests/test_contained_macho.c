#include "ios27/darwin_object.h"
#include "ios27/process.h"

#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    assert(file && !fseek(file, 0, SEEK_END));
    length = ftell(file);
    assert(length >= 32 && length <= 2 * 1024 * 1024);
    assert(!fseek(file, 0, SEEK_SET));
    bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1u, (size_t)length, file) == (size_t)length);
    assert(!fclose(file));
    *size = (size_t)length;
    return bytes;
}

#if defined(__aarch64__)
static void put32(uint8_t *where, uint32_t value) {
    for (unsigned i = 0u; i < 4u; ++i) where[i] = (uint8_t)(value >> (8u * i));
}
#endif

static void cleared(const ios27_process_result *result) {
    assert(result->outcome == 0 && result->exit_code == 0 && result->signal == 0);
}

/* A NULL loader selects the fork-based ios27_run_executable_contained;
 * otherwise the project's trusted ios27-loader build output is exec'd into a
 * fresh address space by ios27_run_executable_exec. Both runners share one
 * contract, so every case below runs against both. */
static int run_mode(const char *loader, const uint8_t *bytes, size_t size,
                    const ios27_macho_info *info,
                    const ios27_startup_context *context,
                    const ios27_process_options *options,
                    ios27_process_result *result, char *error,
                    size_t error_size) {
    if (loader)
        return ios27_run_executable_exec(loader, bytes, size, info, context,
                                         options, result, error, error_size);
    return ios27_run_executable_contained(bytes, size, info, context, options,
                                          result, error, error_size);
}

static const char *mode_name(const char *loader) {
    return loader ? "exec-loader" : "fork";
}

/* Exercise the original compiler-linked phase19a workload without changing
 * its code. It recursively uses its private stack, checks every startup
 * vector, and writes argv/envp/apple and globals before returning 42. In
 * exec mode the vectors cross execveat inside the sealed launch payload. */
static void process_context_test(const char *path, const char *loader) {
    size_t size;
    uint8_t *bytes = read_file(path, &size);
    ios27_macho_info info;
    ios27_exec_layout layout;
    const char *args[] = {"ios27", "oak", "pine", "cedar"};
    const char *env[] = {"MODE=phase19a", "REPEAT=1", NULL};
    const char *apple[] = {"executable_path=/ios27/phase19a", "ios27.mode=native", NULL};
    ios27_startup_context context = {4, args, env, apple};
    ios27_process_options options = {1000u};
    ios27_process_result result;
    char error[160];
    assert(!ios27_macho_parse(bytes, size, &info, error, sizeof(error)));
    assert(!ios27_macho_executable_layout(bytes, size, &info, 4096u,
                                          &layout, error, sizeof(error)));
    assert(!ios27_macho_executable_layout(bytes, size, &info, 16384u,
                                          &layout, error, sizeof(error)));
#if defined(__aarch64__)
    for (unsigned i = 0u; i < 16u; ++i) {
        assert(!run_mode(loader, bytes, size, &info, &context,
                         &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 42);
        assert(result.signal == 0 && error[0] == '\0');
        assert(!strcmp(args[1], "oak") && !strcmp(env[0], "MODE=phase19a"));
        assert(!strcmp(apple[0], "executable_path=/ios27/phase19a"));
    }
    {
        ios27_startup_context empty = {0, NULL, NULL, NULL};
        assert(!run_mode(loader, bytes, size, &info, &empty,
                         &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 101);
    }
    {
        const char *wrong[] = {"MODE=wrong", "REPEAT=1", NULL};
        ios27_startup_context changed = context;
        changed.envp = wrong;
        assert(!run_mode(loader, bytes, size, &info, &changed,
                         &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 104);
    }
    {
        const char *wrong[] = {"wrong=path", "ios27.mode=native", NULL};
        ios27_startup_context changed = context;
        changed.apple = wrong;
        assert(!run_mode(loader, bytes, size, &info, &changed,
                         &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 105);
    }
    printf("native ARM64 %s child: unmodified phase19a startup vectors, recursion, "
           "private copies and fresh globals passed\n", mode_name(loader));
#else
    memset(&result, 0xff, sizeof(result));
    assert(run_mode(loader, bytes, size, &info, &context,
                    &options, &result, error, sizeof(error)) == -1);
    cleared(&result);
#endif
    {
        ios27_startup_context invalid = context;
        invalid.argc = -1;
        memset(&result, 0xff, sizeof(result));
        assert(run_mode(loader, bytes, size, &info, &invalid,
                        &options, &result, error, sizeof(error)) == -1);
        cleared(&result);
        assert(error[0]);
    }
    {
        ios27_macho_info stale = info;
        stale.entryoff += 4u;
        memset(&result, 0xff, sizeof(result));
        assert(run_mode(loader, bytes, size, &stale, &context,
                        &options, &result, error, sizeof(error)) == -1);
        cleared(&result);
        assert(error[0]);
    }
    free(bytes);
}

#if defined(__aarch64__)
/* Explicit synthetic instruction injections test failure containment; the
 * unmodified 43-returning workload is genuinely compiler-linked. The exec
 * deadline also covers execveat and the loader's re-parse, so its loop case
 * gets a longer budget to reach the guest before the deadline expires. */
static void native_failure_test(const char *loader, const uint8_t *bytes,
                                size_t size, const ios27_macho_info *info,
                                size_t entry) {
    ios27_startup_context context = {0, NULL, NULL, NULL};
    ios27_process_options options = {1000u};
    ios27_process_result result;
    char error[160];
    uint8_t *mutated = malloc(size);
    assert(mutated && entry <= size && size - entry >= 12u);
    for (unsigned i = 0u; i < 16u; ++i) {
        assert(!run_mode(loader, bytes, size, info, &context,
                         &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 43);
    }
    memcpy(mutated, bytes, size);
    put32(mutated + entry, UINT32_C(0xd4200000)); /* BRK #0 */
    assert(!run_mode(loader, mutated, size, info, &context,
                     &options, &result, error, sizeof(error)));
    assert(result.outcome == IOS27_PROCESS_SIGNALED && result.signal == SIGTRAP);
    put32(mutated + entry, UINT32_C(0x14000000)); /* B . */
    options.timeout_ms = loader ? 250u : 40u;
    assert(!run_mode(loader, mutated, size, info, &context,
                     &options, &result, error, sizeof(error)));
    assert(result.outcome == IOS27_PROCESS_TIMED_OUT && result.signal == SIGKILL);
    options.timeout_ms = 1000u;
    put32(mutated + entry, UINT32_C(0xd2801588)); /* MOV X8, #172: Linux getpid */
    put32(mutated + entry + 4u, UINT32_C(0xd4000001)); /* SVC #0 */
    put32(mutated + entry + 8u, UINT32_C(0xd65f03c0)); /* RET */
    assert(!run_mode(loader, mutated, size, info, &context,
                     &options, &result, error, sizeof(error)));
    assert(result.outcome == IOS27_PROCESS_SIGNALED && result.signal == SIGSYS);
    /* Argument register delivery still uses the existing Darwin trampoline. */
    memcpy(mutated, bytes, size);
    put32(mutated + entry, UINT32_C(0xd65f03c0)); /* RET, preserving W0=argc */
    {
        const char *args[] = {"one", "two", "three"};
        context.argc = 3;
        context.argv = args;
        assert(!run_mode(loader, mutated, size, info, &context,
                         &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 3);
    }
    context.argc = 0;
    context.argv = NULL;
    /* The parent and loader remain usable after every abnormal child. */
    assert(!run_mode(loader, bytes, size, info, &context,
                     &options, &result, error, sizeof(error)));
    assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 43);
    free(mutated);
    printf("native ARM64 Mach-O %s child: real linked return=43, injected "
           "fault/loop/syscall, argc and recovery passed\n", mode_name(loader));
}
#else
static void unsupported_host_test(const char *loader, const uint8_t *bytes,
                                  size_t size, const ios27_macho_info *info) {
    ios27_startup_context context = {0, NULL, NULL, NULL};
    ios27_process_options options = {1000u};
    ios27_process_result result;
    char error[160];
    memset(&result, 0xff, sizeof(result));
    assert(run_mode(loader, bytes, size, info, &context,
                    &options, &result, error, sizeof(error)) == -1);
    cleared(&result);
    assert(error[0]);
    printf("contained Mach-O %s parsing passed; native entry requires ARM64 Linux\n",
           mode_name(loader));
}
#endif

/* Invalid deadlines and missing outputs are rejected with a cleared result. */
static void options_test(const char *loader, const uint8_t *bytes, size_t size,
                         const ios27_macho_info *info) {
    ios27_startup_context context = {0, NULL, NULL, NULL};
    ios27_process_options options = {0u};
    ios27_process_result result;
    char error[160];
    memset(&result, 0xff, sizeof(result));
    assert(run_mode(loader, bytes, size, info, &context,
                    &options, &result, error, sizeof(error)) == -1);
    cleared(&result);
    options.timeout_ms = IOS27_PROCESS_MAX_TIMEOUT_MS + 1u;
    memset(&result, 0xff, sizeof(result));
    assert(run_mode(loader, bytes, size, info, &context,
                    &options, &result, error, sizeof(error)) == -1);
    cleared(&result);
    memset(&result, 0xff, sizeof(result));
    assert(run_mode(loader, bytes, size, info, &context,
                    NULL, &result, error, sizeof(error)) == -1);
    cleared(&result);
    options.timeout_ms = 1000u;
    assert(run_mode(loader, bytes, size, info, &context,
                    &options, NULL, error, sizeof(error)) == -1);
}

/* Exec mode only runs the named trusted loader: a missing path, a missing
 * file or a non-ELF image (the Mach-O fixture itself) must fail closed. */
static void loader_path_test(const char *not_loader, const uint8_t *bytes,
                             size_t size, const ios27_macho_info *info) {
    const char *paths[] = {NULL, "", "/nonexistent/ios27-loader", not_loader};
    ios27_startup_context context = {0, NULL, NULL, NULL};
    ios27_process_options options = {1000u};
    ios27_process_result result;
    char error[160];
    for (unsigned i = 0u; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        error[0] = '\0';
        memset(&result, 0xff, sizeof(result));
        assert(ios27_run_executable_exec(paths[i], bytes, size, info, &context,
                                         &options, &result, error,
                                         sizeof(error)) == -1);
        cleared(&result);
        assert(error[0]);
    }
}

int main(int argc, char **argv) {
    size_t size;
    uint8_t *bytes;
    ios27_macho_info info;
    ios27_darwin_executable executable;
    ios27_exec_layout layout;
    const char *modes[2];
    char error[160];
    assert(argc == 4);
    modes[0] = NULL;
    modes[1] = argv[3];
    bytes = read_file(argv[1], &size);
    assert(!ios27_macho_parse(bytes, size, &info, error, sizeof(error)));
    assert(!ios27_darwin_executable_inspect(bytes, size, &executable, error, sizeof(error)));
    /* phase15a is only required to have a 4 KiB-granular layout, as in
     * compiled-darwin-real-mh-execute. CI shows its linked segments are
     * rejected at 16 KiB granularity, so no 16 KiB host layout is claimed
     * here; phase19a's 16 KiB preflight is asserted in process_context_test. */
    assert(!ios27_macho_executable_layout(bytes, size, &info, 4096u,
                                          &layout, error, sizeof(error)));
    for (unsigned m = 0u; m < 2u; ++m) {
#if defined(__aarch64__)
        native_failure_test(modes[m], bytes, size, &info,
                            (size_t)executable.entry_file_offset);
#else
        unsupported_host_test(modes[m], bytes, size, &info);
#endif
        options_test(modes[m], bytes, size, &info);
    }
    loader_path_test(argv[1], bytes, size, &info);
    free(bytes);
    for (unsigned m = 0u; m < 2u; ++m)
        process_context_test(argv[2], modes[m]);
    return 0;
}
