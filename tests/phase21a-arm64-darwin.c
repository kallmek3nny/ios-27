/* Compiled as real arm64-apple-darwin Mach-O, with no Apple SDK.
 * This emits actual Darwin pthread symbol callsites, not host Linux calls. */
#include "ios27/libsystem21a_abi.h"
extern int pthread_mutex_init(ios27_21a_mutex *, const ios27_21a_mutexattr *);
extern int pthread_mutex_lock(ios27_21a_mutex *);
extern int pthread_mutex_trylock(ios27_21a_mutex *);
extern int pthread_mutex_unlock(ios27_21a_mutex *);
extern int pthread_mutex_destroy(ios27_21a_mutex *);
extern int pthread_cond_init(ios27_21a_cond *, const ios27_21a_condattr *);
extern int pthread_cond_signal(ios27_21a_cond *);
extern int pthread_cond_broadcast(ios27_21a_cond *);
extern int pthread_cond_destroy(ios27_21a_cond *);
extern int pthread_rwlock_init(ios27_21a_rwlock *, const ios27_21a_rwlockattr *);
extern int pthread_rwlock_rdlock(ios27_21a_rwlock *);
extern int pthread_rwlock_wrlock(ios27_21a_rwlock *);
extern int pthread_rwlock_unlock(ios27_21a_rwlock *);
extern int pthread_rwlock_destroy(ios27_21a_rwlock *);
extern int pthread_once(ios27_21a_once *, void (*)(void));
static ios27_21a_mutex guarded;
static ios27_21a_cond notice;
static ios27_21a_rwlock transfer;
static ios27_21a_once once_guard;
static volatile unsigned long total;
static void prepare(void) { total = 19; }
int phase21a_real_compiler_call_graph(unsigned n) {
    unsigned i;
    if (pthread_mutex_init(&guarded, 0)) return -1;
    if (pthread_cond_init(&notice, 0)) return -2;
    if (pthread_rwlock_init(&transfer, 0)) return -3;
    for (i = 0; i < n; ++i) {
        (void)pthread_once(&once_guard, prepare);
        if (pthread_mutex_lock(&guarded)) return -4;
        if (pthread_rwlock_wrlock(&transfer)) return -5;
        total += i;
        (void)pthread_cond_signal(&notice);
        (void)pthread_rwlock_unlock(&transfer);
        (void)pthread_mutex_unlock(&guarded);
        if (!pthread_rwlock_rdlock(&transfer)) {
            (void)pthread_mutex_trylock(&guarded);
            (void)pthread_rwlock_unlock(&transfer);
        }
    }
    (void)pthread_cond_broadcast(&notice);
    (void)pthread_rwlock_destroy(&transfer);
    (void)pthread_cond_destroy(&notice);
    (void)pthread_mutex_destroy(&guarded);
    return (int)total;
}
