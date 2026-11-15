#ifndef IOS27_PLATFORM_MEMORY_H
#define IOS27_PLATFORM_MEMORY_H

#include <stddef.h>

/* Original, allocation-free implementations of the standard memory/string
 * contracts. Buffers must be valid C objects. Darwin builds export the real
 * names; host tests use prefixed names to avoid replacing the host's libc. */
#if defined(IOS27_PLATFORM_EXPORT_DARWIN)
#define IOS27_PLATFORM(name) name
#else
#define IOS27_PLATFORM(name) ios27_platform_##name
#endif

void *IOS27_PLATFORM(memcpy)(void *destination, const void *source, size_t size);
void *IOS27_PLATFORM(memmove)(void *destination, const void *source, size_t size);
void *IOS27_PLATFORM(memset)(void *destination, int value, size_t size);
void IOS27_PLATFORM(bzero)(void *destination, size_t size);
int IOS27_PLATFORM(memcmp)(const void *left, const void *right, size_t size);
size_t IOS27_PLATFORM(strlen)(const char *string);
size_t IOS27_PLATFORM(strnlen)(const char *string, size_t limit);
char *IOS27_PLATFORM(strcpy)(char *destination, const char *source);
char *IOS27_PLATFORM(strncpy)(char *destination, const char *source, size_t size);
int IOS27_PLATFORM(strcmp)(const char *left, const char *right);
int IOS27_PLATFORM(strncmp)(const char *left, const char *right, size_t limit);
char *IOS27_PLATFORM(strstr)(const char *string, const char *needle);

#endif
