#include "ios27/phase24e_application.h"
#include "ios27/dyld_paths.h"
#include "ios27/exports.h"
#include "ios27/images.h"
#include "ios27/macho.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
#include <sys/mman.h>
#include <unistd.h>
#endif

#define FNV_OFFSET UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)
#define MAX_STRING 1024u
#define MAX_PAYLOAD (16u * 1024u)
#define GUEST_STACK_BYTES (1024u * 1024u)

typedef struct {
    ios27_macho_info info;
    ios27_dyld_image_metadata dyld;
    const char *dependencies[IOS27_24E_MAX_IMAGES];
} checked_image;

static uint32_t rd32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
           ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
}
static uint64_t rd64(const uint8_t *b) {
    return (uint64_t)rd32(b) | ((uint64_t)rd32(b + 4u) << 32u);
}
static uint64_t hash_bytes(uint64_t h, const void *p, size_t len) {
    const unsigned char *bytes = p;
    for (size_t i = 0; i < len; ++i) {
        h ^= bytes[i];
        h *= FNV_PRIME;
    }
    return h;
}
static uint64_t hash_number(uint64_t h, uint64_t n) {
    unsigned char bytes[8];
    for (unsigned i = 0; i < 8u; ++i)
        bytes[i] = (unsigned char)(n >> (i * 8u));
    return hash_bytes(h, bytes, sizeof(bytes));
}
static int valid_string(const char *s, size_t cap) {
    return s != NULL && s[0] != 0 && memchr(s, 0, cap) != NULL;
}
static int failure(ios27_24e_report *r, ios27_24e_status status,
                   const char *diagnostic) {
    r->status = status;
    (void)snprintf(r->diagnostic, sizeof(r->diagnostic), "%s", diagnostic);
    r->replay_hash = hash_number(r->replay_hash, (uint64_t)status);
    r->replay_hash = hash_bytes(r->replay_hash, diagnostic, strlen(diagnostic));
    return -1;
}
static int check_vector(const char *const *items, size_t count,
                        size_t *budget, uint64_t *trace) {
    if (count && !items) return -1;
    for (size_t i = 0; i < count; ++i) {
        const char *end;
        size_t len;
        if (!items[i] || !(end = memchr(items[i], 0, MAX_STRING)))
            return -1;
        len = (size_t)(end - items[i]) + 1u;
        if (len > *budget) return -1;
        *budget -= len;
        *trace = hash_bytes(*trace, items[i], len);
    }
    return items && items[count] ? -1 : 0;
}
static int check_context(const ios27_24e_application *app, uint64_t *trace,
                         size_t *env_count, size_t *apple_count) {
    size_t budget = MAX_PAYLOAD;
    if (app->argc < 0 || app->argc > (int)IOS27_24E_MAX_ARGS ||
        (app->argc && !app->argv)) return -1;
    *env_count = 0u;
    *apple_count = 0u;
    if (app->envp) {
        while (*env_count <= IOS27_24E_MAX_ARGS &&
               app->envp[*env_count]) ++*env_count;
        if (*env_count > IOS27_24E_MAX_ARGS) return -1;
    }
    if (app->apple) {
        while (*apple_count <= IOS27_24E_MAX_ARGS &&
               app->apple[*apple_count]) ++*apple_count;
        if (*apple_count > IOS27_24E_MAX_ARGS) return -1;
    }
    return check_vector(app->argv, (size_t)app->argc, &budget, trace) ||
           check_vector(app->envp, *env_count, &budget, trace) ||
           check_vector(app->apple, *apple_count, &budget, trace) ? -1 : 0;
}

/* Reject any nonempty Mach-O lifecycle section instead of silently dropping
 * its observable side effects. The loader's parser validates command bounds;
 * this independent scan repeats section bounds before trusting the offsets. */
