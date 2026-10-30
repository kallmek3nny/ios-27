#include "ios27/phase26a_libc.h"
#include <stddef.h>
#include <stdint.h>

/* Original-source freestanding libc subset. Each Mach-O mapping owns its
 * state. All accepted data pointers must refer to a live allocation from
 * THIS mapping, or to a value owned by its bounded environment. This does
 * not accept arbitrary host, stack, or unrelated image pointers. */
#ifdef PHASE26A_DARWIN_EXPORT
#define P26(x) x
#else
#define P26(x) ios27_26a_##x
#endif
#define HEAP_BYTES (256u * 1024u)
#define MAX_BLOCKS 256u
#define MAX_ENV 16u
#define KEY_BYTES 32u
#define VALUE_BYTES 128u

typedef struct { size_t offset, length; unsigned active; } block;
typedef struct { char name[KEY_BYTES]; char value[VALUE_BYTES]; unsigned used; } env_entry;
static union { max_align_t alignment; unsigned char bytes[HEAP_BYTES]; } heap;
static block blocks[MAX_BLOCKS];
static env_entry env[MAX_ENV];
static size_t head, live_bytes, block_count;
static int last_error;

static int inside(const void *ptr, size_t count, const void *base, size_t span) {
    uintptr_t p = (uintptr_t)ptr, b = (uintptr_t)base;
    return p >= b && p - b <= span && count <= span - (size_t)(p - b);
}
static int permitted(const void *ptr, size_t count, int writable) {
    if (count == 0u) return 1;
    if (!ptr) return 0;
    for (size_t i = 0u; i < block_count; ++i)
        if (blocks[i].active &&
            inside(ptr, count, heap.bytes + blocks[i].offset, blocks[i].length))
            return 1;
    if (!writable)
        for (size_t i = 0u; i < MAX_ENV; ++i)
            if (env[i].used && inside(ptr, count, env[i].value, VALUE_BYTES))
                return 1;
    return 0;
}
static int require(const void *ptr, size_t count, int write) {
    if (permitted(ptr, count, write)) return 1;
    last_error = IOS27_26A_EFAULT;
    return 0;
}
static size_t cstr(const char *s) {
    size_t n = 0u;
    while (require(s + n, 1u, 0)) {
        if (!s[n]) return n;
        ++n;
    }
    return SIZE_MAX;
}
static void zero_bytes(void *dst, size_t count) {
    unsigned char *p = dst;
    for (size_t i = 0u; i < count; ++i) p[i] = 0u;
}
static void copy_bytes(void *dst, const void *src, size_t count) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (size_t i = 0u; i < count; ++i) d[i] = s[i];
}

