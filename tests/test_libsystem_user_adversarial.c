/* X t-2cfb43: host-side adversarial tests for the bounded libSystem BSD trap
 * dispatch in src/libsystem_user.c (phase 19e, a4441f2).
 *
 * No native exec and no guest code. Every case calls ios27_libsystem_syscall
 * and the accessors directly. include/ios27/libsystem_user.h lists the Darwin
 * errno values but not when each one applies, so the expected error for each
 * case and the check order (EBADF, then EINVAL, then EFAULT, then ENOMEM)
 * follow src/libsystem_user.c:64-116. Every reject must return value -1 with
 * the expected errno and leave the arena, both captured outputs and the heap
 * unchanged. Later reads and writes prove that no reject moved the stdin
 * cursor or closed a descriptor.
 *
 * Skipped because services() in tests/phase19e-standalone.c covers it: the
 * fd1 write at GUEST_BYTES-4 (count 4 ok, count 5 EFAULT), a short stdin
 * read, getpid 123, number 999 and class 0x01000000 ENOSYS, the fd1 double
 * close, malloc(SIZE_MAX), the 128-allocation cap, free and double free.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "ios27/libsystem_user.h"

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GUEST IOS27_LIBSYSTEM_GUEST_BYTES
#define MAX_IO IOS27_LIBSYSTEM_MAX_IO
#define TRAP(n) (IOS27_DARWIN_CLASS_UNIX | (uint32_t)(n))
#define SYS(name) TRAP(IOS27_DARWIN_SYS_##name)
#define D_EBADF IOS27_DARWIN_EBADF
#define D_ENOMEM IOS27_DARWIN_ENOMEM
#define D_EFAULT IOS27_DARWIN_EFAULT
#define D_EINVAL IOS27_DARWIN_EINVAL
#define D_ENOSYS IOS27_DARWIN_ENOSYS

static int failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond); \
            ++failures; \
        } \
    } while (0)

/* Everything a reject could change that a caller can observe through the
 * API. The descriptor table and the stdin cursor are not readable, so later
 * reads and writes check those. */
typedef struct {
    uint8_t arena[IOS27_LIBSYSTEM_GUEST_BYTES];
    uint8_t out[2][IOS27_LIBSYSTEM_MAX_IO];
    size_t out_count[2];
    size_t heap_used;
    size_t heap_allocations;
} snapshot;

static snapshot snap_before, snap_after;

static void take(ios27_libsystem *ctx, snapshot *s) {
    uint8_t *arena = ios27_libsystem_guest(ctx, 0, GUEST);
    memset(s, 0, sizeof(*s));
    if (arena) memcpy(s->arena, arena, sizeof(s->arena));
    s->out_count[0] =
        ios27_libsystem_output(ctx, 1, s->out[0], sizeof(s->out[0]));
    s->out_count[1] =
        ios27_libsystem_output(ctx, 2, s->out[1], sizeof(s->out[1]));
    ios27_libsystem_heap_state(ctx, &s->heap_used, &s->heap_allocations);
}

static int same(const snapshot *a, const snapshot *b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

static int all_bytes(const uint8_t *p, size_t n, uint8_t value) {
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != value) return 0;
    }
    return 1;
}

/* A context with a patterned arena and two live heap blocks, so a reject
 * that touched the arena or the heap shows up in a snapshot. */
static ios27_libsystem *fresh(uint32_t pid) {
    ios27_libsystem *ctx = NULL;
    uint8_t *arena;
    if (ios27_libsystem_create(&ctx, pid) != 0 || !ctx) {
        fprintf(stderr, "ios27_libsystem_create(pid %u) failed\n",
                (unsigned)pid);
        exit(1);
    }
    arena = ios27_libsystem_guest(ctx, 0, GUEST);
    if (!arena || !ios27_libsystem_malloc(ctx, 24) ||
        !ios27_libsystem_malloc(ctx, 40)) {
        fprintf(stderr, "context setup failed\n");
        ios27_libsystem_destroy(ctx);
        exit(1);
    }
    for (size_t i = 0; i < GUEST; ++i) arena[i] = (uint8_t)(i * 31u + 7u);
    return ctx;
}

static ios27_libsystem_result call(ios27_libsystem *ctx, uint32_t trap,
                                   uint64_t a0, uint64_t a1, uint64_t a2) {
    uint64_t args[6] = {0, 0, 0, 0, 0, 0};
    args[0] = a0;
    args[1] = a1;
    args[2] = a2;
    return ios27_libsystem_syscall(ctx, trap, args);
}

