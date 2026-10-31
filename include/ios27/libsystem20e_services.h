#ifndef IOS27_LIBSYSTEM20E_SERVICES_H
#define IOS27_LIBSYSTEM20E_SERVICES_H
#include <stddef.h>
#include <stdint.h>

/* Original-source, sandboxed Darwin-like ABI boundary. Guest pointers are
 * offsets, NEVER host pointers. All structures are caller-owned. */
#define IOS27_20E_BYTES 65536u
#define IOS27_20E_PAGE 4096u
#define IOS27_20E_FDS 16u
#define IOS27_20E_FILES 8u
#define IOS27_20E_BLOCKS 32u
#define IOS27_20E_MAP_FIRST 8u
#define IOS27_20E_MAP_PAGES 8u
#define IOS27_20E_FILE_BYTES 4096u
#define IOS27_20E_RDONLY 0
#define IOS27_20E_WRONLY 1
#define IOS27_20E_RDWR 2
#define IOS27_20E_APPEND 0x0008
#define IOS27_20E_CREAT 0x0200
#define IOS27_20E_TRUNC 0x0400
#define IOS27_20E_PROT_READ 1
#define IOS27_20E_PROT_WRITE 2
#define IOS27_20E_PROT_EXEC 4
#define IOS27_20E_MAP_PRIVATE 2
#define IOS27_20E_MAP_ANON 0x1000
#define IOS27_20E_SEEK_SET 0
#define IOS27_20E_SEEK_CUR 1
#define IOS27_20E_SEEK_END 2
#define IOS27_20E_ENOENT 2
#define IOS27_20E_EBADF 9
#define IOS27_20E_ENOMEM 12
#define IOS27_20E_EACCES 13
#define IOS27_20E_EFAULT 14
#define IOS27_20E_EEXIST 17
#define IOS27_20E_EINVAL 22
#define IOS27_20E_EMFILE 24
#define IOS27_20E_ENOSPC 28
#define IOS27_20E_EROFS 30
#define IOS27_20E_ERANGE 34
#define IOS27_20E_ENOSYS 78
#define IOS27_20E_EOVERFLOW 84

typedef struct { uint8_t used, writable; char path[96]; uint8_t data[IOS27_20E_FILE_BYTES]; size_t len; } ios27_20e_file;
typedef struct { uint8_t used; uint8_t kind; uint8_t mode; uint8_t pad; size_t pos; } ios27_20e_fd;
typedef struct { uint8_t used; uint64_t addr, bytes; } ios27_20e_block;
typedef struct { uint8_t mapped, prot; } ios27_20e_page;
typedef struct {
    uint8_t guest[IOS27_20E_BYTES];
    ios27_20e_file files[IOS27_20E_FILES];
    ios27_20e_fd fds[IOS27_20E_FDS];
    ios27_20e_block blocks[IOS27_20E_BLOCKS];
    ios27_20e_page pages[IOS27_20E_MAP_PAGES];
    uint8_t input[IOS27_20E_FILE_BYTES]; size_t input_len;
    uint8_t output[IOS27_20E_FILE_BYTES]; size_t output_len;
    uint8_t errors[IOS27_20E_FILE_BYTES]; size_t errors_len;
    int guest_errno;
    uint64_t guest_pid, guest_uid, guest_gid;
    int64_t unix_seconds; int32_t microseconds;
    char env_key[8][32], env_val[8][128]; uint8_t env_count;
} ios27_20e_ctx;

void ios27_20e_init(ios27_20e_ctx *ctx);
int ios27_20e_seed_file(ios27_20e_ctx *ctx, const char *path, const uint8_t *data, size_t len, int writable);
int ios27_20e_setenv(ios27_20e_ctx *ctx, const char *key, const char *value);
int64_t ios27_20e_getenv(ios27_20e_ctx *ctx, uint64_t name, uint64_t destination, uint64_t capacity);
int64_t ios27_20e_open(ios27_20e_ctx *ctx, uint64_t path, int flags);
int64_t ios27_20e_close(ios27_20e_ctx *ctx, int fd);
int64_t ios27_20e_read(ios27_20e_ctx *ctx, int fd, uint64_t destination, uint64_t count);
int64_t ios27_20e_write(ios27_20e_ctx *ctx, int fd, uint64_t source, uint64_t count);
int64_t ios27_20e_lseek(ios27_20e_ctx *ctx, int fd, int64_t delta, int whence);
int64_t ios27_20e_gettimeofday(ios27_20e_ctx *ctx, uint64_t tv);
int64_t ios27_20e_advance_time(ios27_20e_ctx *ctx, uint64_t microseconds);
uint64_t ios27_20e_malloc(ios27_20e_ctx *ctx, uint64_t bytes);
uint64_t ios27_20e_calloc(ios27_20e_ctx *ctx, uint64_t count, uint64_t bytes);
uint64_t ios27_20e_realloc(ios27_20e_ctx *ctx, uint64_t addr, uint64_t bytes);
int64_t ios27_20e_free(ios27_20e_ctx *ctx, uint64_t addr);
int64_t ios27_20e_mmap(ios27_20e_ctx *ctx, uint64_t hint, uint64_t length, int prot, int flags, int fd, uint64_t offset);
int64_t ios27_20e_mprotect(ios27_20e_ctx *ctx, uint64_t addr, uint64_t length, int prot);
int64_t ios27_20e_munmap(ios27_20e_ctx *ctx, uint64_t addr, uint64_t length);
/* Validates guest memory including per-page protection. Returns null on fault. */
void *ios27_20e_guest_ptr(ios27_20e_ctx *ctx, uint64_t addr, uint64_t len, int write_access);
/* Raw Darwin BSD syscall numbers, unsupported calls always ENOSYS. */
int64_t ios27_20e_syscall(ios27_20e_ctx *ctx, uint32_t number, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
#endif