static int lifecycle_section(const uint8_t *data, size_t size) {
    size_t pos = 32u;
    uint32_t count;
    if (size < 32u || rd32(data + 20u) > size - 32u) return -1;
    count = rd32(data + 16u);
    if (count > 4096u) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t cmd, len;
        if (pos > size || size - pos < 8u) return -1;
        cmd = rd32(data + pos);
        len = rd32(data + pos + 4u);
        if (len < 8u || len > size - pos) return -1;
        if (cmd == 0x19u) {
            uint32_t sections;
            if (len < 72u) return -1;
            sections = rd32(data + pos + 64u);
            if (sections > 64u || sections > (len - 72u) / 80u)
                return -1;
            for (uint32_t j = 0; j < sections; ++j) {
                const uint8_t *s = data + pos + 72u + (size_t)j * 80u;
                uint32_t type = rd32(s + 64u) & 255u;
                if (rd64(s + 40u) &&
                    (type == 9u || type == 10u || type == 0x16u ||
                     memcmp(s, "__mod_init_func", 15u) == 0 ||
                     memcmp(s, "__mod_term_func", 15u) == 0 ||
                     memcmp(s, "__init_offsets", 14u) == 0))
                    return 1;
            }
        }
        pos += len;
    }
    return 0;
}

/* An exported entry is NOT automatically executable. Require its image-
 * relative offset to be identical to the LC_MAIN entry in an RX segment. */
static int validate_main_export(const ios27_24e_image *exe,
                                ios27_macho_info *info, char *err, size_t cap) {
    ios27_exports *exports = NULL;
    const ios27_export *symbol;
    uint64_t main_vm, min_vm = UINT64_MAX;
    int result = -1;
    if (!info->has_main || info->filetype != IOS27_MH_EXECUTE ||
        !info->needs_dyld || info->stacksize > 8u * 1024u * 1024u)
        return -1;
    /* The core loader independently rejects all unrecognized required
     * commands. This local view permits only the already inspected RPATH. */
    info->unsupported_required = 0u;
    if (ios27_macho_entry_vm(info, &main_vm)) return -1;
    for (size_t i = 0; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        if (strcmp(s->name, "__PAGEZERO") && s->vmsize &&
            s->vmaddr < min_vm) min_vm = s->vmaddr;
    }
    if (min_vm == UINT64_MAX || main_vm < min_vm) return -1;
    exports = calloc(1u, sizeof(*exports));
    if (!exports) return -1;
    if (ios27_exports_parse(exe->bytes, exe->size, exports, err, cap))
        goto done;
    symbol = ios27_exports_find(exports, "_main");
    if (!symbol || symbol->flags != 0u ||
        symbol->address != main_vm - min_vm) goto done;
    for (size_t i = 0; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        if ((s->initprot & 7u) == 5u && main_vm >= s->vmaddr &&
            main_vm - s->vmaddr < s->vmsize) {
            result = 0;
            break;
        }
    }
done:
    free(exports);
    return result;
}

#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
typedef struct {
    void *reservation;
    size_t reserved_bytes;
    uintptr_t sp;
    char **argv;
    char **envp;
    char **apple;
} guest_stack;

static void release_stack(guest_stack *stack) {
    if (stack->reservation) (void)munmap(stack->reservation,
                                         stack->reserved_bytes);
    memset(stack, 0, sizeof(*stack));
}
static int make_stack(const ios27_24e_application *app,
                      size_t env_count, size_t apple_count,
                      guest_stack *stack) {
    long page = sysconf(_SC_PAGESIZE);
    uintptr_t cursor, bottom;
    size_t capacity, total;
    char *vec[3][IOS27_24E_MAX_ARGS];
    size_t sizes[3] = {(size_t)app->argc, env_count, apple_count};
    const char *const *inputs[3] = {app->argv, app->envp, app->apple};
    memset(stack, 0, sizeof(*stack));
    if (page < 4096 || page > 65536 ||
        ((unsigned long)page & ((unsigned long)page - 1u)) != 0u)
        return -1;
    capacity = GUEST_STACK_BYTES;
    total = capacity + 2u * (size_t)page;
    stack->reservation = mmap(NULL, total, PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack->reservation == MAP_FAILED) {
        stack->reservation = NULL;
        return -1;
    }
    stack->reserved_bytes = total;
    if (mprotect((char *)stack->reservation + page, capacity,
                 PROT_READ | PROT_WRITE)) goto failed;
    bottom = (uintptr_t)stack->reservation + (uintptr_t)page;
    cursor = bottom + capacity;
    for (size_t g = 0; g < 3u; ++g) {
        for (size_t i = sizes[g]; i > 0u; --i) {
            size_t n = strlen(inputs[g][i - 1u]) + 1u;
            cursor -= n;
            memcpy((void *)cursor, inputs[g][i - 1u], n);
            vec[g][i - 1u] = (char *)cursor;
        }
    }
    cursor &= ~(uintptr_t)15u;
    {
        size_t pointers = sizes[0] + sizes[1] + sizes[2] + 3u;
        size_t bytes = pointers * sizeof(char *);
        if (cursor < bottom + bytes + 16384u) goto failed;
        cursor = (cursor - bytes) & ~(uintptr_t)15u;
    }
    stack->sp = cursor;
    stack->argv = (char **)cursor;
    stack->envp = stack->argv + sizes[0] + 1u;
    stack->apple = stack->envp + sizes[1] + 1u;
    {
        char **outputs[3] = {stack->argv, stack->envp, stack->apple};
        for (size_t g = 0; g < 3u; ++g) {
            for (size_t i = 0; i < sizes[g]; ++i)
                outputs[g][i] = vec[g][i];
            outputs[g][sizes[g]] = NULL;
        }
    }
    return 0;
failed:
    release_stack(stack);
    return -1;
}