static void report(int line, const char *what, uint32_t trap, uint64_t a0,
                   uint64_t a1, uint64_t a2, ios27_libsystem_result got,
                   int64_t want_value, int want_errno, int kept) {
    fprintf(stderr,
            "line %d: %s: trap 0x%08x args {0x%llx, 0x%llx, 0x%llx}: got value "
            "%lld errno %d, want value %lld errno %d%s\n",
            line, what, (unsigned)trap, (unsigned long long)a0,
            (unsigned long long)a1, (unsigned long long)a2,
            (long long)got.value, got.darwin_errno, (long long)want_value,
            want_errno, kept ? "" : ", state changed");
    ++failures;
}

/* A reject: value -1, the expected errno and no observable state change. */
static void reject_at(int line, ios27_libsystem *ctx, const char *what,
                      uint32_t trap, uint64_t a0, uint64_t a1, uint64_t a2,
                      int err) {
    ios27_libsystem_result r;
    int kept;
    take(ctx, &snap_before);
    r = call(ctx, trap, a0, a1, a2);
    take(ctx, &snap_after);
    kept = same(&snap_before, &snap_after);
    if (r.value != -1 || r.darwin_errno != err || !kept)
        report(line, what, trap, a0, a1, a2, r, -1, err, kept);
}

/* A success that must not change state: zero-length I/O, reads at end of
 * input, getpid and the identity traps. */
static void pure_at(int line, ios27_libsystem *ctx, const char *what,
                    uint32_t trap, uint64_t a0, uint64_t a1, uint64_t a2,
                    int64_t value) {
    ios27_libsystem_result r;
    int kept;
    take(ctx, &snap_before);
    r = call(ctx, trap, a0, a1, a2);
    take(ctx, &snap_after);
    kept = same(&snap_before, &snap_after);
    if (r.value != value || r.darwin_errno != 0 || !kept)
        report(line, what, trap, a0, a1, a2, r, value, 0, kept);
}

/* A success that may change state. */
static void ok_at(int line, ios27_libsystem *ctx, const char *what,
                  uint32_t trap, uint64_t a0, uint64_t a1, uint64_t a2,
                  int64_t value) {
    ios27_libsystem_result r = call(ctx, trap, a0, a1, a2);
    if (r.value != value || r.darwin_errno != 0)
        report(line, what, trap, a0, a1, a2, r, value, 0, 1);
}

#define EXPECT_REJECT(ctx, what, trap, a0, a1, a2, err) \
    reject_at(__LINE__, (ctx), (what), (trap), (a0), (a1), (a2), (err))
#define EXPECT_PURE(ctx, what, trap, a0, a1, a2, value) \
    pure_at(__LINE__, (ctx), (what), (trap), (a0), (a1), (a2), (value))
#define EXPECT_OK(ctx, what, trap, a0, a1, a2, value) \
    ok_at(__LINE__, (ctx), (what), (trap), (a0), (a1), (a2), (value))

static void test_create(void) {
    ios27_libsystem *ctx = NULL;
    uint64_t args[6] = {0, 0, 0, 0, 0, 0};
    ios27_libsystem_result r;

    CHECK(ios27_libsystem_create(NULL, 1u) == -1);
    CHECK(ios27_libsystem_create(&ctx, 0u) == -1);
    CHECK(ctx == NULL);
    /* The pid is a uint32_t. The widest one must come back positive, not
     * sign-extended to -1, which would look like an error. */
    CHECK(ios27_libsystem_create(&ctx, UINT32_MAX) == 0);
    CHECK(ctx != NULL);
    if (!ctx) return;
    r = ios27_libsystem_syscall(ctx, SYS(GETPID), args);
    CHECK(r.darwin_errno == 0 && r.value == (int64_t)UINT32_MAX);
    ios27_libsystem_destroy(ctx);
    ios27_libsystem_destroy(NULL);
}

static void test_null_context(void) {
    uint64_t args[6] = {1u, 0u, 4u, 0u, 0u, 0u};
    uint8_t buf[4] = {0xa5, 0xa5, 0xa5, 0xa5};
    size_t used = 1, allocations = 1;
    ios27_libsystem_result r;

    /* EINVAL for a NULL context comes before the class check. */
    r = ios27_libsystem_syscall(NULL, SYS(WRITE), args);
    CHECK(r.value == -1 && r.darwin_errno == D_EINVAL);
    r = ios27_libsystem_syscall(NULL, 0x01000000u, args);
    CHECK(r.value == -1 && r.darwin_errno == D_EINVAL);
    r = ios27_libsystem_syscall(NULL, SYS(GETPID), NULL);
    CHECK(r.value == -1 && r.darwin_errno == D_EINVAL);
    CHECK(ios27_libsystem_guest(NULL, 0, 0) == NULL);
    CHECK(ios27_libsystem_stdin(NULL, "a", 1) == -1);
    CHECK(ios27_libsystem_stdin(NULL, NULL, 0) == -1);
    CHECK(ios27_libsystem_output(NULL, 1, buf, sizeof buf) == 0);
    CHECK(all_bytes(buf, sizeof buf, 0xa5));
    ios27_libsystem_heap_state(NULL, &used, &allocations);
    CHECK(used == 0 && allocations == 0);
    CHECK(ios27_libsystem_malloc(NULL, 1) == NULL);
    CHECK(ios27_libsystem_free(NULL, buf) == -1);
}

