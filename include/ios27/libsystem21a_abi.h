#ifndef IOS27_LIBSYSTEM21A_ABI_H
#define IOS27_LIBSYSTEM21A_ABI_H
#include <stddef.h>
#include <stdint.h>

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

_Static_assert(sizeof(ios27_21a_mutex) == 64, "Darwin mutex size");
_Static_assert(sizeof(ios27_21a_cond) == 48, "Darwin cond size");
_Static_assert(sizeof(ios27_21a_rwlock) == 200, "Darwin rwlock size");
_Static_assert(sizeof(ios27_21a_once) == 16, "Darwin once size");
_Static_assert(sizeof(ios27_21a_attr) == 64, "Darwin attr size");
_Static_assert(sizeof(ios27_21a_mutexattr) == 16, "Darwin mutexattr size");
_Static_assert(sizeof(ios27_21a_condattr) == 16, "Darwin condattr size");
_Static_assert(sizeof(ios27_21a_rwlockattr) == 24, "Darwin rwlockattr size");
_Static_assert(_Alignof(ios27_21a_rwlock) == 8, "Darwin LP64 alignment");

/* Documented Darwin static-initializer signatures, not runtime signatures. */
#define IOS27_21A_MUTEX_STATIC_SIG UINT64_C(0x32aaaba7)
#define IOS27_21A_MUTEX_ERRORCHECK_SIG UINT64_C(0x32aaaba1)
#define IOS27_21A_MUTEX_RECURSIVE_SIG UINT64_C(0x32aaaba2)
#define IOS27_21A_COND_STATIC_SIG UINT64_C(0x3cb0b1bb)
#define IOS27_21A_RWLOCK_STATIC_SIG UINT64_C(0x2da8b3b4)
#define IOS27_21A_MAX_OBJECTS 128u

typedef struct ios27_21a_runtime ios27_21a_runtime;

/* Guest addresses are NOT process pointers. Guest arena must remain stable,
 * mapped, and exclusively managed for the lifetime of the bridge. */
int ios27_21a_create(ios27_21a_runtime **out, unsigned char *arena,
                     size_t arena_size, uint64_t guest_base);
int ios27_21a_destroy(ios27_21a_runtime *rt);
int ios27_21a_validate(ios27_21a_runtime *rt, uint64_t guest_addr,
                       size_t span, size_t alignment);

/* Called from trusted native ABI thunk with AArch64 x0..x5 guest-register
 * arguments; scalar return goes in x0. No guest callback execution. */
int ios27_21a_call(ios27_21a_runtime *rt, const char *symbol,
                   const uint64_t args[6], uint64_t *x0);
#endif
