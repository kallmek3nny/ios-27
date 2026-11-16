#define _GNU_SOURCE
#include "guest_stack_internal.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Host-only tests of the production stack builder. No guest instructions,
 * signals, faults, subprocesses or changes to the host SP are involved.
 * Linker wrappers force a host-page-aligned but selected-page-misaligned
 * reservation, and deterministic mmap/mprotect failures. They are not linked
 * into the runner, loader or Android application. */
void *__real_mmap(void *, size_t, int, int, int, off_t);
int __real_mprotect(void *, size_t, int);
int __real_munmap(void *, size_t);

static struct {
    size_t host_page;
    size_t force_page;
    size_t maps;
    size_t protects;
    size_t unmaps;
    size_t live;
    uintptr_t last_map;
    size_t last_size;
    uintptr_t last_protect;
    size_t last_protect_size;
    int last_prot;
    int fail_map;
    int fail_protect;
} hooks;

void *__wrap_mmap(void *address, size_t length, int prot, int flags,
                  int fd, off_t offset) {
    uint8_t *mapped;
    size_t extra = hooks.force_page > hooks.host_page ? hooks.force_page : 0u;
    ++hooks.maps;
    assert(!address && length && length % hooks.host_page == 0u);
    assert(prot == PROT_NONE && flags == (MAP_PRIVATE | MAP_ANONYMOUS));
    assert(fd == -1 && offset == 0 && length <= SIZE_MAX - extra);
    if (hooks.fail_map) {
        hooks.fail_map = 0;
        errno = ENOMEM;
        return MAP_FAILED;
    }
    mapped = __real_mmap(NULL, length + extra, prot, flags, fd, offset);
    assert(mapped != MAP_FAILED);
    if (extra) {
        size_t prefix = (extra - (uintptr_t)mapped % extra) % extra + hooks.host_page;
        assert(prefix <= extra);
        assert(__real_munmap(mapped, prefix) == 0);
        mapped += prefix;
        if (extra > prefix)
            assert(__real_munmap(mapped + length, extra - prefix) == 0);
        assert((uintptr_t)mapped % extra == hooks.host_page);
    }
    hooks.last_map = (uintptr_t)mapped;
    hooks.last_size = length;
    ++hooks.live;
    return mapped;
}

int __wrap_mprotect(void *address, size_t length, int prot) {
    ++hooks.protects;
    hooks.last_protect = (uintptr_t)address;
    hooks.last_protect_size = length;
    hooks.last_prot = prot;
    assert(prot == (PROT_READ | PROT_WRITE));
    if (hooks.fail_protect) {
        hooks.fail_protect = 0;
        errno = EACCES;
        return -1;
    }
    return __real_mprotect(address, length, prot);
}

int __wrap_munmap(void *address, size_t length) {
    int result = __real_munmap(address, length);
    ++hooks.unmaps;
    assert(result == 0 && hooks.live);
    --hooks.live;
    return result;
}

static void cleared(const ios27_guest_stack *stack) {
    const uint8_t *bytes = (const uint8_t *)stack;
    for (size_t i = 0u; i < sizeof(*stack); ++i) assert(bytes[i] == 0u);
}

static void unmapped(uintptr_t address, size_t length) {
    unsigned char resident;
    for (size_t i = 0u; i < length; i += hooks.host_page) {
        errno = 0;
        assert(mincore((void *)(address + i), hooks.host_page, &resident) == -1);
        assert(errno == ENOMEM);
    }
}

/* Read the real kernel map instead of deliberately faulting a guard page.
 * Every real page, including alignment slack, must have the correct mode. */
static void permissions(const ios27_guest_stack *stack) {
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[512], mode[5];
    uintptr_t start, end, cursor = (uintptr_t)stack->reservation;
    uintptr_t limit = cursor + stack->size;
    uintptr_t base = (uintptr_t)stack->base;
    assert(maps);
    while (fgets(line, sizeof(line), maps) && cursor < limit) {
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4s",
                   &start, &end, mode) != 3 || cursor < start || cursor >= end)
            continue;
        while (cursor < end && cursor < limit) {
            const char *expected = cursor >= base && cursor < base + stack->capacity ?
                                   "rw-p" : "---p";
            assert(!strcmp(mode, expected));
            cursor += hooks.host_page;
        }
    }
    assert(fclose(maps) == 0 && cursor == limit);
}

static size_t count_vector(const char *const *values) {
    size_t count = 0u;
    if (values) while (values[count]) ++count;
    return count;
}