static void test_guest_accessor(void) {
    ios27_libsystem *ctx = fresh(77u);
    uint8_t *base = ios27_libsystem_guest(ctx, 0, 0);

    CHECK(base != NULL);
    if (!base) {
        ios27_libsystem_destroy(ctx);
        return;
    }
    CHECK(ios27_libsystem_guest(ctx, 0, GUEST) == base);
    CHECK(ios27_libsystem_guest(ctx, GUEST - 1u, 1) == base + GUEST - 1u);
    CHECK(ios27_libsystem_guest(ctx, GUEST, 0) == base + GUEST);
    CHECK(ios27_libsystem_guest(ctx, GUEST, 1) == NULL);
    CHECK(ios27_libsystem_guest(ctx, GUEST + 1u, 0) == NULL);
    CHECK(ios27_libsystem_guest(ctx, 0, GUEST + 1u) == NULL);
    CHECK(ios27_libsystem_guest(ctx, 1, GUEST) == NULL);
    CHECK(ios27_libsystem_guest(ctx, GUEST, SIZE_MAX) == NULL);
    CHECK(ios27_libsystem_guest(ctx, 1, SIZE_MAX) == NULL);
    CHECK(ios27_libsystem_guest(ctx, SIZE_MAX, 0) == NULL);
    CHECK(ios27_libsystem_guest(ctx, SIZE_MAX, SIZE_MAX) == NULL);
    ios27_libsystem_destroy(ctx);
}

static void test_stdin(void) {
    static uint8_t big[MAX_IO + 1u];
    ios27_libsystem *ctx = fresh(77u);
    uint8_t *arena = ios27_libsystem_guest(ctx, 0, GUEST);

    memset(big, 'z', sizeof big);
    CHECK(ios27_libsystem_stdin(ctx, "abcdef", 6) == 0);
    EXPECT_OK(ctx, "read 2", SYS(READ), 0u, 0u, 2u, 2);
    CHECK(memcmp(arena, "ab", 2) == 0);
    /* Rejected stdin calls keep the old bytes and the cursor. */
    CHECK(ios27_libsystem_stdin(ctx, NULL, 3) == -1);
    CHECK(ios27_libsystem_stdin(ctx, big, MAX_IO + 1u) == -1);
    CHECK(ios27_libsystem_stdin(ctx, big, SIZE_MAX) == -1);
    EXPECT_PURE(ctx, "zero-length read", SYS(READ), 0u, 8u, 0u, 0);
    EXPECT_OK(ctx, "read the rest", SYS(READ), 0u, 16u, 8u, 4);
    CHECK(memcmp(arena + 16, "cdef", 4) == 0);
    /* At end of input a read returns 0 and copies nothing. */
    EXPECT_PURE(ctx, "read at EOF", SYS(READ), 0u, 32u, 4u, 0);
    EXPECT_PURE(ctx, "read MAX_IO at EOF", SYS(READ), 0u, 0u, MAX_IO, 0);
    /* The largest stdin is accepted and read in full. */
    CHECK(ios27_libsystem_stdin(ctx, big, MAX_IO) == 0);
    EXPECT_OK(ctx, "read MAX_IO", SYS(READ), 0u, 0u, MAX_IO, MAX_IO);
    CHECK(all_bytes(arena, MAX_IO, 'z'));
    /* An empty stdin replaces the old input. */
    CHECK(ios27_libsystem_stdin(ctx, "q", 1) == 0);
    CHECK(ios27_libsystem_stdin(ctx, NULL, 0) == 0);
    EXPECT_PURE(ctx, "read after empty stdin", SYS(READ), 0u, 0u, 4u, 0);
    ios27_libsystem_destroy(ctx);
}

