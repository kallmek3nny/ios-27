#include "ios27/platform_memory.h"

#include <stdint.h>

void *IOS27_PLATFORM(memcpy)(void *destination, const void *source, size_t size) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0u; i < size; ++i) out[i] = in[i];
    return destination;
}

void *IOS27_PLATFORM(memmove)(void *destination, const void *source, size_t size) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    if ((uintptr_t)out <= (uintptr_t)in) {
        for (size_t i = 0u; i < size; ++i) out[i] = in[i];
    } else {
        while (size) { --size; out[size] = in[size]; }
    }
    return destination;
}

void *IOS27_PLATFORM(memset)(void *destination, int value, size_t size) {
    unsigned char *out = destination;
    for (size_t i = 0u; i < size; ++i) out[i] = (unsigned char)value;
    return destination;
}

void IOS27_PLATFORM(bzero)(void *destination, size_t size) {
    (void)IOS27_PLATFORM(memset)(destination, 0, size);
}

int IOS27_PLATFORM(memcmp)(const void *left, const void *right, size_t size) {
    const unsigned char *a = left, *b = right;
    for (size_t i = 0u; i < size; ++i)
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    return 0;
}

size_t IOS27_PLATFORM(strlen)(const char *string) {
    size_t length = 0u;
    while (string[length]) ++length;
    return length;
}

size_t IOS27_PLATFORM(strnlen)(const char *string, size_t limit) {
    size_t length = 0u;
    while (length < limit && string[length]) ++length;
    return length;
}

char *IOS27_PLATFORM(strcpy)(char *destination, const char *source) {
    size_t i = 0u;
    do { destination[i] = source[i]; } while (source[i++]);
    return destination;
}

char *IOS27_PLATFORM(strncpy)(char *destination, const char *source, size_t size) {
    size_t i = 0u;
    for (; i < size && source[i]; ++i) destination[i] = source[i];
    for (; i < size; ++i) destination[i] = '\0';
    return destination;
}

int IOS27_PLATFORM(strcmp)(const char *left, const char *right) {
    const unsigned char *a = (const unsigned char *)left, *b = (const unsigned char *)right;
    while (*a && *a == *b) { ++a; ++b; }
    return (int)*a - (int)*b;
}

int IOS27_PLATFORM(strncmp)(const char *left, const char *right, size_t limit) {
    const unsigned char *a = (const unsigned char *)left, *b = (const unsigned char *)right;
    for (size_t i = 0u; i < limit; ++i)
        if (a[i] != b[i] || !a[i]) return (int)a[i] - (int)b[i];
    return 0;
}

char *IOS27_PLATFORM(strstr)(const char *string, const char *needle) {
    if (!*needle) return (char *)string;
    for (; *string; ++string) {
        size_t i = 0u;
        while (needle[i] && string[i] && string[i] == needle[i]) ++i;
        if (!needle[i]) return (char *)string;
    }
    return NULL;
}