/* Execute LC_MAIN's C entry signature on a guarded, private guest stack.
 * Calls are only allowed after strict export/LC_MAIN equality validation.
 * x18 is untouched, and the guest must follow AArch64 callee-save rules. */
__asm__(".text\n"
        ".global ios27_24e_enter_main\n"
        ".type ios27_24e_enter_main, %function\n"
        "ios27_24e_enter_main:\n"
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
        ".size ios27_24e_enter_main, .-ios27_24e_enter_main\n");
extern int ios27_24e_enter_main(uintptr_t entry, uintptr_t sp, int argc,
                                char **argv, char **envp, char **apple);
#endif

int ios27_24e_run(const ios27_24e_application *app, ios27_24e_report *report) {
    checked_image *checked = NULL;
    ios27_image_spec specs[IOS27_24E_MAX_IMAGES];
    const char *paths[IOS27_24E_MAX_IMAGES];
    ios27_images *graph = NULL;
    size_t env_count, apple_count, main_index = SIZE_MAX;
    uintptr_t address = 0u, op_address = 0u;
    char err[256] = {0};
    int rc = -1;
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    guest_stack stack = {0};
#endif
    if (!report) return -1;
    memset(report, 0, sizeof(*report));
    report->replay_hash = FNV_OFFSET;
    report->exit_code = -1;
    report->status = IOS27_24E_REJECTED;
    if (!app || !app->images || app->image_count == 0u ||
        app->image_count > IOS27_24E_MAX_IMAGES ||
        !valid_string(app->executable_name, 64u) ||
        !valid_string(app->executable_path, IOS27_DYLD_PATH_MAX) ||
        check_context(app, &report->replay_hash, &env_count, &apple_count))
        return failure(report, IOS27_24E_REJECTED, "invalid bounded application context");
    if (app->needs_objc || app->needs_threads || app->needs_mach ||
        app->needs_syscalls)
        return failure(report, IOS27_24E_UNSUPPORTED_ABI,
                       "application requests an unverified Darwin runtime interface");
    checked = calloc(app->image_count, sizeof(*checked));
    if (!checked)
        return failure(report, IOS27_24E_LOAD_FAILED, "metadata allocation failed");
    for (size_t i = 0u; i < app->image_count; ++i) {
        const ios27_24e_image *image = &app->images[i];
        int lifecycle;
        if (!valid_string(image->name, 64u) ||
            !valid_string(image->guest_path, IOS27_DYLD_PATH_MAX) ||
            image->guest_path[0] != '/' || !image->bytes ||
            image->size < 32u || image->size > IOS27_24E_MAX_IMAGE_BYTES) {
            (void)failure(report, IOS27_24E_REJECTED, "invalid catalog image");
            goto done;
        }
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(image->name, app->images[j].name) ||
                !strcmp(image->guest_path, app->images[j].guest_path)) {
                (void)failure(report, IOS27_24E_REJECTED, "duplicate image identity");
                goto done;
            }
        report->replay_hash = hash_bytes(report->replay_hash,
                                         image->name, strlen(image->name));
        report->replay_hash = hash_bytes(report->replay_hash,
                                         image->bytes, image->size);
        if (ios27_macho_parse(image->bytes, image->size, &checked[i].info,
                              err, sizeof(err)) ||
            ios27_dyld_inspect_image(image->bytes, image->size,
                                     &checked[i].dyld, err, sizeof(err))) {
            (void)failure(report, IOS27_24E_REJECTED,
                          err[0] ? err : "malformed Mach-O");
            goto done;
        }
        lifecycle = lifecycle_section(image->bytes, image->size);
        if (lifecycle) {
            (void)failure(report, lifecycle < 0 ? IOS27_24E_REJECTED :
                          IOS27_24E_UNSUPPORTED_ABI,
                          lifecycle < 0 ? "malformed lifecycle metadata" :
                          "unverified native constructor/destructor execution");
            goto done;
        }
        if (checked[i].dyld.dependency_count > IOS27_24E_MAX_IMAGES) {
            (void)failure(report, IOS27_24E_REJECTED, "too many dependencies");
            goto done;
        }
        for (size_t j = 0; j < checked[i].dyld.dependency_count; ++j)
            checked[i].dependencies[j] =
                checked[i].dyld.dependencies[j].install_name;
        specs[i] = (ios27_image_spec){image->name, image->bytes, image->size,
                 checked[i].dependencies, checked[i].dyld.dependency_count};
        paths[i] = image->guest_path;
        if (checked[i].info.filetype == IOS27_MH_EXECUTE) {
            if (main_index != SIZE_MAX ||
                strcmp(image->name, app->executable_name) ||
                strcmp(image->guest_path, app->executable_path)) {
                (void)failure(report, IOS27_24E_REJECTED, "ambiguous MH_EXECUTE");
                goto done;
            }
            main_index = i;
        } else if (checked[i].info.filetype != 6u) {
            (void)failure(report, IOS27_24E_REJECTED, "unsupported image kind");
            goto done;
        }
    }
    if (main_index == SIZE_MAX ||
        validate_main_export(&app->images[main_index],
                             &checked[main_index].info, err, sizeof(err))) {
        (void)failure(report, IOS27_24E_UNSUPPORTED_ABI,
                      "LC_MAIN is not verified RX exported _main");
        goto done;
    }
    if (ios27_images_load_with_paths(specs, app->image_count, paths,
                                     app->executable_path, &graph,
                                     err, sizeof(err))) {
        (void)failure(report, IOS27_24E_LOAD_FAILED,
                      err[0] ? err : "dependency binding or W^X mapping rejected");
        goto done;
    }
    report->live_graphs = 1u;
    report->images_loaded = app->image_count;
    if (ios27_images_symbol(graph, app->executable_name, "_main",
                            &address) || !address ||
        ios27_images_symbol(graph, app->executable_name,
                            "_phase24e_guest_operations", &op_address) ||
        !op_address) {
        (void)failure(report, IOS27_24E_LOAD_FAILED,
                      "validated entry or guest operation counter not mapped");
        goto done;
    }
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    if (make_stack(app, env_count, apple_count, &stack)) {
        (void)failure(report, IOS27_24E_LOAD_FAILED, "guarded guest stack setup failed");
        goto done;
    }
    report->live_stacks = 1u;
    report->actual_guest_entry = 1u;
    /* The entry and all imports were mapped/relocated by the merged loader.
     * No simulation, metadata-only call, or direct host ABI shim is counted. */
    report->exit_code = ios27_24e_enter_main(address, stack.sp, app->argc,
                                              stack.argv, stack.envp, stack.apple);
    {
        volatile uint64_t *counter = (volatile uint64_t *)op_address;
        uint64_t operations = *counter;
        if (report->exit_code != app->expected_exit_code ||
            operations != app->expected_guest_operations) {
            (void)failure(report, IOS27_24E_GUEST_FAILED,
                          "real guest return/operation counter mismatch");
            goto done;
        }
        report->verified_guest_operations = operations;
        report->replay_hash = hash_number(report->replay_hash,
                                           (uint64_t)(unsigned)report->exit_code);
        report->replay_hash = hash_number(report->replay_hash, operations);
        report->status = IOS27_24E_NATIVE_EXECUTED;
        (void)snprintf(report->diagnostic, sizeof(report->diagnostic),
                       "native LC_MAIN _main returned; cross-image results verified");
        rc = 0;
    }
#else
    (void)address;
    (void)op_address;
    (void)env_count;
    (void)apple_count;
    (void)failure(report, IOS27_24E_UNSUPPORTED_HOST,
                  "metadata and mapped imports verified; ARM64 guest entry unavailable");
#endif
done:
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    release_stack(&stack);
#endif
    report->live_stacks = 0u;
    ios27_images_unload(graph);
    report->live_graphs = 0u;
    free(checked);
    return rc;
}
