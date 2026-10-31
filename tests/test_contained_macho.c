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

/* Exercise the original compiler-linked phase19a workload without changing
 * its code. It recursively uses its private stack, checks every startup
 * vector, and writes argv/envp/apple and globals before returning 42. */
static void process_context_test(const char *path) {
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
        assert(!ios27_run_executable_contained(bytes, size, &info, &context,
                                                &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 42);
        assert(result.signal == 0 && error[0] == '\0');
        assert(!strcmp(args[1], "oak") && !strcmp(env[0], "MODE=phase19a"));
        assert(!strcmp(apple[0], "executable_path=/ios27/phase19a"));
    }
    {
        ios27_startup_context empty = {0, NULL, NULL, NULL};
        assert(!ios27_run_executable_contained(bytes, size, &info, &empty,
                                                &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 101);
    }
    {
        const char *wrong[] = {"MODE=wrong", "REPEAT=1", NULL};
        ios27_startup_context changed = context;
        changed.envp = wrong;
        assert(!ios27_run_executable_contained(bytes, size, &info, &changed,
                                                &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 104);
    }
    {
        const char *wrong[] = {"wrong=path", "ios27.mode=native", NULL};
        ios27_startup_context changed = context;
        changed.apple = wrong;
        assert(!ios27_run_executable_contained(bytes, size, &info, &changed,
                                                &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 105);
    }
    puts("native ARM64 child: unmodified phase19a startup vectors, recursion, private copies and fresh globals passed");
#else
    assert(ios27_run_executable_contained(bytes, size, &info, &context,
                                           &options, &result, error, sizeof(error)) == -1);
    cleared(&result);
#endif
    {
        ios27_startup_context invalid = context;
        invalid.argc = -1;
        memset(&result, 0xff, sizeof(result));
        assert(ios27_run_executable_contained(bytes, size, &info, &invalid,
                                               &options, &result, error, sizeof(error)) == -1);
        cleared(&result);
        assert(error[0]);
    }
    {
        ios27_macho_info stale = info;
        stale.entryoff += 4u;
        memset(&result, 0xff, sizeof(result));
        assert(ios27_run_executable_contained(bytes, size, &stale, &context,
                                               &options, &result, error, sizeof(error)) == -1);
        cleared(&result);
        assert(error[0]);
    }
    free(bytes);
}

int main(int argc, char **argv) {
    size_t size;
    uint8_t *bytes;
    ios27_macho_info info;
    ios27_darwin_executable executable;
    ios27_exec_layout layout;
    ios27_startup_context context = {0, NULL, NULL, NULL};
    ios27_process_options options = {1000u};
    ios27_process_result result;
    char error[160];
    assert(argc == 3);
    bytes = read_file(argv[1], &size);
    assert(!ios27_macho_parse(bytes, size, &info, error, sizeof(error)));
    assert(!ios27_darwin_executable_inspect(bytes, size, &executable, error, sizeof(error)));
    assert(!ios27_macho_executable_layout(bytes, size, &info, 4096u,
                                          &layout, error, sizeof(error)));
    assert(!ios27_macho_executable_layout(bytes, size, &info, 16384u,
                                          &layout, error, sizeof(error)));
#if defined(__aarch64__)
    {
        uint8_t *mutated = malloc(size);
        size_t entry = (size_t)executable.entry_file_offset;
        assert(mutated && entry <= size && size - entry >= 12u);
        for (unsigned i = 0u; i < 16u; ++i) {
            assert(!ios27_run_executable_contained(bytes, size, &info, &context,
                                                    &options, &result, error, sizeof(error)));
            assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 43);
        }
        /* Explicit synthetic instruction injections test failure containment;
         * the unmodified 43-returning workload above is genuinely compiler-linked. */
        memcpy(mutated, bytes, size);
        put32(mutated + entry, UINT32_C(0xd4200000)); /* BRK #0 */
        assert(!ios27_run_executable_contained(mutated, size, &info, &context,
                                                &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_SIGNALED && result.signal == SIGTRAP);
        put32(mutated + entry, UINT32_C(0x14000000)); /* B . */
        options.timeout_ms = 40u;
        assert(!ios27_run_executable_contained(mutated, size, &info, &context,
                                                &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_TIMED_OUT && result.signal == SIGKILL);
        options.timeout_ms = 1000u;
        put32(mutated + entry, UINT32_C(0xd2801588)); /* MOV X8, #172: Linux getpid */
        put32(mutated + entry + 4u, UINT32_C(0xd4000001)); /* SVC #0 */
        put32(mutated + entry + 8u, UINT32_C(0xd65f03c0)); /* RET */
        assert(!ios27_run_executable_contained(mutated, size, &info, &context,
                                                &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_SIGNALED && result.signal == SIGSYS);
        /* Argument register delivery still uses the existing Darwin trampoline. */
        memcpy(mutated, bytes, size);
        put32(mutated + entry, UINT32_C(0xd65f03c0)); /* RET, preserving W0=argc */
        {
            const char *args[] = {"one", "two", "three"};
            context.argc = 3;
            context.argv = args;
            assert(!ios27_run_executable_contained(mutated, size, &info, &context,
                                                    &options, &result, error, sizeof(error)));
            assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 3);
        }
        context.argc = 0;
        context.argv = NULL;
        /* The parent and loader remain usable after every abnormal child. */
        assert(!ios27_run_executable_contained(bytes, size, &info, &context,
                                                &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 43);
        free(mutated);
        puts("native ARM64 Mach-O child: real linked return=43, injected fault/loop/syscall, argc and recovery passed");
    }
#else
    assert(ios27_run_executable_contained(bytes, size, &info, &context,
                                           &options, &result, error, sizeof(error)) == -1);
    assert(result.outcome == 0 && error[0]);
    puts("contained Mach-O parsing passed; native entry requires ARM64 Linux");
#endif
    options.timeout_ms = 0u;
    memset(&result, 0xff, sizeof(result));
    assert(ios27_run_executable_contained(bytes, size, &info, &context,
                                           &options, &result, error, sizeof(error)) == -1);
    cleared(&result);
    options.timeout_ms = IOS27_PROCESS_MAX_TIMEOUT_MS + 1u;
    memset(&result, 0xff, sizeof(result));
    assert(ios27_run_executable_contained(bytes, size, &info, &context,
                                           &options, &result, error, sizeof(error)) == -1);
    cleared(&result);
    memset(&result, 0xff, sizeof(result));
    assert(ios27_run_executable_contained(bytes, size, &info, &context,
                                           NULL, &result, error, sizeof(error)) == -1);
    cleared(&result);
    assert(ios27_run_executable_contained(bytes, size, &info, &context,
                                           &options, NULL, error, sizeof(error)) == -1);
    free(bytes);
    process_context_test(argv[2]);
    return 0;
}
