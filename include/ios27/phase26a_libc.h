#ifndef IOS27_PHASE26A_LIBC_H
#define IOS27_PHASE26A_LIBC_H

#include <stddef.h>

/* Only trusted, compiler-produced fixtures are permitted. This is an
 * independent in-image heap, not host libc or Apple libSystem. The supported
 * symbols reject pointers outside the active per-image allocation set. */
#define IOS27_26A_LIVE_QUOTA (128u * 1024u)
#define IOS27_26A_EFAULT 14
#define IOS27_26A_ENOMEM 12
#define IOS27_26A_EINVAL 22

void *ios27_26a_malloc(size_t count);
void *ios27_26a_calloc(size_t count, size_t bytes);
void *ios27_26a_realloc(void *ptr, size_t count);
void ios27_26a_free(void *ptr);
void *ios27_26a_memcpy(void *dst, const void *src, size_t count);
void *ios27_26a_memmove(void *dst, const void *src, size_t count);
void *ios27_26a_memset(void *dst, int byte, size_t count);
int ios27_26a_memcmp(const void *left, const void *right, size_t count);
size_t ios27_26a_strlen(const char *str);
size_t ios27_26a_strnlen(const char *str, size_t max);
int ios27_26a_strcmp(const char *left, const char *right);
int ios27_26a_strncmp(const char *left, const char *right, size_t count);
char *ios27_26a_strcpy(char *dst, const char *src);
char *ios27_26a_strncpy(char *dst, const char *src, size_t count);
char *ios27_26a_strdup(const char *str);
char *ios27_26a_getenv(const char *name);
int ios27_26a_setenv(const char *name, const char *value, int overwrite);
int ios27_26a_unsetenv(const char *name);
int ios27_26a_last_error(void);
size_t ios27_26a_live_bytes(void);
#endif