static void test_trap_numbers(void) {
    /* 0 and 0xffff, the unimplemented neighbours of each implemented
     * number, and implemented numbers with bits 8-15 set. */
    static const uint32_t unimplemented[] = {
        0u, 1u, 2u, 5u, 7u, 19u, 21u, 23u, 26u, 42u, 44u, 46u, 48u,
        0x0103u, 0x0104u, 0x0106u, 0x8004u, 0xfffeu, 0xffffu,
    };
    /* Darwin number 0 is the indirect syscall. It must not shift the
     * arguments and run args[0] as a 4-byte write to fd1. */
    static const uint64_t indirect[6] = {
        IOS27_DARWIN_SYS_WRITE, 1u, 0u, 4u, 0u, 0u,
    };
    static uint8_t got[16];
    ios27_libsystem *ctx = fresh(77u);
    uint8_t *arena = ios27_libsystem_guest(ctx, 0, GUEST);
    ios27_libsystem_result r;
    uint32_t c;
    unsigned bit;
    size_t i;

    CHECK(ios27_libsystem_stdin(ctx, "abcdef", 6) == 0);

    /* NULL args is EINVAL and changes nothing. */
    take(ctx, &snap_before);
    r = ios27_libsystem_syscall(ctx, SYS(GETPID), NULL);
    take(ctx, &snap_after);
    CHECK(r.value == -1 && r.darwin_errno == D_EINVAL);
    CHECK(same(&snap_before, &snap_after));
    r = ios27_libsystem_syscall(ctx, SYS(WRITE), NULL);
    take(ctx, &snap_after);
    CHECK(r.value == -1 && r.darwin_errno == D_EINVAL);
    CHECK(same(&snap_before, &snap_after));

    /* Every class byte but 0x02 is ENOSYS, even with valid read or write
     * arguments, so a misdispatch would show up in the snapshot. */
    for (c = 0; c < 256u; ++c) {
        if (c == (IOS27_DARWIN_CLASS_UNIX >> 24)) continue;
        EXPECT_REJECT(ctx, "foreign class write",
                      (c << 24) | IOS27_DARWIN_SYS_WRITE, 1u, 0u, 4u,
                      D_ENOSYS);
        EXPECT_REJECT(ctx, "foreign class read",
                      (c << 24) | IOS27_DARWIN_SYS_READ, 0u, 0u, 4u,
                      D_ENOSYS);
    }
    /* Any bit in 0x00ff0000 is ENOSYS, even with an implemented number. */
    for (bit = 16; bit < 24; ++bit) {
        EXPECT_REJECT(ctx, "reserved bit write", SYS(WRITE) | (1u << bit),
                      1u, 0u, 4u, D_ENOSYS);
        EXPECT_REJECT(ctx, "reserved bit getpid", SYS(GETPID) | (1u << bit),
                      0u, 0u, 0u, D_ENOSYS);
    }
    EXPECT_REJECT(ctx, "all reserved bits", SYS(WRITE) | 0x00ff0000u, 1u, 0u,
                  4u, D_ENOSYS);
    for (i = 0; i < sizeof unimplemented / sizeof unimplemented[0]; ++i) {
        EXPECT_REJECT(ctx, "unimplemented number, write args",
                      TRAP(unimplemented[i]), 1u, 0u, 4u, D_ENOSYS);
        EXPECT_REJECT(ctx, "unimplemented number, read args",
                      TRAP(unimplemented[i]), 0u, 0u, 4u, D_ENOSYS);
    }
    take(ctx, &snap_before);
    r = ios27_libsystem_syscall(ctx, TRAP(0u), indirect);
    take(ctx, &snap_after);
    CHECK(r.value == -1 && r.darwin_errno == D_ENOSYS);
    CHECK(same(&snap_before, &snap_after));

    /* No reject consumed stdin, captured output, or closed fd0 or fd1
     * (0x0106 would close them if bits 8-15 were dropped). */
    CHECK(ios27_libsystem_output(ctx, 1, got, sizeof got) == 0);
    CHECK(ios27_libsystem_output(ctx, 2, got, sizeof got) == 0);
    EXPECT_OK(ctx, "read stdin after rejects", SYS(READ), 0u, 200u, 6u, 6);
    CHECK(memcmp(arena + 200, "abcdef", 6) == 0);
    EXPECT_OK(ctx, "write fd1 after rejects", SYS(WRITE), 1u, 0u, 4u, 4);
    ios27_libsystem_destroy(ctx);
}

