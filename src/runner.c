#include "ios27/macho.h"
#include "process_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
#include <sys/mman.h>
#include <unistd.h>
#endif

static int fail(char *error, size_t capacity, const char *message) {
    if (error != NULL && capacity != 0u)
        (void)snprintf(error, capacity, "%s", message);
    return -1;
}

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
/* Darwin VM_PROT_* and Linux PROT_* share bits 1,2,4. */
static int protection(uint32_t p) {
    return ((p & 1u) ? PROT_READ : 0) |
           ((p & 2u) ? PROT_WRITE : 0) |
           ((p & 4u) ? PROT_EXEC : 0);
}

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t read64(const uint8_t *p) {
    return (uint64_t)read32(p) | ((uint64_t)read32(p + 4u) << 32u);
}

/* S_ZEROFILL has no meaningful file bytes, even when the segment's
 * filesize is rounded up across the BSS virtual range by ld64.lld. */
static void initialize_zerofill(const uint8_t *file,
                                const ios27_exec_layout *layout,
                                uint8_t *mapping) {
    size_t cursor = 32u;
    uint32_t commands = read32(file + 16u);
    for (uint32_t i = 0u; i < commands; ++i) {
        const uint8_t *cmd = file + cursor;
        if (read32(cmd) == 0x19u) {
            uint32_t sections = read32(cmd + 64u);
            for (uint32_t j = 0u; j < sections; ++j) {
                const uint8_t *sec = cmd + 72u + (size_t)j * 80u;
                if ((read32(sec + 64u) & 0xffu) == 1u) {
                    uint64_t addr = read64(sec + 32u);
                    uint64_t length = read64(sec + 40u);
                    if (length)
                        memset(mapping + (size_t)(addr - layout->min_vmaddr),
                               0, (size_t)length);
                }
            }
        }
        cursor += (size_t)read32(cmd + 4u);
    }
}

/*
 * LC_MAIN uses the Darwin C main ABI, not a Linux process-entry stack.
 * An out-of-line trampoline is mandatory: C compilers assume SP is stable.
 * Preserve host callee-saved state and leave platform register x18 alone.
 * This trusted SDK-free guest must return normally.
 */
__asm__(".text\n"
        ".global ios27_arm64_enter_main\n"
        ".type ios27_arm64_enter_main, %function\n"
        "ios27_arm64_enter_main:\n"
        "stp x29, x30, [sp, #-16]!\n"
        "stp x19, x20, [sp, #-16]!\n"
        "mov x19, sp\n"
        "mov x9, x0\n"
        "mov x10, x1\n"
        "mov x0, x2\n"
        "mov x1, x3\n"
        "mov x2, x4\n"
        "mov x3, x5\n"
        "mov sp, x10\n"
        "blr x9\n"
        "mov sp, x19\n"
        "ldp x19, x20, [sp], #16\n"
        "ldp x29, x30, [sp], #16\n"
        "ret\n"
        ".size ios27_arm64_enter_main, .-ios27_arm64_enter_main\n");

extern int ios27_arm64_enter_main(uintptr_t entry, uintptr_t guest_sp,
                                  int argc, char **argv, char **envp,
                                  char **apple);

#define IOS27_GUEST_MAX_ARGS 128u
#define IOS27_GUEST_MAX_STRING 4096u
#define IOS27_GUEST_MAX_BYTES (64u * 1024u)
#define IOS27_GUEST_DEFAULT_STACK (1024u * 1024u)
#define IOS27_GUEST_MAX_STACK (8u * 1024u * 1024u)

typedef struct {
    uint8_t *reservation;
    size_t size;
    uintptr_t sp;
    char **argv;
    char **envp;
    char **apple;
} ios27_guest_stack;

static void release_guest_stack(ios27_guest_stack *stack) {
    if (stack->reservation)
        (void)munmap(stack->reservation, stack->size);
    memset(stack, 0, sizeof(*stack));
}

typedef struct {
    uintptr_t entry;
    ios27_guest_stack *stack;
    int argc;
} prepared_entry;