void *P26(malloc)(size_t n) {
    size_t offset;
    if (!n) n = 1u;
    if (n > HEAP_BYTES || block_count == MAX_BLOCKS ||
        n > HEAP_BYTES - head || n > IOS27_26A_LIVE_QUOTA - live_bytes) {
        last_error = IOS27_26A_ENOMEM;
        return NULL;
    }
    offset = head;
    head += n;
    if (head > HEAP_BYTES - 15u) head = HEAP_BYTES;
    else head = (head + 15u) & ~(size_t)15u;
    blocks[block_count].offset = offset;
    blocks[block_count].length = n;
    blocks[block_count++].active = 1u;
    live_bytes += n;
    zero_bytes(heap.bytes + offset, n);
    last_error = 0;
    return heap.bytes + offset;
}
void *P26(calloc)(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) {
        last_error = IOS27_26A_ENOMEM;
        return NULL;
    }
    return P26(malloc)(count * size);
}
void P26(free)(void *ptr) {
    if (!ptr) return;
    for (size_t i = 0u; i < block_count; ++i)
        if (ptr == (void *)(heap.bytes + blocks[i].offset)) {
            if (!blocks[i].active) {
                last_error = IOS27_26A_EINVAL;
                return;
            }
            zero_bytes(ptr, blocks[i].length);
            live_bytes -= blocks[i].length;
            blocks[i].active = 0u;
            last_error = 0;
            return;
        }
    last_error = IOS27_26A_EFAULT;
}
void *P26(realloc)(void *ptr, size_t n) {
    size_t old = 0u;
    void *next;
    if (!ptr) return P26(malloc)(n);
    if (!n) { P26(free)(ptr); return NULL; }
    for (size_t i = 0u; i < block_count; ++i)
        if (ptr == (void *)(heap.bytes + blocks[i].offset) && blocks[i].active) {
            old = blocks[i].length;
            break;
        }
    if (!old) { last_error = IOS27_26A_EFAULT; return NULL; }
    if (n == old) { last_error = 0; return ptr; }
    next = P26(malloc)(n);
    if (!next) return NULL; /* original allocation stays valid */
    copy_bytes(next, ptr, old < n ? old : n);
    P26(free)(ptr);
    return next;
}
void *P26(memset)(void *dst, int c, size_t n) {
    if (!require(dst, n, 1)) return NULL;
    unsigned char *p = dst;
    for (size_t i = 0u; i < n; ++i) p[i] = (unsigned char)c;
    last_error = 0;
    return dst;
}
void *P26(memmove)(void *dst, const void *src, size_t n) {
    if (!require(dst, n, 1) || !require(src, n, 0)) return NULL;
    unsigned char *d = dst;
    const unsigned char *s = src;
    if ((uintptr_t)d > (uintptr_t)s && (uintptr_t)d - (uintptr_t)s < n)
        for (size_t i = n; i; --i) d[i - 1u] = s[i - 1u];
    else copy_bytes(dst, src, n);
    last_error = 0;
    return dst;
}
void *P26(memcpy)(void *dst, const void *src, size_t n) {
    uintptr_t d = (uintptr_t)dst, s = (uintptr_t)src;
    if ((d >= s && d - s < n) || (s > d && s - d < n)) {
        last_error = IOS27_26A_EINVAL;
        return NULL;
    }
    return P26(memmove)(dst, src, n);
}
int P26(memcmp)(const void *a, const void *b, size_t n) {
    if (!require(a, n, 0) || !require(b, n, 0)) return 0;
    const unsigned char *x = a, *y = b;
    for (size_t i = 0u; i < n; ++i)
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    last_error = 0;
    return 0;
}
size_t P26(strlen)(const char *s) {
    size_t n = cstr(s);
    if (n != SIZE_MAX) last_error = 0;
    return n;
}
size_t P26(strnlen)(const char *s, size_t max) {
    for (size_t i = 0u; i < max; ++i) {
        if (!require(s + i, 1u, 0)) return SIZE_MAX;
        if (!s[i]) { last_error = 0; return i; }
    }
    last_error = 0;
    return max;
}
int P26(strcmp)(const char *a, const char *b) {
    for (size_t i = 0u;; ++i) {
        if (!require(a + i, 1u, 0) || !require(b + i, 1u, 0)) return 0;
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x != y) return (int)x - (int)y;
        if (!x) { last_error = 0; return 0; }
    }
}
int P26(strncmp)(const char *a, const char *b, size_t n) {
    for (size_t i = 0u; i < n; ++i) {
        if (!require(a + i, 1u, 0) || !require(b + i, 1u, 0)) return 0;
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x != y) return (int)x - (int)y;
        if (!x) break;
    }
    last_error = 0;
    return 0;
}
char *P26(strcpy)(char *dst, const char *src) {
    size_t n = cstr(src);
    if (n == SIZE_MAX || n == SIZE_MAX - 1u || !require(dst, n + 1u, 1))
        return NULL;
    copy_bytes(dst, src, n + 1u);
    last_error = 0;
    return dst;
}
char *P26(strncpy)(char *dst, const char *src, size_t n) {
    if (!require(dst, n, 1)) return NULL;
    size_t i = 0u;
    for (; i < n; ++i) {
        if (!require(src + i, 1u, 0)) return NULL;
        dst[i] = src[i];
        if (!src[i]) { ++i; break; }
    }
    for (; i < n; ++i) dst[i] = 0;
    last_error = 0;
    return dst;
}
char *P26(strdup)(const char *src) {
    size_t n = cstr(src);
    char *dst;
    if (n == SIZE_MAX || n == SIZE_MAX - 1u) return NULL;
    dst = P26(malloc)(n + 1u);
    if (dst) copy_bytes(dst, src, n + 1u);
    return dst;
}
static int name_ok(const char *name, size_t *length) {
    size_t n = cstr(name);
    if (n == SIZE_MAX || n == 0u || n >= KEY_BYTES) return 0;
    for (size_t i = 0u; i < n; ++i) if (name[i] == '=') return 0;
    *length = n;
    return 1;
}
static int match(const char *a, const char *b, size_t n) {
    for (size_t i = 0u; i < n; ++i) if (a[i] != b[i]) return 0;
    return b[n] == 0;
}
char *P26(getenv)(const char *name) {
    size_t n;
    if (!name_ok(name, &n)) { last_error = IOS27_26A_EINVAL; return NULL; }
    for (size_t i = 0u; i < MAX_ENV; ++i)
        if (env[i].used && match(name, env[i].name, n)) {
            last_error = 0;
            return env[i].value;
        }
    last_error = 0;
    return NULL;
}
int P26(setenv)(const char *name, const char *value, int overwrite) {
    size_t n, m, slot = MAX_ENV;
    if (!name_ok(name, &n) || (m = cstr(value)) == SIZE_MAX || m >= VALUE_BYTES) {
        last_error = IOS27_26A_EINVAL; return -1;
    }
    for (size_t i = 0u; i < MAX_ENV; ++i)
        if (env[i].used && match(name, env[i].name, n)) {
            if (!overwrite) return 0;
            slot = i;
            break;
        }
    if (slot == MAX_ENV)
        for (size_t i = 0u; i < MAX_ENV; ++i)
            if (!env[i].used) { slot = i; break; }
    if (slot == MAX_ENV) { last_error = IOS27_26A_ENOMEM; return -1; }
    copy_bytes(env[slot].name, name, n + 1u);
    copy_bytes(env[slot].value, value, m + 1u);
    env[slot].used = 1u;
    last_error = 0;
    return 0;
}
int P26(unsetenv)(const char *name) {
    size_t n;
    if (!name_ok(name, &n)) { last_error = IOS27_26A_EINVAL; return -1; }
    for (size_t i = 0u; i < MAX_ENV; ++i)
        if (env[i].used && match(name, env[i].name, n)) {
            zero_bytes(&env[i], sizeof(env[i]));
            break;
        }
    last_error = 0;
    return 0;
}
int ios27_26a_last_error(void) { return last_error; }
size_t ios27_26a_live_bytes(void) { return live_bytes; }
