#ifndef IOS27_OBJC20C_LIFETIME_H
#define IOS27_OBJC20C_LIFETIME_H

#include <stddef.h>
#include <stdint.h>

/* Standalone, bounded single-threaded manual-reference-counting substrate.
 * All operations return 0 on success, -1 for invalid/stale/exhausted state.
 * Objects are non-moving and tombstoned until runtime destruction: no ABA
 * pointer reuse inside a runtime. This API is NOT Apple ARC or libobjc. */
#define IOS27_OBJC20C_MAX_OBJECTS 32768u
#define IOS27_OBJC20C_MAX_LIVE 4096u
#define IOS27_OBJC20C_MAX_BYTES (8u * 1024u * 1024u)
#define IOS27_OBJC20C_MAX_OBJECT_BYTES 4096u
#define IOS27_OBJC20C_MAX_WEAK 8192u
#define IOS27_OBJC20C_MAX_POOL_DEPTH 128u
#define IOS27_OBJC20C_MAX_AUTORELEASES 32768u

typedef struct ios27_objc20c_runtime ios27_objc20c_runtime;
typedef void (*ios27_objc20c_destructor)(ios27_objc20c_runtime *, void *, void *);

typedef struct {
    size_t live_objects;
    size_t live_bytes;
    size_t lifetime_objects;
    size_t lifetime_bytes;
    size_t weak_slots;
    size_t pool_depth;
    size_t pending_autoreleases;
    size_t destroyed_objects;
} ios27_objc20c_stats;

int ios27_objc20c_create(ios27_objc20c_runtime **out);
/* Shutdown reclaims leaked/cyclic payloads without invoking destructors.
 * Release managed ownership normally to run destructor callbacks. */
void ios27_objc20c_destroy(ios27_objc20c_runtime *rt);
/* Allocates zero-filled, pointer-aligned bytes, initialized at +1 retain. */
int ios27_objc20c_alloc(ios27_objc20c_runtime *rt, size_t bytes,
                       ios27_objc20c_destructor destructor, void *context,
                       void **out);
int ios27_objc20c_retain(ios27_objc20c_runtime *rt, const void *object);
/* Last release zeroes weak references, then runs destructor once. */
int ios27_objc20c_release(ios27_objc20c_runtime *rt, const void *object);
int ios27_objc20c_retain_count(const ios27_objc20c_runtime *rt,
                              const void *object, uint32_t *count);

/* Strong-store transaction: retain next before replacing/releasing old. */
int ios27_objc20c_strong_store(ios27_objc20c_runtime *rt, void **location,
                              void *next);
/* Weak slots must remain address-stable and be unregistered before expiry. */
int ios27_objc20c_weak_store(ios27_objc20c_runtime *rt, void **location,
                            void *next);
int ios27_objc20c_weak_unregister(ios27_objc20c_runtime *rt, void **location);
int ios27_objc20c_weak_load_retained(ios27_objc20c_runtime *rt,
                                    void **location, void **out);

/* Only the top token may pop. A drain consumes all queued releases even
 * if a stale entry is encountered, returning -1 in that case. */
int ios27_objc20c_pool_push(ios27_objc20c_runtime *rt, uint64_t *token);
int ios27_objc20c_autorelease(ios27_objc20c_runtime *rt,
                             const void *object);
int ios27_objc20c_pool_pop(ios27_objc20c_runtime *rt, uint64_t token);
void ios27_objc20c_get_stats(const ios27_objc20c_runtime *rt,
                            ios27_objc20c_stats *out);
int ios27_objc20c_validate(const ios27_objc20c_runtime *rt);

#endif
