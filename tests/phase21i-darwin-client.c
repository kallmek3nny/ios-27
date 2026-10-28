/* SDK-free Darwin target, with opaque guest object pointers supplied by its
 * caller. Clang creates genuine ARM64 Mach-O external pthread relocations.
 * This does NOT assert Apple pthread storage sizes or initialization bytes. */
/* This test profile is derived from Apple's published libpthread LP64
 * _pthread_types.h (opaque arrays: mutex=56, condition=40, preceded by
 * an 8-byte signature). It is NOT a claim about an unreleased iOS SDK.
 * https://github.com/apple-oss-distributions/libpthread/blob/main/include/sys/_pthread/_pthread_types.h
 */
typedef struct { long signature; char opaque[56]; } phase21i_lp64_mutex;
typedef struct { long signature; char opaque[40]; } phase21i_lp64_cond;
typedef struct { long seconds; long nanoseconds; } phase21i_lp64_timespec;
_Static_assert(sizeof(long) == 8, "LP64 target required");
_Static_assert(sizeof(phase21i_lp64_mutex) == 64, "Apple LP64 mutex extent");
_Static_assert(sizeof(phase21i_lp64_cond) == 48, "Apple LP64 cond extent");
_Static_assert(_Alignof(phase21i_lp64_mutex) == 8, "mutex alignment");
_Static_assert(_Alignof(phase21i_lp64_cond) == 8, "cond alignment");
_Static_assert(sizeof(phase21i_lp64_timespec) == 16, "LP64 deadline");
/* Type + bitfield declarations reproduce the published LP64 source *shape*
 * only, not an Apple SDK import or a claim about iOS 27 internal semantics. */
typedef struct {
    long signature;
    int priority_ceiling;
    unsigned int protocol:2, type:2, pshared:2, opt:3, unused:23;
} phase21i_lp64_mutexattr;
typedef struct { long signature; unsigned int pshared:2, other:30; }
    phase21i_lp64_condattr;
_Static_assert(sizeof(int) == 4, "arm64 Darwin int width");
_Static_assert(sizeof(phase21i_lp64_mutexattr) == 16, "LP64 mutexattr size");
_Static_assert(sizeof(phase21i_lp64_condattr) == 16, "LP64 condattr size");
_Static_assert(_Alignof(phase21i_lp64_mutexattr) == 8, "LP64 attr alignment");
_Static_assert(__builtin_offsetof(phase21i_lp64_mutexattr, priority_ceiling) == 8,
               "Apple-published mutex priority field location");
__attribute__((visibility("default")))
int phase21i_published_apple_attr_fields(const phase21i_lp64_mutexattr *mutexattr,
                                         const phase21i_lp64_condattr *condattr) {
    if (!mutexattr || !condattr) return -1;
    return mutexattr->protocol == 0 && mutexattr->type <= 2 &&
           mutexattr->pshared == 2 && mutexattr->opt >= 1 &&
           mutexattr->opt <= 2 && condattr->pshared == 2;
}
extern int pthread_mutex_init(phase21i_lp64_mutex *, const void *);
extern int pthread_mutex_destroy(phase21i_lp64_mutex *);
extern int pthread_mutex_lock(phase21i_lp64_mutex *);
extern int pthread_mutex_trylock(phase21i_lp64_mutex *);
extern int pthread_mutex_unlock(phase21i_lp64_mutex *);
extern int pthread_cond_init(phase21i_lp64_cond *, const void *);
extern int pthread_cond_destroy(phase21i_lp64_cond *);
extern int pthread_cond_wait(phase21i_lp64_cond *, phase21i_lp64_mutex *);
extern int pthread_cond_timedwait(phase21i_lp64_cond *, phase21i_lp64_mutex *, const phase21i_lp64_timespec *);
extern int pthread_cond_signal(phase21i_lp64_cond *);
extern int pthread_cond_broadcast(phase21i_lp64_cond *);

__attribute__((visibility("default")))
int phase21i_darwin_calls(phase21i_lp64_mutex *mutex, phase21i_lp64_cond *condition,
                         const phase21i_lp64_timespec *deadline,
                         volatile int *ready) {
    int rc = pthread_mutex_init(mutex, (void *)0);
    if (rc) return rc;
    rc = pthread_cond_init(condition, (void *)0);
    if (rc) { (void)pthread_mutex_destroy(mutex); return rc; }
    rc = pthread_mutex_trylock(mutex);
    if (rc) rc = pthread_mutex_lock(mutex);
    if (!rc) {
        while (!*ready && !rc)
            rc = pthread_cond_timedwait(condition, mutex, deadline);
        if (!rc) rc = pthread_cond_signal(condition);
        if (!rc) rc = pthread_cond_broadcast(condition);
        if (!rc) rc = pthread_cond_wait(condition, mutex);
        if (pthread_mutex_unlock(mutex)) return -1;
    }
    if (pthread_cond_destroy(condition)) return -2;
    if (pthread_mutex_destroy(mutex)) return -3;
    return rc;
}