static void vector_copy(char *const *actual, const char *const *source,
                         size_t count, const ios27_guest_stack *stack) {
    uintptr_t base = (uintptr_t)stack->base, end = base + stack->capacity;
    assert((uintptr_t)actual >= stack->sp && (uintptr_t)actual < end);
    assert((count + 1u) * sizeof(*actual) <= end - (uintptr_t)actual);
    for (size_t i = 0u; i < count; ++i) {
        size_t length = strlen(source[i]) + 1u;
        assert((uintptr_t)actual[i] > (uintptr_t)actual);
        assert((uintptr_t)actual[i] >= base && (uintptr_t)actual[i] < end);
        assert(length <= end - (uintptr_t)actual[i]);
        assert(actual[i] != source[i] && !memcmp(actual[i], source[i], length));
        /* Repeated host pointers still get independent guest copies. */
        for (size_t j = 0u; j < i; ++j) assert(actual[i] != actual[j]);
    }
    assert(actual[count] == NULL);
}

static void check_stack(const ios27_startup_context *context,
                         uint64_t requested, size_t page_size) {
    ios27_guest_stack stack;
    size_t page = page_size ? page_size : hooks.host_page;
    size_t capacity = requested ? (size_t)requested : 1024u * 1024u;
    size_t env_count = count_vector(context->envp);
    size_t apple_count = count_vector(context->apple);
    size_t before_maps = hooks.maps, before_protects = hooks.protects;
    size_t before_unmaps = hooks.unmaps, before_live = hooks.live;
    uintptr_t reservation;
    size_t reservation_size;
    char error[160] = "stale error";
    if (capacity < 65536u) capacity = 65536u;
    capacity = (capacity + page - 1u) & ~(page - 1u);
    hooks.force_page = page;
    memset(&stack, 0xa5, sizeof(stack));
    assert(ios27_guest_stack_prepare(context, requested, page_size, &stack,
                                      error, sizeof(error)) == 0);
    assert(!error[0] && stack.reservation && stack.base);
    if ((uintptr_t)stack.base % page)
        fprintf(stderr, "guest stack base is not aligned to selected page %zu\n", page);
    assert((uintptr_t)stack.base % page == 0u);
    assert(stack.page_size == page && stack.capacity == capacity);
    assert(stack.size == capacity + 2u * page + page - hooks.host_page);
    assert((uintptr_t)stack.base >= (uintptr_t)stack.reservation + page);
    assert((uintptr_t)stack.base + capacity + page <=
           (uintptr_t)stack.reservation + stack.size);
    assert(hooks.maps == before_maps + 1u && hooks.protects == before_protects + 1u);
    assert(hooks.live == before_live + 1u);
    assert(hooks.last_map == (uintptr_t)stack.reservation && hooks.last_size == stack.size);
    assert(hooks.last_protect == (uintptr_t)stack.base);
    assert(hooks.last_protect_size == capacity && hooks.last_prot == (PROT_READ | PROT_WRITE));
    assert(stack.sp % 16u == 0u && stack.sp >= (uintptr_t)stack.base + 16384u);
    assert(stack.sp < (uintptr_t)stack.base + capacity);
    assert((uintptr_t)stack.argv == stack.sp);
    assert(stack.envp == stack.argv + context->argc + 1u);
    assert(stack.apple == stack.envp + env_count + 1u);
    vector_copy(stack.argv, context->argv, (size_t)context->argc, &stack);
    vector_copy(stack.envp, context->envp, env_count, &stack);
    vector_copy(stack.apple, context->apple, apple_count, &stack);
    permissions(&stack);
    for (size_t i = 0u; i < stack.sp - (uintptr_t)stack.base; ++i)
        assert(stack.base[i] == 0u);
    if (context->argc && context->argv[0][0]) {
        char original = context->argv[0][0];
        stack.argv[0][0] ^= 1;
        assert(context->argv[0][0] == original);
        if (context->argc > 1 && context->argv[0] == context->argv[1])
            assert(stack.argv[1][0] == original);
    }
    reservation = (uintptr_t)stack.reservation;
    reservation_size = stack.size;
    ios27_guest_stack_release(&stack);
    cleared(&stack);
    assert(hooks.unmaps == before_unmaps + 1u && hooks.live == before_live);
    unmapped(reservation, reservation_size);
    ios27_guest_stack_release(&stack);
    ios27_guest_stack_release(NULL);
    assert(hooks.unmaps == before_unmaps + 1u);
}