static void test_descriptors(void) {
    /* The 2^32 values become 0, 1 and 2 if the fd is cut to 32 bits. */
    static const uint64_t bad_fds[] = {
        3u, 4u, 255u, 0x7fffffffu, 0x80000000u, 0xffffffffu,
        0x100000000ull, 0x100000001ull, 0x100000002ull,
        UINT64_MAX - 1u, UINT64_MAX,
    };
    static uint8_t got[16];
    ios27_libsystem *ctx = fresh(77u);
    uint8_t *arena = ios27_libsystem_guest(ctx, 0, GUEST);
    size_t i;

    CHECK(ios27_libsystem_stdin(ctx, "abcdef", 6) == 0);
    for (i = 0; i < sizeof bad_fds / sizeof bad_fds[0]; ++i) {
        uint64_t fd = bad_fds[i];
        EXPECT_REJECT(ctx, "read bad fd", SYS(READ), fd, 0u, 4u, D_EBADF);
        EXPECT_REJECT(ctx, "zero-length read bad fd", SYS(READ), fd, 0u, 0u,
                      D_EBADF);
        EXPECT_REJECT(ctx, "write bad fd", SYS(WRITE), fd, 0u, 4u, D_EBADF);
        EXPECT_REJECT(ctx, "zero-length write bad fd", SYS(WRITE), fd, 0u, 0u,
                      D_EBADF);
        EXPECT_REJECT(ctx, "close bad fd", SYS(CLOSE), fd, 0u, 0u, D_EBADF);
    }
    /* stdin is read-only; stdout and stderr are write-only. */
    EXPECT_REJECT(ctx, "read fd1", SYS(READ), 1u, 0u, 4u, D_EBADF);
    EXPECT_REJECT(ctx, "read fd2", SYS(READ), 2u, 0u, 4u, D_EBADF);
    EXPECT_REJECT(ctx, "zero-length read fd1", SYS(READ), 1u, 0u, 0u, D_EBADF);
    EXPECT_REJECT(ctx, "write fd0", SYS(WRITE), 0u, 0u, 4u, D_EBADF);
    EXPECT_REJECT(ctx, "zero-length write fd0", SYS(WRITE), 0u, 0u, 0u,
                  D_EBADF);

    /* No reject moved the stdin cursor or closed a descriptor. */
    EXPECT_OK(ctx, "read stdin", SYS(READ), 0u, 100u, 6u, 6);
    CHECK(memcmp(arena + 100, "abcdef", 6) == 0);
    EXPECT_OK(ctx, "write fd1", SYS(WRITE), 1u, 0u, 3u, 3);
    EXPECT_OK(ctx, "write fd2", SYS(WRITE), 2u, 3u, 3u, 3);

    /* Close each descriptor once; every later use is EBADF. */
    EXPECT_OK(ctx, "close fd0", SYS(CLOSE), 0u, 0u, 0u, 0);
    EXPECT_REJECT(ctx, "read closed fd0", SYS(READ), 0u, 0u, 4u, D_EBADF);
    EXPECT_REJECT(ctx, "zero-length read closed fd0", SYS(READ), 0u, 0u, 0u,
                  D_EBADF);
    EXPECT_REJECT(ctx, "close closed fd0", SYS(CLOSE), 0u, 0u, 0u, D_EBADF);
    EXPECT_OK(ctx, "close fd2", SYS(CLOSE), 2u, 0u, 0u, 0);
    EXPECT_REJECT(ctx, "write closed fd2", SYS(WRITE), 2u, 0u, 4u, D_EBADF);
    EXPECT_REJECT(ctx, "close closed fd2", SYS(CLOSE), 2u, 0u, 0u, D_EBADF);
    /* Closing fd0 and fd2 left fd1 open. */
    EXPECT_OK(ctx, "write fd1 after closing 0 and 2", SYS(WRITE), 1u, 6u, 3u,
              3);
    EXPECT_OK(ctx, "close fd1", SYS(CLOSE), 1u, 0u, 0u, 0);
    EXPECT_REJECT(ctx, "write closed fd1", SYS(WRITE), 1u, 0u, 4u, D_EBADF);
    EXPECT_REJECT(ctx, "zero-length write closed fd1", SYS(WRITE), 1u, 0u, 0u,
                  D_EBADF);
    /* The captured output holds only the accepted writes. */
    CHECK(ios27_libsystem_output(ctx, 1, got, sizeof got) == 6);
    CHECK(memcmp(got, arena, 3) == 0 && memcmp(got + 3, arena + 6, 3) == 0);
    CHECK(ios27_libsystem_output(ctx, 2, got, sizeof got) == 3);
    CHECK(memcmp(got, arena + 3, 3) == 0);
    ios27_libsystem_destroy(ctx);
}

