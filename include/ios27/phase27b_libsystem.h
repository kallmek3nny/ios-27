#ifndef IOS27_PHASE27B_LIBSYSTEM_H
#define IOS27_PHASE27B_LIBSYSTEM_H
#include <stddef.h>
#include <stdint.h>
#include "ios27/darwin_shim.h"

/* SDK-free, explicitly bounded native ARM64 Darwin fixture declarations.
 * Source objects resolve ordinary named C symbols at link time. This is
 * NOT a complete Apple libSystem, dynamic linker, or untrusted-code API. */
void *malloc(size_t size);
void *calloc(size_t count, size_t bytes);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
void *memcpy(void *destination, const void *source, size_t size);
void *memmove(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);
int memcmp(const void *left, const void *right, size_t size);
size_t strlen(const char *text);
size_t strnlen(const char *text, size_t limit);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t limit);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t limit);
char *strdup(const char *source);
int snprintf(char *destination, size_t size, const char *format, ...);
long strtol(const char *input, char **endptr, int base);
char *strerror(int error);
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
void qsort(void *base, size_t count, size_t element_bytes,
           int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count,
              size_t element_bytes, int (*compare)(const void *, const void *));
int atexit(void (*handler)(void));
/* Explicit fixture-finalization hook. Generic LC_MAIN return does NOT
 * invoke registered atexit callbacks yet. */
void ios27_27b_run_exit_handlers(void);

#define IOS27_27B_AT_FDCWD (-2)
#define IOS27_27B_F_GETFL 3
int32_t openat(int32_t dirfd, const char *path, int32_t flags, ...);
int64_t pread(int32_t fd, void *buffer, uint64_t count, int64_t offset);
int64_t pwrite(int32_t fd, const void *buffer, uint64_t count, int64_t offset);
/* 144-byte Darwin inode64 fixture layout, NOT a native host struct stat. */
int32_t fstat(int32_t fd, void *darwin_stat_144);
int32_t fcntl(int32_t fd, int32_t command, ...);
int32_t gettimeofday(void *timeval_16, void *timezone);
#endif