static void rejected(const ios27_startup_context *context,
                      uint64_t requested, size_t page) {
    ios27_guest_stack stack;
    size_t before_maps = hooks.maps, before_protects = hooks.protects;
    size_t before_unmaps = hooks.unmaps;
    char error[6] = {'L', 'x', 'x', 'x', 'x', 'R'};
    memset(&stack, 0xa5, sizeof(stack));
    assert(ios27_guest_stack_prepare(context, requested, page, &stack,
                                      error + 1u, 4u) == -1);
    assert(error[0] == 'L' && error[1] != '\0' && error[4] == '\0' && error[5] == 'R');
    cleared(&stack);
    assert(hooks.maps == before_maps && hooks.protects == before_protects);
    assert(hooks.unmaps == before_unmaps && !hooks.live);
}

static void invalid_inputs(void) {
    const char *args[] = {"guest", "argument"};
    const char *env[] = {"KEY=value", NULL};
    const char *apple[] = {"explicit=value", NULL};
    const char *many[IOS27_STARTUP_MAX_VECTOR + 2u];
    const char *null_arg[] = {NULL};
    char unterminated[IOS27_STARTUP_MAX_STRING];
    const char *long_arg[] = {unterminated, NULL};
    const size_t bad_pages[] = {1u, 2048u, 4095u, 4097u, 12288u, 65537u, 131072u, SIZE_MAX};
    ios27_startup_context valid = {2, args, env, apple}, invalid;
    ios27_guest_stack stack;
    char error[2] = {'x', 'y'};
    size_t before_maps = hooks.maps;
    rejected(NULL, 0u, 0u);
    invalid = valid; invalid.argc = -1; rejected(&invalid, 0u, 0u);
    invalid.argc = (int)IOS27_STARTUP_MAX_VECTOR + 1; rejected(&invalid, 0u, 0u);
    invalid = valid; invalid.argv = NULL; rejected(&invalid, 0u, 0u);
    invalid = valid; invalid.argc = 1; invalid.argv = null_arg; rejected(&invalid, 0u, 0u);
    for (size_t i = 0u; i <= IOS27_STARTUP_MAX_VECTOR; ++i) many[i] = "";
    many[IOS27_STARTUP_MAX_VECTOR + 1u] = NULL;
    invalid = valid; invalid.envp = many; rejected(&invalid, 0u, 0u);
    invalid = valid; invalid.apple = many; rejected(&invalid, 0u, 0u);
    many[IOS27_STARTUP_MAX_VECTOR - 1u] = NULL;
    invalid = valid; invalid.argc = (int)IOS27_STARTUP_MAX_VECTOR; invalid.argv = many;
    rejected(&invalid, 0u, 0u);
    memset(unterminated, 'X', sizeof(unterminated));
    invalid = valid; invalid.argc = 1; invalid.argv = long_arg; rejected(&invalid, 0u, 0u);
    invalid = valid; invalid.envp = long_arg; rejected(&invalid, 0u, 0u);
    invalid = valid; invalid.apple = long_arg; rejected(&invalid, 0u, 0u);
    rejected(&valid, 8u * 1024u * 1024u + 1u, 0u);
    rejected(&valid, UINT64_MAX, 0u);
    for (size_t i = 0u; i < sizeof(bad_pages) / sizeof(bad_pages[0]); ++i)
        rejected(&valid, 0u, bad_pages[i]);
    for (size_t page = 4096u; page < hooks.host_page; page *= 2u)
        rejected(&valid, 0u, page);
    assert(ios27_guest_stack_prepare(&valid, 0u, 0u, NULL, error, sizeof(error)) == -1);
    assert(error[0] != '\0' && error[1] == '\0');
    assert(ios27_guest_stack_prepare(NULL, 0u, 0u, &stack, error, 1u) == -1);
    assert(error[0] == '\0');
    cleared(&stack);
    error[0] = 'x';
    assert(ios27_guest_stack_prepare(NULL, 0u, 0u, &stack, error, 0u) == -1);
    assert(error[0] == 'x');
    assert(ios27_guest_stack_prepare(NULL, 0u, 0u, &stack, NULL, 0u) == -1);
    cleared(&stack);
    assert(hooks.maps == before_maps);
}

