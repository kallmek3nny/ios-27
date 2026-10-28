#ifndef IOS27_LIBSYSTEM21A_ABI_H
#define IOS27_LIBSYSTEM21A_ABI_H
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define IOS27_21A_STATIC_ASSERT static_assert
#define IOS27_21A_ALIGNOF alignof
extern "C" {
#else
#define IOS27_21A_STATIC_ASSERT _Static_assert
#define IOS27_21A_ALIGNOF _Alignof
#endif

/* Arm64 Darwin LP64 object envelopes, not host pthread types.
 * Opaque contents belong to this bridge, NOT Apple's libpthread. */
typedef struct { uint64_t sig; unsigned char opaque[56]; } ios27_21a_mutex;
typedef struct { uint64_t sig; unsigned char opaque[40]; } ios27_21a_cond;
typedef struct { uint64_t sig; unsigned char opaque[192]; } ios27_21a_rwlock;
typedef struct { uint64_t sig; unsigned char opaque[8]; } ios27_21a_once;
typedef struct { uint64_t sig; unsigned char opaque[56]; } ios27_21a_attr;
typedef struct { uint64_t sig; unsigned char opaque[8]; } ios27_21a_mutexattr;
typedef struct { uint64_t sig; unsigned char opaque[8]; } ios27_21a_condattr;
typedef struct { uint64_t sig; unsigned char opaque[16]; } ios27_21a_rwlockattr;
typedef uint64_t ios27_21a_thread; /* Darwin pthread_t is a guest pointer. */
typedef uint64_t ios27_21a_key;

IOS27_21A_STATIC_ASSERT(sizeof(ios27_21a_mutex) == 64, "Darwin mutex size");
IOS27_21A_STATIC_ASSERT(sizeof(ios27_21a_cond) == 48, "Darwin cond size");
IOS27_21A_STATIC_ASSERT(sizeof(ios27_21a_rwlock) == 200, "Darwin rwlock size");
IOS27_21A_STATIC_ASSERT(sizeof(ios27_21a_once) == 16, "Darwin once size");
IOS27_21A_STATIC_ASSERT(sizeof(ios27_21a_attr) == 64, "Darwin attr size");
IOS27_21A_STATIC_ASSERT(sizeof(ios27_21a_mutexattr) == 16, "Darwin mutexattr size");
IOS27_21A_STATIC_ASSERT(sizeof(ios27_21a_condattr) == 16, "Darwin condattr size");
IOS27_21A_STATIC_ASSERT(sizeof(ios27_21a_rwlockattr) == 24, "Darwin rwlockattr size");
IOS27_21A_STATIC_ASSERT(IOS27_21A_ALIGNOF(ios27_21a_rwlock) == 8, "Darwin LP64 alignment");

/* Documented Darwin static-initializer signatures, not runtime signatures. */
#define IOS27_21A_MUTEX_STATIC_SIG UINT64_C(0x32aaaba7)
#define IOS27_21A_MUTEX_ERRORCHECK_SIG UINT64_C(0x32aaaba1)
#define IOS27_21A_MUTEX_RECURSIVE_SIG UINT64_C(0x32aaaba2)
#define IOS27_21A_COND_STATIC_SIG UINT64_C(0x3cb0b1bb)
#define IOS27_21A_RWLOCK_STATIC_SIG UINT64_C(0x2da8b3b4)
#define IOS27_21A_ONCE_STATIC_SIG UINT64_C(0x30b1bcba)
#define IOS27_21A_MAX_ONCE_CALLBACKS 32u
#define IOS27_21A_MAX_BOUND_THREADS 64u
#define IOS27_21A_MAX_KEYS 64u
#define IOS27_21A_MAX_OBJECTS 128u
#define IOS27_21A_MAX_REGIONS 8u
#define IOS27_21A_READ 1u
#define IOS27_21A_WRITE 2u

/* Caller-owned stable regions; backing host ranges must not alias each other. */
typedef struct {
    unsigned char *host;
    size_t size;
    uint64_t guest_base;
    unsigned permissions;
} ios27_21a_region;
typedef struct ios27_21a_runtime ios27_21a_runtime;
/* Trusted native test/integration callback; never receives a raw guest ptr. */
typedef int (*ios27_21a_once_callback)(void *context);

/* Guest addresses are NOT process pointers. Guest arena must remain stable,
 * mapped, and exclusively managed for the lifetime of the bridge. */
int ios27_21a_create(ios27_21a_runtime **out, unsigned char *arena,
                     size_t arena_size, uint64_t guest_base);
int ios27_21a_create_regions(ios27_21a_runtime **out,
                             const ios27_21a_region *regions, size_t count);
int ios27_21a_destroy(ios27_21a_runtime *rt);
int ios27_21a_register_once(ios27_21a_runtime *rt, uint64_t guest_pc,
                            ios27_21a_once_callback callback, void *context);
int ios27_21a_release_once(ios27_21a_runtime *rt, uint64_t once_address);
/* Host-thread binding to an existing, mapped guest pthread_t address.
 * Never cast host pthread_t to guest integer; addresses cannot be reused
 * within this runtime after detachment (stale-pointer fail-closed policy). */
int ios27_21a_attach_thread(ios27_21a_runtime *rt, uint64_t guest_thread_addr);
int ios27_21a_detach_thread(ios27_21a_runtime *rt);
int ios27_21a_validate_access(ios27_21a_runtime *rt, uint64_t guest_addr,
                              size_t span, size_t alignment, unsigned permissions);
int ios27_21a_validate(ios27_21a_runtime *rt, uint64_t guest_addr,
                       size_t span, size_t alignment);

/* Called from trusted native ABI thunk with AArch64 x0..x5 guest-register
 * arguments; scalar return goes in x0. No guest callback execution. */
int ios27_21a_call(ios27_21a_runtime *rt, const char *symbol,
                   const uint64_t args[6], uint64_t *x0);

#if defined(__cplusplus)
} /* extern "C" */
#endif
#undef IOS27_21A_STATIC_ASSERT
#undef IOS27_21A_ALIGNOF
#endif
