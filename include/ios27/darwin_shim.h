#ifndef IOS27_DARWIN_SHIM_H
#define IOS27_DARWIN_SHIM_H

#include <stdint.h>

/* SDK-free, fixed-width declarations for the original static research shim.
 * These named functions marshal ordinary C buffers to the bounded service
 * arena. They are not a replacement Apple SDK or a general libSystem ABI.
 * Single guest thread, <=4096 bytes per read/write, <=95-byte paths. The
 * current virtual model accepts but does not implement open's mode argument.
 * mprotect/munmap use page-aligned addresses and 4096-byte-multiple lengths.
 */
#define IOS27_DARWIN_O_RDONLY 0
#define IOS27_DARWIN_O_RDWR 2
#define IOS27_DARWIN_O_CREAT 0x0200
#define IOS27_DARWIN_PROT_NONE 0
#define IOS27_DARWIN_PROT_READ 1
#define IOS27_DARWIN_PROT_WRITE 2
#define IOS27_DARWIN_MAP_PRIVATE 2
#define IOS27_DARWIN_MAP_ANON 0x1000
#define IOS27_DARWIN_MAP_FAILED ((void *)(intptr_t)-1)

/* Compile the SAME shim and client as host tests without interposing the
 * test process's real libc functions. Darwin builds retain the real names. */
#if defined(IOS27_SHIM_HOST_TEST)
#define getpid ios27_shim_getpid
#define open ios27_shim_open
#define read ios27_shim_read
#define write ios27_shim_write
#define close ios27_shim_close
#define lseek ios27_shim_lseek
#define mmap ios27_shim_mmap
#define mprotect ios27_shim_mprotect
#define munmap ios27_shim_munmap
#define __error ios27_shim_error
#endif

int32_t getpid(void);
int32_t open(const char *path, int32_t flags, ...);
int64_t read(int32_t fd, void *buffer, uint64_t count);
int64_t write(int32_t fd, const void *buffer, uint64_t count);
int32_t close(int32_t fd);
int64_t lseek(int32_t fd, int64_t offset, int32_t whence);
void *mmap(void *hint, uint64_t length, int32_t protection, int32_t flags,
           int32_t fd, int64_t offset);
int32_t mprotect(void *address, uint64_t length, int32_t protection);
int32_t munmap(void *address, uint64_t length);
int32_t *__error(void);

#endif