static void payload_limits(size_t page) {
    char longest[IOS27_STARTUP_MAX_STRING];
    const char *values[IOS27_STARTUP_MAX_VECTOR + 1u];
    const char *one[] = {longest, NULL};
    const char *empty[] = {"", NULL};
    ios27_startup_context context = {1, one, NULL, NULL};
    memset(longest, 'X', sizeof(longest));
    longest[sizeof(longest) - 1u] = '\0';
    check_stack(&context, 65536u, page); /* longest allowed individual string */
    for (size_t i = 0u; i < IOS27_STARTUP_MAX_VECTOR; ++i) values[i] = "";
    values[IOS27_STARTUP_MAX_VECTOR] = NULL;
    context = (ios27_startup_context){(int)IOS27_STARTUP_MAX_VECTOR, values, values, values};
    check_stack(&context, 65536u, page); /* all three vectors at their limits */
    for (size_t i = 0u; i < 17u; ++i) values[i] = longest;
    values[17] = NULL;
    context = (ios27_startup_context){16, values, NULL, NULL};
    check_stack(&context, 0u, page); /* exactly 64 KiB including terminators */
    rejected(&context, 65536u, page); /* insufficient 16 KiB headroom */
    context.envp = empty;
    rejected(&context, 0u, page); /* one byte beyond aggregate payload */
    context.envp = NULL;
    context.argc = 17;
    rejected(&context, 0u, page);
    values[4] = NULL;
    context = (ios27_startup_context){8, values + 5u, values, values};
    check_stack(&context, 131072u, page); /* cap also applies across all vectors */
    assert(longest[0] == 'X' && longest[sizeof(longest) - 1u] == '\0');
}

static void allocation_failures(size_t page) {
    ios27_startup_context context = {0, NULL, NULL, NULL};
    ios27_guest_stack stack;
    size_t before_maps = hooks.maps, before_protects = hooks.protects;
    size_t before_unmaps = hooks.unmaps;
    char error[160] = {0};
    hooks.force_page = page;
    hooks.fail_map = 1;
    assert(ios27_guest_stack_prepare(&context, 0u, page, &stack,
                                      error, sizeof(error)) == -1);
    assert(error[0] && !hooks.fail_map && !hooks.live);
    cleared(&stack);
    assert(hooks.maps == before_maps + 1u && hooks.protects == before_protects);
    assert(hooks.unmaps == before_unmaps);
    hooks.fail_protect = 1;
    assert(ios27_guest_stack_prepare(&context, 0u, page, &stack,
                                      error, sizeof(error)) == -1);
    assert(error[0] && !hooks.fail_protect && !hooks.live);
    cleared(&stack);
    assert(hooks.maps == before_maps + 2u && hooks.protects == before_protects + 1u);
    assert(hooks.unmaps == before_unmaps + 1u);
    unmapped(hooks.last_map, hooks.last_size); /* rollback includes alignment slack */
    check_stack(&context, 0u, page); /* the next allocation must still work */
}

static void independent_stacks(size_t page) {
    const char *args[] = {"repeat", "repeat"};
    ios27_startup_context context = {2, args, NULL, NULL};
    ios27_guest_stack first, second;
    uintptr_t first_map, second_map;
    size_t first_size, second_size;
    hooks.force_page = page;
    assert(ios27_guest_stack_prepare(&context, 0u, page, &first, NULL, 0u) == 0);
    first.base[0] = 0x7fu;
    first.argv[0][0] = 'R';
    assert(ios27_guest_stack_prepare(&context, 0u, page, &second, NULL, 0u) == 0);
    assert(hooks.live == 2u && first.reservation != second.reservation);
    assert(second.base[0] == 0u && !strcmp(second.argv[0], "repeat"));
    assert(first.argv[1][0] == 'r' && args[0][0] == 'r');
    first_map = (uintptr_t)first.reservation; first_size = first.size;
    second_map = (uintptr_t)second.reservation; second_size = second.size;
    ios27_guest_stack_release(&first);
    cleared(&first);
    unmapped(first_map, first_size);
    assert(!strcmp(second.argv[0], "repeat") && hooks.live == 1u);
    ios27_guest_stack_release(&second);
    cleared(&second);
    unmapped(second_map, second_size);
    assert(!hooks.live);
}

