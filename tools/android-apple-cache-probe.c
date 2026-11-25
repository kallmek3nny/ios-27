/* Standalone ARM64 Android probe for the audited iOS 27.0 Simulator cache.
 * The caller must verify all four complete files against trusted inputs.
 * Cache and SpringBoard mappings are non-executable. Two reviewed leaf
 * routines run from separate RX pages.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(__aarch64__) || !defined(__ANDROID__)
#error "This probe requires native ARM64 Android."
#endif

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static const struct cache_mapping {
    unsigned part;
    uintptr_t address;
    size_t size;
    off_t offset;
} mappings[] = {
    {0, 0x1c0000000, 0x62270000, 0},
    {0, 0x222270000, 0x0a204000, 0x62270000},
    {0, 0x22e474000, 0x00004000, 0x6c474000},
    {0, 0x22e478000, 0x05c18000, 0x6c478000},
    {0, 0x236090000, 0x07844000, 0x72090000},
    {1, 0x23d8d4000, 0x23f48000, 0},
    {1, 0x26181c000, 0x03308000, 0x23f48000},
    {1, 0x266b24000, 0x00004000, 0x27250000},
    {1, 0x266b28000, 0x02340000, 0x27254000},
    {1, 0x26ae68000, 0x001a8000, 0x29594000},
    {1, 0x26b010000, 0x40cb8000, 0x2973c000},
};

enum {
    PAGE = 4096,
    POINT_BYTES = 20,
    IDENTITY_BYTES = 180,
    CONSTANT_BYTES = 128,
    IDENTITY_OFFSET = 0x53c,
    CONSTANT_PAGE = 0x121000,
    CONSTANT_OFFSET = CONSTANT_PAGE + 0x200,
};

typedef struct { double x, y; } Point;
typedef uint32_t (*PointEqual)(Point, Point);
typedef uint32_t (*TransformIsIdentity)(const double *);

static void fail(const char *message)
{
    perror(message);
    exit(1);
}

/* A local mismatch check in addition to the full cache SHA-256 verification. */
static uint64_t fingerprint(const unsigned char *data, size_t size)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ data[i]) * UINT64_C(1099511628211);
    return hash;
}

static void *map_readonly(int fd, uintptr_t address, size_t size, off_t offset)
{
    void *wanted = (void *)address;
    void *p = mmap(wanted, size, PROT_READ, MAP_PRIVATE | MAP_FIXED_NOREPLACE, fd, offset);
    if (p == MAP_FAILED)
        fail("map image at requested address");
    if (p != wanted) {
        fputs("kernel did not honor the requested address\n", stderr);
        exit(1);
    }
    return p;
}

static void map_cache(const char *const paths[2])
{
    const off_t sizes[] = {2043297792, 1786019840};
    int fd[2];
    for (unsigned i = 0; i < 2; ++i) {
        struct stat st;
        char magic[16];
        fd[i] = open(paths[i], O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd[i] < 0 || fstat(fd[i], &st) != 0)
            fail("open cache");
        if (!S_ISREG(st.st_mode) || st.st_size != sizes[i] ||
            pread(fd[i], magic, sizeof(magic), 0) != (ssize_t)sizeof(magic) ||
            memcmp(magic, "dyld_v1   arm64", sizeof(magic)) != 0) {
            fprintf(stderr, "unexpected cache part %u\n", i);
            exit(1);
        }
    }
    size_t total = 0;
    for (size_t i = 0; i < sizeof(mappings) / sizeof(mappings[0]); ++i) {
        const struct cache_mapping *m = &mappings[i];
        map_readonly(fd[m->part], m->address, m->size, m->offset);
        total += m->size;
    }
    close(fd[0]);
    close(fd[1]);
    printf("Mapped %zu cache regions at preferred addresses (read-only, %zu bytes)\n",
           sizeof(mappings) / sizeof(mappings[0]), total);
}

static unsigned char *map_standalone(const char *path, uintptr_t address, off_t size,
                                     uint32_t filetype)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat st;
    uint32_t header[4];
    if (fd < 0 || fstat(fd, &st) != 0)
        fail("open SpringBoard image");
    if (!S_ISREG(st.st_mode) || st.st_size != size ||
        pread(fd, header, sizeof(header), 0) != (ssize_t)sizeof(header) ||
        header[0] != 0xfeedfacf || header[1] != 0x0100000c ||
        header[2] != 0 || header[3] != filetype) {
        fputs("unexpected SpringBoard Mach-O image\n", stderr);
        exit(1);
    }
    unsigned char *image = map_readonly(fd, address, (size_t)size, 0);
    close(fd);
    return image;
}

