#include "ios27/phase26a_libc.h"
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    char *a = ios27_26a_malloc(128u);
    char *b = ios27_26a_calloc(2u, 64u);
    char *c, *name, *value;
    assert(a && b && ios27_26a_live_bytes() == 256u);
    assert(ios27_26a_calloc(SIZE_MAX, 2u) == NULL);
    assert(ios27_26a_last_error() == IOS27_26A_ENOMEM);
    assert(ios27_26a_malloc(IOS27_26A_LIVE_QUOTA + 1u) == NULL);
    assert(ios27_26a_memset(a, 0x35, 128u) == a);
    assert(ios27_26a_memmove(a + 1, a, 100u) == a + 1);
    assert(a[1] == '5' && a[100] == '5');
    assert(ios27_26a_memcpy(b, a, 128u) == b);
    assert(ios27_26a_memcmp(a, b, 128u) == 0);
    assert(ios27_26a_memcpy(a + 1, a, 10u) == NULL);
    assert(ios27_26a_last_error() == IOS27_26A_EINVAL);
    assert(ios27_26a_memcpy(b + 127, a, 2u) == NULL);
    assert(ios27_26a_last_error() == IOS27_26A_EFAULT);
    assert(ios27_26a_memcpy(b, (const void *)(uintptr_t)1234, 2u) == NULL);
    assert(ios27_26a_last_error() == IOS27_26A_EFAULT);
    a[0] = 'a'; a[1] = 'b'; a[2] = 0; a[3] = 'x';
    assert(ios27_26a_strlen(a) == 2u);
    assert(ios27_26a_strnlen(a, 1u) == 1u);
    assert(ios27_26a_strnlen(a, 4u) == 2u);
    assert(ios27_26a_strcpy(b, a) == b);
    assert(ios27_26a_strcmp(a, b) == 0);
    assert(ios27_26a_strncmp(a, b, 2u) == 0);
    b[1] = 'd';
    assert(ios27_26a_strcmp(a, b) < 0);
    assert(ios27_26a_strncmp(a, b, 1u) == 0);
    assert(ios27_26a_strncpy(b, a, 8u) == b);
    for (size_t i = 2u; i < 8u; ++i) assert(b[i] == 0);
    c = ios27_26a_strdup(a);
    assert(c && ios27_26a_strcmp(a, c) == 0);
    assert(ios27_26a_memset(b, 0, 128u) == b);
    assert(ios27_26a_strnlen(b, 128u) == 0u);
    name = ios27_26a_malloc(16u);
    value = ios27_26a_malloc(32u);
    assert(name && value);
    name[0] = 'T'; name[1] = 'E'; name[2] = 'S'; name[3] = 'T'; name[4] = 0;
    value[0] = 'o'; value[1] = 'k'; value[2] = 0;
    assert(ios27_26a_setenv(name, value, 1) == 0);
    assert(ios27_26a_getenv(name) && ios27_26a_strcmp(ios27_26a_getenv(name), value) == 0);
    value[0] = 'n';
    assert(ios27_26a_setenv(name, value, 0) == 0);
    assert(ios27_26a_getenv(name)[0] == 'o');
    assert(ios27_26a_setenv(name, value, 1) == 0);
    assert(ios27_26a_getenv(name)[0] == 'n');
    assert(ios27_26a_unsetenv(name) == 0 && ios27_26a_getenv(name) == NULL);
    assert(ios27_26a_realloc((void *)(uintptr_t)1234, 16u) == NULL);
    assert(ios27_26a_last_error() == IOS27_26A_EFAULT);
    c = ios27_26a_realloc(c, 64u);
    assert(c && c[0] == 'a' && c[1] == 'b' && c[2] == 0);
    assert(ios27_26a_realloc(a, SIZE_MAX) == NULL);
    assert(a[0] == 'a'); /* old allocation retained on failure */

    {
        char *fmt = ios27_26a_malloc(64u);
        char *numbers = ios27_26a_malloc(64u);
        char **last = ios27_26a_malloc(sizeof(*last));
        const char pattern[] = "v=%d x=%x s=%s %%";
        const char minus[] = "-42END";
        const char overflowing[] = "9999999999999999999999999999";
        assert(fmt && numbers && last);
        for (size_t i = 0u; i < sizeof(pattern); ++i) fmt[i] = pattern[i];
        for (size_t i = 0u; i < sizeof(minus); ++i) numbers[i] = minus[i];
        a[0] = 'o'; a[1] = 'k'; a[2] = 0;
        assert(ios27_26a_snprintf(b, 128u, fmt, -42, 42u, a) == 18);
        assert(strcmp(b, "v=-42 x=2a s=ok %") == 0);
        assert(ios27_26a_snprintf(b, 4u, fmt, -42, 42u, a) == 18);
        assert(strcmp(b, "v=-") == 0);
        assert(ios27_26a_strtol(numbers, last, 10) == -42);
        assert(**last == 'E');
        for (size_t i = 0u; i < sizeof(overflowing); ++i)
            numbers[i] = overflowing[i];
        assert(ios27_26a_strtol(numbers, last, 10) == LONG_MAX);
        assert(ios27_26a_last_error() == IOS27_26A_ERANGE);
        assert(ios27_26a_strerror(IOS27_26A_ENOMEM)[0] == 'C');
        assert(ios27_26a_strerror(9999)[0] == 'U');
        fmt[3] = 'f'; /* unsupported specifier must not modify output */
        assert(ios27_26a_snprintf(b, 128u, fmt, 3.14) == -1);
        assert(strcmp(b, "v=-") == 0);
        ios27_26a_free(fmt);
        ios27_26a_free(numbers);
        ios27_26a_free(last);
    }
    for (unsigned i = 0u; i < 210000u; ++i) {
        assert(ios27_26a_memset(b, (int)(i & 255u), 64u) == b);
        assert(ios27_26a_memmove(b + 1, b, 60u) == b + 1);
        assert(ios27_26a_memcpy(a, b, 64u) == a);
        assert(ios27_26a_memcmp(a, b, 64u) == 0);
    }
    assert(ios27_26a_memset(a + 127, 1, 2u) == NULL);
    assert(ios27_26a_last_error() == IOS27_26A_EFAULT);
    ios27_26a_free(a);
    assert(ios27_26a_live_bytes() > 0u);
    ios27_26a_free(a);
    assert(ios27_26a_last_error() == IOS27_26A_EINVAL);
    ios27_26a_free((void *)(uintptr_t)1234);
    assert(ios27_26a_last_error() == IOS27_26A_EFAULT);
    ios27_26a_free(b);
    ios27_26a_free(c);
    ios27_26a_free(name);
    ios27_26a_free(value);
    assert(ios27_26a_live_bytes() == 0u);
    puts("phase26a: 840000 verified bounded native-host libc operations");
    return 0;
}