static void test_buffers(void) {
    static const struct {
        uint64_t off;
        uint64_t count;
        int err;
        const char *why;
    } cases[] = {
        {GUEST + 1u, 0u, D_EFAULT, "count 0 one past the end"},
        {GUEST, 1u, D_EFAULT, "one byte at the end"},
        {GUEST - 1u, 2u, D_EFAULT, "straddles the end"},
        {GUEST - MAX_IO + 1u, MAX_IO, D_EFAULT, "MAX_IO straddles the end"},
        {0x100000000ull, 4u, D_EFAULT, "offset 2^32, 0 if cut to 32 bits"},
        {0x100000000ull + 16u, 4u, D_EFAULT, "offset 2^32 + 16"},
        {UINT64_MAX, 0u, D_EFAULT, "offset UINT64_MAX, count 0"},
        {UINT64_MAX, 1u, D_EFAULT, "offset UINT64_MAX"},
        {UINT64_MAX - 3u, 8u, D_EFAULT, "offset plus count wraps to 4"},
        {UINT64_MAX - MAX_IO + 2u, MAX_IO, D_EFAULT,
         "offset plus MAX_IO wraps to 1"},
        {0u, MAX_IO + 1u, D_EINVAL, "count MAX_IO + 1"},
        {0u, 0x100000000ull, D_EINVAL, "count 2^32, 0 if cut to 32 bits"},
        {0u, 0x100000004ull, D_EINVAL, "count 2^32 + 4"},
        {1u, UINT64_MAX, D_EINVAL, "offset plus count wraps to 0"},
        {GUEST + 1u, MAX_IO + 1u, D_EINVAL, "EINVAL before EFAULT"},
    };
    static const uint64_t zero_offsets[] = {0u, 1u, GUEST - 1u, GUEST};
    static uint8_t got[MAX_IO];
    ios27_libsystem *ctx = fresh(77u);
    uint8_t *arena = ios27_libsystem_guest(ctx, 0, GUEST);
    size_t i;

    CHECK(ios27_libsystem_stdin(ctx, "abcdef", 6) == 0);
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        EXPECT_REJECT(ctx, cases[i].why, SYS(WRITE), 1u, cases[i].off,
                      cases[i].count, cases[i].err);
        EXPECT_REJECT(ctx, cases[i].why, SYS(WRITE), 2u, cases[i].off,
                      cases[i].count, cases[i].err);
        EXPECT_REJECT(ctx, cases[i].why, SYS(READ), 0u, cases[i].off,
                      cases[i].count, cases[i].err);
    }
    /* EBADF comes before EINVAL and EFAULT. */
    EXPECT_REJECT(ctx, "write fd0, bad count and offset", SYS(WRITE), 0u,
                  GUEST + 1u, MAX_IO + 1u, D_EBADF);
    EXPECT_REJECT(ctx, "read fd3, bad count and offset", SYS(READ), 3u,
                  UINT64_MAX, UINT64_MAX, D_EBADF);
    /* Zero-length calls succeed at every offset up to one past the end. */
    for (i = 0; i < sizeof zero_offsets / sizeof zero_offsets[0]; ++i) {
        EXPECT_PURE(ctx, "zero-length write fd1", SYS(WRITE), 1u,
                    zero_offsets[i], 0u, 0);
        EXPECT_PURE(ctx, "zero-length write fd2", SYS(WRITE), 2u,
                    zero_offsets[i], 0u, 0);
        EXPECT_PURE(ctx, "zero-length read", SYS(READ), 0u, zero_offsets[i],
                    0u, 0);
    }
    /* The largest copy, ending exactly at the end of the arena. */
    EXPECT_OK(ctx, "MAX_IO ending at the end", SYS(WRITE), 2u, GUEST - MAX_IO,
              MAX_IO, MAX_IO);
    CHECK(ios27_libsystem_output(ctx, 2, got, sizeof got) == MAX_IO);
    CHECK(memcmp(got, arena + GUEST - MAX_IO, MAX_IO) == 0);
    /* No reject or zero-length read moved the stdin cursor. */
    EXPECT_OK(ctx, "read 6 ending at the end", SYS(READ), 0u, GUEST - 6u, 6u,
              6);
    CHECK(memcmp(arena + GUEST - 6u, "abcdef", 6) == 0);
    CHECK(ios27_libsystem_output(ctx, 1, got, sizeof got) == 0);
    ios27_libsystem_destroy(ctx);
}