static void stage_app_bind(const char *app_path, const char *framework_path)
{
    /* The audited build has identical file and image-relative offsets in
     * its file-backed SpringBoard segments. Keep both images non-executable. */
    unsigned char *app = map_standalone(app_path, 0x100000000, 133872, 2);
    const unsigned char *framework =
        map_standalone(framework_path, 0x110000000, 36539312, 6);
    unsigned char *slot = app + 0x18008;
    const unsigned char *entry = framework + 0x8afc;
    uint64_t raw;
    memcpy(&raw, slot, sizeof(raw));
    if (raw != UINT64_C(0x8000000000000000) ||
        fingerprint(app + 0x618, 16) != UINT64_C(0x62038f263884e6ba) ||
        fingerprint(entry, 64) != UINT64_C(0x33cfbab73868c820)) {
        fputs("unexpected SpringBoard bind slot or entry instructions\n", stderr);
        exit(1);
    }
    if (mprotect(app + 0x18000, PAGE, PROT_READ | PROT_WRITE))
        fail("open SpringBoard bind page");
    uintptr_t target = (uintptr_t)entry;
    memcpy(slot, &target, sizeof(target));
    if (mprotect(app + 0x18000, PAGE, PROT_READ))
        fail("seal SpringBoard bind page");
    memcpy(&raw, slot, sizeof(raw));
    if (raw != target) {
        fputs("SpringBoard bind readback mismatch\n", stderr);
        exit(1);
    }
    puts("Staged the real SpringBoard _SBSystemAppMain bind (images non-executable)");
}

static int restrict_child(void)
{
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_exit, 2, 0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_exit_group, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog program = {sizeof(filter) / sizeof(filter[0]), filter};
    return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
           prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program);
}

int main(int argc, char **argv)
{
    if (argc != 5 || sysconf(_SC_PAGESIZE) != PAGE) {
        fprintf(stderr, "usage on 4 KiB ARM64 Android: %s CACHE CACHE.01 APP FRAMEWORK\n", argv[0]);
        return 2;
    }
    const char *paths[] = {argv[1], argv[2]};
    map_cache(paths);
    stage_app_bind(argv[3], argv[4]);
    const unsigned char *point = (const void *)(uintptr_t)0x1c3363bec;
    const unsigned char *identity = (const void *)(uintptr_t)0x1c3a8553c;
    const unsigned char *constant = (const void *)(uintptr_t)0x1c3ba6200;
    if (fingerprint(point, POINT_BYTES) != UINT64_C(0xf93244266bceb2df) ||
        fingerprint(identity, IDENTITY_BYTES) != UINT64_C(0xa981580484c79cfb) ||
        fingerprint(constant, CONSTANT_BYTES) != UINT64_C(0x78704a9e6b24e2e5)) {
        fputs("unexpected leaf instructions or constants\n", stderr);
        return 1;
    }

    /* Preserve QuartzCore's PC-relative distance to the 128-byte constant. */
    unsigned char *arena = mmap(NULL, CONSTANT_PAGE + PAGE, PROT_NONE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED || mprotect(arena, PAGE, PROT_READ | PROT_WRITE) ||
        mprotect(arena + CONSTANT_PAGE, PAGE, PROT_READ | PROT_WRITE))
        fail("stage leaf code and data");
    memcpy(arena, point, POINT_BYTES);
    memcpy(arena + IDENTITY_OFFSET, identity, IDENTITY_BYTES);
    memcpy(arena + CONSTANT_OFFSET, constant, CONSTANT_BYTES);
    __builtin___clear_cache((char *)arena, (char *)arena + PAGE);
    if (mprotect(arena + CONSTANT_PAGE, PAGE, PROT_READ) ||
        mprotect(arena, PAGE, PROT_READ | PROT_EXEC))
        fail("seal leaf code and data");

    /* POSIX-style function-pointer conversion via representation copy. */
    PointEqual equal;
    TransformIsIdentity is_identity;
    unsigned char *identity_code = arena + IDENTITY_OFFSET;
    _Static_assert(sizeof(equal) == sizeof(arena), "ARM64 function pointer width");
    _Static_assert(sizeof(is_identity) == sizeof(identity_code), "ARM64 function pointer width");
    memcpy(&equal, &arena, sizeof(equal));
    memcpy(&is_identity, &identity_code, sizeof(is_identity));
    const double matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    double translated[16];
    memcpy(translated, matrix, sizeof(matrix));
    translated[12] = 2;

    pid_t child = fork();
    if (child < 0)
        fail("fork");
    if (!child) {
        if (restrict_child())
            _exit(10);
        if (equal((Point){1.5, -2}, (Point){1.5, -2}) != 1 ||
            equal((Point){1.5, -2}, (Point){1.5, 2}) != 0)
            _exit(11);
        if (is_identity(matrix) != 1 || is_identity(translated) != 0)
            _exit(12);
        _exit(0);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            fail("waitpid");
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "leaf child terminated by signal %d\n", WTERMSIG(status));
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "leaf child failed, wait status %#x\n", status);
        return 1;
    }
    puts("PASS: native Apple CoreGraphics CGPointEqualToPoint (equal / unequal)");
    puts("PASS: native Apple QuartzCore CATransform3DIsIdentity (identity / translated)");
    return 0;
}
