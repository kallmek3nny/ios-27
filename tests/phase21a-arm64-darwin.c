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
extern int pthread_attr_init(ios27_21a_attr *);
extern int pthread_attr_setstacksize(ios27_21a_attr *, unsigned long);
extern int pthread_attr_getstacksize(const ios27_21a_attr *, unsigned long *);
extern int pthread_attr_destroy(ios27_21a_attr *);
extern int pthread_mutexattr_init(ios27_21a_mutexattr *);
extern int pthread_mutexattr_settype(ios27_21a_mutexattr *, int);
extern int pthread_mutexattr_destroy(ios27_21a_mutexattr *);
extern int pthread_condattr_init(ios27_21a_condattr *);
extern int pthread_condattr_destroy(ios27_21a_condattr *);
extern int pthread_rwlockattr_init(ios27_21a_rwlockattr *);
extern int pthread_rwlockattr_destroy(ios27_21a_rwlockattr *);
extern int pthread_key_create(ios27_21a_key *, void (*)(void *));
extern int pthread_key_delete(ios27_21a_key);
extern int pthread_setspecific(ios27_21a_key, const void *);
extern void *pthread_getspecific(ios27_21a_key);
extern ios27_21a_thread pthread_self(void);
extern int pthread_equal(ios27_21a_thread, ios27_21a_thread);
extern int pthread_threadid_np(ios27_21a_thread, unsigned long long *);
extern int pthread_main_np(void);
extern int pthread_is_threaded_np(void);
static ios27_21a_mutex guarded;
static ios27_21a_cond notice;
static ios27_21a_rwlock transfer;
static ios27_21a_once once_guard;
static ios27_21a_attr thread_attrs;
static ios27_21a_mutexattr mutex_attrs;
static ios27_21a_condattr cond_attrs;
static ios27_21a_rwlockattr rw_attrs;
static ios27_21a_key tls_key;
static volatile unsigned long total;
static void prepare(void) { total = 19; }
int phase21a_real_compiler_call_graph(unsigned n) {
    unsigned i;
    unsigned long stacksize = 0;
    unsigned long long threadid = 0;
    if (pthread_attr_init(&thread_attrs)) return -10;
    if (pthread_attr_setstacksize(&thread_attrs, 512u * 1024u)) return -11;
    if (pthread_attr_getstacksize(&thread_attrs, &stacksize)) return -12;
    if (pthread_mutexattr_init(&mutex_attrs)) return -13;
    if (pthread_mutexattr_settype(&mutex_attrs, 2)) return -14;
    if (pthread_condattr_init(&cond_attrs)) return -15;
    if (pthread_rwlockattr_init(&rw_attrs)) return -16;
    if (pthread_mutex_init(&guarded, &mutex_attrs)) return -1;
    if (pthread_cond_init(&notice, &cond_attrs)) return -2;
    if (pthread_rwlock_init(&transfer, &rw_attrs)) return -3;
    if (pthread_key_create(&tls_key, 0)) return -17;
    if (pthread_setspecific(tls_key, &guarded)) return -18;
    if (pthread_getspecific(tls_key) != &guarded) return -19;
    if (!pthread_equal(pthread_self(), pthread_self())) return -20;
    if (pthread_threadid_np(pthread_self(), &threadid)) return -21;
    if (!pthread_main_np() && !pthread_is_threaded_np()) return -22;
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
    (void)pthread_key_delete(tls_key);
    (void)pthread_rwlockattr_destroy(&rw_attrs);
    (void)pthread_condattr_destroy(&cond_attrs);
    (void)pthread_mutexattr_destroy(&mutex_attrs);
    (void)pthread_attr_destroy(&thread_attrs);
    return (int)(total + stacksize / (512u * 1024u) - 1u + (threadid & 1u));
}