static int enter_prepared(void *opaque) {
    const prepared_entry *call = opaque;
    return ios27_arm64_enter_main(call->entry, call->stack->sp, call->argc,
                                  call->stack->argv, call->stack->envp,
                                  call->stack->apple);
}

/* All argument checks precede stack allocation and executable mappings. No
 * host environment is automatically passed to the Darwin image. */
/* Validate every vector and size before making any guest mappings. */
static int prepare_guest_stack(const ios27_startup_context *context,
                               uint64_t requested, size_t page_size,
                               ios27_guest_stack *stack,
                               char *error, size_t error_size) {
    size_t env_count = 0u, apple_count = 0u, payload = 0u, capacity;
    size_t count, vector_bytes;
    char *args[IOS27_GUEST_MAX_ARGS];
    char *env[IOS27_GUEST_MAX_ARGS];
    char *apple[IOS27_GUEST_MAX_ARGS];
    uintptr_t cursor, base;
    uint8_t *mapped;
    memset(stack, 0, sizeof(*stack));
    if (!context || context->argc < 0 ||
        context->argc > (int)IOS27_GUEST_MAX_ARGS ||
        (context->argc != 0 && !context->argv))
        return fail(error, error_size, "invalid guest argc/argv");
    if (context->envp) {
        while (env_count <= IOS27_GUEST_MAX_ARGS &&
               context->envp[env_count])
            ++env_count;
        if (env_count > IOS27_GUEST_MAX_ARGS)
            return fail(error, error_size, "too many guest environment variables");
    }
    if (context->apple) {
        while (apple_count <= IOS27_GUEST_MAX_ARGS &&
               context->apple[apple_count])
            ++apple_count;
        if (apple_count > IOS27_GUEST_MAX_ARGS)
            return fail(error, error_size, "too many guest apple arguments");
    }
    count = (size_t)context->argc;
    vector_bytes = (count + env_count + apple_count + 3u) * sizeof(char *);
    for (size_t i = 0u; i < count + env_count + apple_count; ++i) {
        const char *value = i < count ? context->argv[i] :
                            i < count + env_count ?
                            context->envp[i - count] :
                            context->apple[i - count - env_count];
        const char *end;
        size_t n;
        if (!value)
            return fail(error, error_size, "null guest argument");
        end = memchr(value, '\0', IOS27_GUEST_MAX_STRING);
        if (!end)
            return fail(error, error_size, "guest argument exceeds string limit");
        n = (size_t)(end - value) + 1u;
        if (n > IOS27_GUEST_MAX_BYTES - payload)
            return fail(error, error_size, "guest argument payload too large");
        payload += n;
    }
    if (requested > IOS27_GUEST_MAX_STACK)
        return fail(error, error_size, "LC_MAIN stacksize exceeds trusted limit");
    capacity = requested ? (size_t)requested : IOS27_GUEST_DEFAULT_STACK;
    if (capacity < 65536u)
        capacity = 65536u;
    if (capacity > SIZE_MAX - (page_size - 1u))
        return fail(error, error_size, "guest stack capacity overflow");
    capacity = (capacity + page_size - 1u) & ~(page_size - 1u);
    if (capacity < payload + vector_bytes + 16384u ||
        capacity > SIZE_MAX - 2u * page_size)
        return fail(error, error_size, "guest stack insufficient for arguments");
    stack->size = capacity + 2u * page_size;
    mapped = mmap(NULL, stack->size, PROT_NONE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
        stack->size = 0u;
        return fail(error, error_size, "guest stack reservation failed");
    }
    stack->reservation = mapped;
    if (mprotect(mapped + page_size, capacity, PROT_READ | PROT_WRITE))
        goto failed;
    base = (uintptr_t)(mapped + page_size);
    cursor = base + capacity;
    for (size_t i = count; i > 0u; --i) {
        size_t n = strlen(context->argv[i - 1u]) + 1u;
        cursor -= n;
        memcpy((void *)cursor, context->argv[i - 1u], n);
        args[i - 1u] = (char *)cursor;
    }
    for (size_t i = env_count; i > 0u; --i) {
        size_t n = strlen(context->envp[i - 1u]) + 1u;
        cursor -= n;
        memcpy((void *)cursor, context->envp[i - 1u], n);
        env[i - 1u] = (char *)cursor;
    }
    for (size_t i = apple_count; i > 0u; --i) {
        size_t n = strlen(context->apple[i - 1u]) + 1u;
        cursor -= n;
        memcpy((void *)cursor, context->apple[i - 1u], n);
        apple[i - 1u] = (char *)cursor;
    }
    cursor &= ~(uintptr_t)15u;
    cursor -= vector_bytes;
    cursor &= ~(uintptr_t)15u;
    if (cursor < base + 16384u)
        goto failed;
    stack->sp = cursor;
    stack->argv = (char **)cursor;
    stack->envp = stack->argv + count + 1u;
    stack->apple = stack->envp + env_count + 1u;
    for (size_t i = 0u; i < count; ++i)
        stack->argv[i] = args[i];
    stack->argv[count] = NULL;
    for (size_t i = 0u; i < env_count; ++i)
        stack->envp[i] = env[i];
    stack->envp[env_count] = NULL;
    for (size_t i = 0u; i < apple_count; ++i)
        stack->apple[i] = apple[i];
    stack->apple[apple_count] = NULL;
    return 0;
failed:
    release_guest_stack(stack);
    return fail(error, error_size, "guest stack protection/setup failed");
}
#endif

