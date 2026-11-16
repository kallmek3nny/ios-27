#ifndef IOS27_GUEST_STACK_INTERNAL_H
#define IOS27_GUEST_STACK_INTERNAL_H

#include "ios27/macho.h"

/* Darwin's five-pointer ProgramVars ABI, used only for initializer x4.
 * Field order: dyld/DyldProcessConfig.h and Libc/sys/crt_externs.c.
 * Storage and the mutable argc/argv/environment/name cells belong to the
 * private guest stack, not to the launcher's globals. */
typedef struct {
    const void *mh;
    int *argc;
    char ***argv;
    char ***environ;
    char **progname;
} ios27_program_vars;

/* Internal Linux/Android stack preparation shared by the native runner and
 * host-only tests. This does not enter guest code or change the host stack.
 * The caller supplies trusted, readable vectors/strings and a fresh or
 * released output (never an output that still owns a reservation).
 *
 * A zero page_size selects the real host page. Nonzero values must be powers
 * of two in [4096, 65536] and whole multiples of the host page. Coarser pages
 * test mapping arithmetic only, not actual 16 KiB hardware compatibility.
 *
 * The usable stack is RW, never executable, with a selected-page-sized
 * inaccessible guard on each side. Any alignment slack stays inaccessible.
 * size covers the entire reservation, including guards and slack; base and
 * capacity describe only the usable stack. sp is 16-byte aligned and leaves
 * at least 16 KiB below the copied, NULL-terminated startup vectors.
 *
 * prepare returns 0 on success. On failure it returns -1, clears a non-NULL
 * output and releases any mapping it made. Diagnostics, when requested, are
 * NUL-terminated. release accepts NULL or a cleared output and clears it.
 */
typedef struct {
    uint8_t *reservation;
    size_t size;
    uint8_t *base;
    size_t capacity;
    size_t page_size;
    uintptr_t sp;
    char **argv;
    char **envp;
    char **apple;
    int argc;
    ios27_program_vars *program_vars;
} ios27_guest_stack;

int ios27_guest_stack_prepare(const ios27_startup_context *context,
                               uint64_t requested, size_t page_size,
                               ios27_guest_stack *stack,
                               char *error, size_t error_size);
void ios27_guest_stack_release(ios27_guest_stack *stack);

/* Attach ProgramVars once, before any guest execution. header must be the
 * already validated/mapped main executable header. On failure the existing
 * stack is untouched; on success SP moves down, retaining 16 KiB headroom.
 * Ordinary main entry and the separate service-table x4 ABI do not call this. */
int ios27_guest_stack_program_vars(ios27_guest_stack *stack, uintptr_t header,
                                   char *error, size_t error_size);

#endif