static void program_vars_checks(size_t page) {
    const char *args[] = {"/guest/bin/fixture", "argument"};
    const char *env[] = {"EXPLICIT=yes", NULL};
    ios27_startup_context context = {2, args, env, NULL};
    ios27_guest_stack stack, before;
    char error[80];
    const uintptr_t header = UINT64_C(0x100000000);
    size_t maps = hooks.maps, protects = hooks.protects;
    hooks.force_page = page;
    assert(ios27_guest_stack_prepare(&context, 0u, page, &stack, error, sizeof(error)) == 0);
    before = stack;
    assert(ios27_guest_stack_program_vars(&stack, 0u, error, sizeof(error)) == -1);
    assert(!memcmp(&before, &stack, sizeof(stack)));
    assert(ios27_guest_stack_program_vars(&stack, header + 1u, error, sizeof(error)) == -1);
    assert(!memcmp(&before, &stack, sizeof(stack)));
    stack.sp = (uintptr_t)stack.base + 16384u;
    ios27_guest_stack tight = stack;
    assert(ios27_guest_stack_program_vars(&stack, header, error, sizeof(error)) == -1);
    assert(!memcmp(&tight, &stack, sizeof(stack)));
    stack = before;
    assert(ios27_guest_stack_program_vars(&stack, header, error, sizeof(error)) == 0);
    assert(!error[0] && stack.program_vars && stack.sp < before.sp && stack.sp % 16u == 0u);
    assert(stack.sp >= (uintptr_t)stack.base + 16384u);
    assert(stack.argv == before.argv && stack.envp == before.envp && stack.apple == before.apple);
    ios27_program_vars *vars = stack.program_vars;
    _Static_assert(offsetof(ios27_program_vars, progname) == 4u * sizeof(void *), "fifth ProgramVars pointer");
    assert((uintptr_t)vars->mh == header);
    assert(*vars->argc == 2 && *vars->argv == stack.argv && *vars->environ == stack.envp);
    assert(!strcmp(*vars->progname, "fixture"));
    assert(*vars->progname == stack.argv[0] + strlen("/guest/bin/"));
    const uintptr_t cells[] = {(uintptr_t)vars, (uintptr_t)vars->argc,
        (uintptr_t)vars->argv, (uintptr_t)vars->environ, (uintptr_t)vars->progname};
    for (size_t i = 0u; i < sizeof(cells) / sizeof(cells[0]); ++i)
        assert(cells[i] >= stack.sp && cells[i] < before.sp);
    /* The cells may be written by libc without changing the host context or
     * the runner's copies of the entry argument registers. */
    *vars->argc = 77;
    *vars->argv = NULL;
    *vars->progname = stack.argv[1];
    assert(context.argc == 2 && stack.argc == 2 && context.argv == args && stack.argv);
    assert(!strcmp(args[0], "/guest/bin/fixture"));
    before = stack;
    assert(ios27_guest_stack_program_vars(&stack, header, error, sizeof(error)) == -1);
    assert(!memcmp(&before, &stack, sizeof(stack)));
    assert(hooks.maps == maps + 1u && hooks.protects == protects + 1u);
    permissions(&stack);
    ios27_guest_stack_release(&stack);
    cleared(&stack);
    assert(ios27_guest_stack_program_vars(&stack, header, error, sizeof(error)) == -1);
    assert(ios27_guest_stack_program_vars(NULL, header, error, sizeof(error)) == -1);
    context = (ios27_startup_context){0, NULL, NULL, NULL};
    assert(!ios27_guest_stack_prepare(&context, 0u, page, &stack, error, sizeof(error)));
    assert(!ios27_guest_stack_program_vars(&stack, header, error, sizeof(error)));
    assert(*stack.program_vars->argc == 0 && **stack.program_vars->progname == '\0');
    assert((uintptr_t)*stack.program_vars->progname >= stack.sp &&
           (uintptr_t)*stack.program_vars->progname < (uintptr_t)stack.argv);
    ios27_guest_stack_release(&stack);
    assert(!hooks.live);
}

int main(void) {
    const size_t pages[] = {4096u, 8192u, 16384u, 32768u, 65536u};
    const uint64_t requests[] = {0u, 1u, 65536u, 65537u, 8u * 1024u * 1024u};
    const char *args[] = {"guest", "duplicate", "duplicate"};
    const char *env[] = {"ONLY=explicit", "", NULL};
    const char *apple[] = {"path=/guest", NULL};
    ios27_startup_context context = {3, args, env, apple};
    ios27_startup_context empty = {0, NULL, NULL, NULL};
    size_t exercised = 0u;
    long measured = sysconf(_SC_PAGESIZE);
    assert(measured >= 4096 && measured <= 65536);
    hooks.host_page = (size_t)measured;
    assert((hooks.host_page & (hooks.host_page - 1u)) == 0u);
    check_stack(&context, 0u, 0u);
    invalid_inputs();
    for (size_t i = 0u; i < sizeof(pages) / sizeof(pages[0]); ++i) {
        size_t page = pages[i];
        if (page < hooks.host_page) continue;
        ++exercised;
        for (size_t j = 0u; j < sizeof(requests) / sizeof(requests[0]); ++j)
            check_stack(&context, requests[j], page);
        check_stack(&empty, 0u, page);
        payload_limits(page);
        allocation_failures(page);
        independent_stacks(page);
        program_vars_checks(page);
    }
    assert(!hooks.live);
    printf("guest stacks: %zu page sizes, host %zu; forced alignment, RW/NX guards, "
           "vectors, ProgramVars, limits, isolation and rollback passed (no guest execution; "
           "simulated page sizes are not hardware validation)\n", exercised, hooks.host_page);
    return 0;
}