static int run_executable_context(const uint8_t *data, size_t size,
                                 const ios27_macho_info *info,
                                 const ios27_startup_context *context,
                                 int *exit_code,
                                 const ios27_process_options *options,
                                 ios27_process_result *process_result, char *error,
                                 size_t error_size) {
#if !defined(__aarch64__) || (!defined(__linux__) && !defined(__ANDROID__))
    (void)data; (void)size; (void)info; (void)exit_code;
    (void)context; (void)options; (void)process_result;
    return fail(error, error_size, "execution requires ARM64 Linux/Android");
#else
    ios27_exec_layout layout;
    long os_page = sysconf(_SC_PAGESIZE);
    uint8_t *mapping;
    ios27_guest_stack guest;
    if (!data || !info || !context || (!exit_code && !process_result) || os_page <= 0)
        return fail(error, error_size, "invalid native runtime input");
    /* Fail closed on unsupported host pages, stale metadata or W^X ambiguity.
     * All checks complete before any executable memory becomes accessible. */
    if (ios27_macho_executable_layout(data, size, info, (size_t)os_page,
                                      &layout, error, error_size))
        return -1;
    if (prepare_guest_stack(context, info->stacksize,
                            layout.page_size, &guest, error, error_size))
        return -1;
    mapping = mmap(NULL, layout.span, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        release_guest_stack(&guest);
        return fail(error, error_size, "native VM reservation failed");
    }

    for (size_t i = 0u; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        uint64_t aligned_start, aligned_end;
        uint8_t *staging, *target;
        size_t bytes;
        if (s->vmsize == 0u || strcmp(s->name, "__PAGEZERO") == 0)
            continue;
        aligned_start = s->vmaddr & ~((uint64_t)layout.page_size - 1u);
        aligned_end = (s->vmaddr + s->vmsize +
                       (uint64_t)layout.page_size - 1u) &
                      ~((uint64_t)layout.page_size - 1u);
        bytes = (size_t)(aligned_end - aligned_start);
        staging = mapping + (size_t)(aligned_start - layout.min_vmaddr);
        target = mapping + (size_t)(s->vmaddr - layout.min_vmaddr);
        /* A private anonymous reservation provides zero-filled tails and gaps.
         * Writable staging is never executable; final pages are never W+X. */
        if (mprotect(staging, bytes, PROT_READ | PROT_WRITE) != 0)
            goto mapping_failed;
        if (s->filesize != 0u)
            memcpy(target, data + (size_t)s->fileoff, (size_t)s->filesize);
        if (s->initprot & 4u)
            __builtin___clear_cache((char *)target,
                                    (char *)(target + (size_t)s->filesize));
    }
    initialize_zerofill(data, &layout, mapping);
    /*
     * Commit the complete image only after every segment has been populated.
     * No guest code is executable during staging, and all pages get their
     * final protection before LC_MAIN is called. A failed seal unmaps the
     * entire reservation, including any segments already made executable.
     */
    for (size_t i = 0u; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        uint64_t start, end;
        if (!s->vmsize || strcmp(s->name, "__PAGEZERO") == 0)
            continue;
        start = s->vmaddr & ~((uint64_t)layout.page_size - 1u);
        end = (s->vmaddr + s->vmsize +
               (uint64_t)layout.page_size - 1u) &
              ~((uint64_t)layout.page_size - 1u);
        if (mprotect(mapping + (size_t)(start - layout.min_vmaddr),
                     (size_t)(end - start), protection(s->initprot)) != 0)
            goto mapping_failed;
    }
    {
        uintptr_t entry = (uintptr_t)(mapping +
                            (size_t)(layout.entry_vmaddr - layout.min_vmaddr));
        prepared_entry call = {entry, &guest, context->argc};
        ios27_process_result completed = {0};
        int result = 0, status = 0;
        if (options) {
            status = ios27_process_supervise(enter_prepared, &call, options,
                                             &completed, error, error_size);
        } else {
            /* The original API still requires an ordinary in-process return. */
            result = enter_prepared(&call);
        }
        /* Publish the result only after both guest reservations are gone. */
        int image_unmapped = munmap(mapping, layout.span);
        int stack_unmapped = munmap(guest.reservation, guest.size);
        if (image_unmapped || stack_unmapped)
            return fail(error, error_size, "native process cleanup failed");
        if (status) return -1;
        if (process_result) *process_result = completed;
        else *exit_code = result;
    }
    if (error && error_size) error[0] = '\0';
    return 0;

mapping_failed:
    (void)munmap(mapping, layout.span); /* unwind the entire reservation */
    release_guest_stack(&guest);
    return fail(error, error_size, "native segment mapping/protection failed");
#endif
}