static void test_capture_limits(void) {
    static uint8_t got[MAX_IO + 16u];
    ios27_libsystem *ctx = fresh(77u);
    uint8_t *arena = ios27_libsystem_guest(ctx, 0, GUEST);
    size_t n;

    EXPECT_OK(ctx, "fill fd1 to MAX_IO - 2", SYS(WRITE), 1u, 0u, MAX_IO - 2u,
              MAX_IO - 2u);
    /* A write that does not fit is refused whole: no partial copy. */
    EXPECT_REJECT(ctx, "3 bytes into 2 free", SYS(WRITE), 1u, 0u, 3u,
                  D_ENOMEM);
    EXPECT_REJECT(ctx, "MAX_IO into 2 free", SYS(WRITE), 1u, 0u, MAX_IO,
                  D_ENOMEM);
    EXPECT_OK(ctx, "exact fit", SYS(WRITE), 1u, 100u, 2u, 2);
    EXPECT_REJECT(ctx, "1 byte into a full buffer", SYS(WRITE), 1u, 0u, 1u,
                  D_ENOMEM);
    EXPECT_PURE(ctx, "0 bytes into a full buffer", SYS(WRITE), 1u, 0u, 0u, 0);
    EXPECT_PURE(ctx, "0 bytes at the end into a full buffer", SYS(WRITE), 1u,
                GUEST, 0u, 0);
    /* EINVAL and EFAULT are checked before ENOMEM. */
    EXPECT_REJECT(ctx, "full buffer, count MAX_IO + 1", SYS(WRITE), 1u, 0u,
                  MAX_IO + 1u, D_EINVAL);
    EXPECT_REJECT(ctx, "full buffer, bad offset", SYS(WRITE), 1u, GUEST, 1u,
                  D_EFAULT);
    /* stderr has its own buffer. */
    EXPECT_OK(ctx, "fd2 still has room", SYS(WRITE), 2u, 0u, 1u, 1);

    n = ios27_libsystem_output(ctx, 1, got, sizeof got);
    CHECK(n == MAX_IO);
    CHECK(memcmp(got, arena, MAX_IO - 2u) == 0);
    CHECK(memcmp(got + MAX_IO - 2u, arena + 100, 2) == 0);

    /* The output accessor copies at most cap bytes and rejects bad fds. */
    memset(got, 0xa5, sizeof got);
    CHECK(ios27_libsystem_output(ctx, 1, got, 2) == 2);
    CHECK(got[0] == arena[0] && got[1] == arena[1] && got[2] == 0xa5);
    memset(got, 0xa5, sizeof got);
    CHECK(ios27_libsystem_output(ctx, 1, NULL, 4) == 0);
    CHECK(ios27_libsystem_output(ctx, 1, NULL, 0) == 0);
    CHECK(ios27_libsystem_output(ctx, 1, got, 0) == 0);
    CHECK(ios27_libsystem_output(ctx, 0, got, sizeof got) == 0);
    CHECK(ios27_libsystem_output(ctx, 3, got, sizeof got) == 0);
    CHECK(ios27_libsystem_output(ctx, -1, got, sizeof got) == 0);
    CHECK(ios27_libsystem_output(ctx, INT_MIN, got, sizeof got) == 0);
    CHECK(ios27_libsystem_output(ctx, INT_MAX, got, sizeof got) == 0);
    CHECK(all_bytes(got, sizeof got, 0xa5));
    ios27_libsystem_destroy(ctx);
}