int ios27_run_executable_context(const uint8_t *data, size_t size,
                                 const ios27_macho_info *info,
                                 const ios27_startup_context *context,
                                 int *exit_code, char *error, size_t error_size) {
    return run_executable_context(data, size, info, context, exit_code,
                                   NULL, NULL, error, error_size);
}

int ios27_run_executable_contained(const uint8_t *data, size_t size,
                                    const ios27_macho_info *info,
                                    const ios27_startup_context *context,
                                    const ios27_process_options *options,
                                    ios27_process_result *result,
                                    char *error, size_t error_size) {
    if (!result) return fail(error, error_size, "null child result");
    memset(result, 0, sizeof(*result));
    if (!options || !options->timeout_ms ||
        options->timeout_ms > IOS27_PROCESS_MAX_TIMEOUT_MS)
        return fail(error, error_size, "invalid child deadline");
    return run_executable_context(data, size, info, context, NULL,
                                   options, result, error, error_size);
}

/* Older callers use an empty apple vector; no host information is leaked. */
int ios27_run_executable_argv(const uint8_t *data, size_t size,
                             const ios27_macho_info *info,
                             int argc, const char *const argv[],
                             const char *const envp[],
                             int *exit_code, char *error, size_t error_size) {
    ios27_startup_context context = {argc, argv, envp, NULL};
    return ios27_run_executable_context(data, size, info, &context,
                                        exit_code, error, error_size);
}

/* Preserve the no-argument API for existing compiler-produced fixtures. */
int ios27_run_freestanding(const uint8_t *data, size_t size,
                          const ios27_macho_info *info,
                          int *exit_code, char *error, size_t error_size) {
    return ios27_run_executable_argv(data, size, info, 0, NULL, NULL,
                                      exit_code, error, error_size);
}