static void test_host_isolation(void) {
    static const uint32_t id_traps[4] = {
        IOS27_DARWIN_SYS_GETUID, IOS27_DARWIN_SYS_GETEUID,
        IOS27_DARWIN_SYS_GETGID, IOS27_DARWIN_SYS_GETEGID,
    };
    static uint8_t got[128];
    uint32_t host_pid = (uint32_t)getpid();
    uint32_t guest_pid = host_pid + 1000u;
    uint64_t host_ids[4];
    int open_before[3], open_during[3] = {0, 0, 0};
    int in_pipe[2] = {-1, -1}, out_pipe[2] = {-1, -1};
    int saved[3] = {-1, -1, -1};
    int redirected = 0, restore_failed = 0;
    char host_bytes[4] = {0, 0, 0, 0};
    char leak[256];
    ssize_t host_read = -1, leaked = -1;
    ios27_libsystem_result rd = {0, 0}, w1 = {0, 0}, w2 = {0, 0};
    ios27_libsystem_result cl[3] = {{0, 0}, {0, 0}, {0, 0}};
    ios27_libsystem *ctx;
    uint8_t *arena;
    int fd;
    size_t i;

    if (guest_pid == 0u) guest_pid = 1u;
    ctx = fresh(guest_pid);
    arena = ios27_libsystem_guest(ctx, 0, GUEST);
    host_ids[0] = (uint64_t)getuid();
    host_ids[1] = (uint64_t)geteuid();
    host_ids[2] = (uint64_t)getgid();
    host_ids[3] = (uint64_t)getegid();

    /* getpid reports the guest pid, which differs from the host pid. */
    CHECK(guest_pid != host_pid);
    EXPECT_PURE(ctx, "getpid", SYS(GETPID), 0u, 0u, 0u, (int64_t)guest_pid);
    EXPECT_PURE(ctx, "getpid, junk args", SYS(GETPID), UINT64_MAX, UINT64_MAX,
                UINT64_MAX, (int64_t)guest_pid);
    /* The identity traps report the fixed guest id 501 whatever the args. */
    for (i = 0; i < 4; ++i) {
        EXPECT_PURE(ctx, "identity", TRAP(id_traps[i]), 0u, 0u, 0u, 501);
        EXPECT_PURE(ctx, "identity, junk args", TRAP(id_traps[i]), UINT64_MAX,
                    UINT64_MAX, UINT64_MAX, 501);
        if (host_ids[i] == 501u)
            printf("note: the host id behind trap %u is 501, so that check "
                   "cannot tell guest from host\n",
                   (unsigned)id_traps[i]);
    }

    /* Guest I/O never reaches a host descriptor. Host fd 0 becomes a pipe
     * that holds "host", host fds 1 and 2 become a nonblocking pipe that
     * must stay empty, and the guest closes all three guest descriptors. */
    for (fd = 0; fd < 3; ++fd) open_before[fd] = fcntl(fd, F_GETFD) != -1;
    if (!open_before[0] || !open_before[1] || !open_before[2]) {
        printf("note: a host standard descriptor is closed, so the host "
               "descriptor checks are skipped\n");
        ios27_libsystem_destroy(ctx);
        return;
    }
    fflush(stdout);
    fflush(stderr);
    if (pipe(in_pipe) == 0 && pipe(out_pipe) == 0 &&
        write(in_pipe[1], "host", 4) == 4 &&
        fcntl(out_pipe[0], F_SETFL, O_NONBLOCK) == 0) {
        for (fd = 0; fd < 3; ++fd) saved[fd] = dup(fd);
        if (saved[0] >= 0 && saved[1] >= 0 && saved[2] >= 0 &&
            dup2(in_pipe[0], 0) == 0 && dup2(out_pipe[1], 1) == 1 &&
            dup2(out_pipe[1], 2) == 2) {
            redirected = 1;
            /* Guest stdin is empty, so this read must return 0 and must not
             * fall through to host fd 0. */
            take(ctx, &snap_before);
            rd = call(ctx, SYS(READ), 0u, 0u, 4u);
            take(ctx, &snap_after);
            w1 = call(ctx, SYS(WRITE), 1u, 0u, 64u);
            w2 = call(ctx, SYS(WRITE), 2u, 64u, 64u);
            for (fd = 0; fd < 3; ++fd)
                cl[fd] = call(ctx, SYS(CLOSE), (uint64_t)fd, 0u, 0u);
            for (fd = 0; fd < 3; ++fd)
                open_during[fd] = fcntl(fd, F_GETFD) != -1;
            host_read = read(0, host_bytes, sizeof host_bytes);
        }
        for (fd = 0; fd < 3; ++fd) {
            if (saved[fd] >= 0) {
                if (dup2(saved[fd], fd) != fd) restore_failed = 1;
                close(saved[fd]);
            }
        }
    }
    CHECK(!restore_failed);
    CHECK(redirected);
    if (redirected) {
        leaked = read(out_pipe[0], leak, sizeof leak);
        CHECK(rd.value == 0 && rd.darwin_errno == 0);
        CHECK(same(&snap_before, &snap_after));
        CHECK(w1.value == 64 && w1.darwin_errno == 0);
        CHECK(w2.value == 64 && w2.darwin_errno == 0);
        for (fd = 0; fd < 3; ++fd) {
            CHECK(cl[fd].value == 0 && cl[fd].darwin_errno == 0);
            CHECK(open_during[fd]);
        }
        /* Host fd 0 still held its bytes after the guest read and close. */
        CHECK(host_read == 4 && memcmp(host_bytes, "host", 4) == 0);
        /* Nothing reached host fd 1 or 2: the read gives -1 (EAGAIN) or 0. */
        CHECK(leaked <= 0);
        CHECK(ios27_libsystem_output(ctx, 1, got, sizeof got) == 64);
        CHECK(memcmp(got, arena, 64) == 0);
        CHECK(ios27_libsystem_output(ctx, 2, got, sizeof got) == 64);
        CHECK(memcmp(got, arena + 64, 64) == 0);
        EXPECT_REJECT(ctx, "write after guest close", SYS(WRITE), 1u, 0u, 4u,
                      D_EBADF);
    }
    for (fd = 0; fd < 2; ++fd) {
        if (in_pipe[fd] >= 0) close(in_pipe[fd]);
        if (out_pipe[fd] >= 0) close(out_pipe[fd]);
    }
    ios27_libsystem_destroy(ctx);
}

int main(void) {
    test_create();
    test_null_context();
    test_guest_accessor();
    test_stdin();
    test_trap_numbers();
    test_descriptors();
    test_buffers();
    test_capture_limits();
    test_host_isolation();
    if (failures) {
        fprintf(stderr, "libsystem-user-adversarial: %d failure(s)\n",
                failures);
        return 1;
    }
    printf("libsystem-user-adversarial: all checks passed\n");
    return 0;
}
