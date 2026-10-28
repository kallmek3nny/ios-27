/* SDK-free Darwin target, with opaque guest object pointers supplied by its
 * caller. Clang creates genuine ARM64 Mach-O external pthread relocations.
 * This does NOT assert Apple pthread storage sizes or initialization bytes. */
extern int pthread_mutex_init(void *, const void *);
extern int pthread_mutex_destroy(void *);
extern int pthread_mutex_lock(void *);
extern int pthread_mutex_trylock(void *);
extern int pthread_mutex_unlock(void *);
extern int pthread_cond_init(void *, const void *);
extern int pthread_cond_destroy(void *);
extern int pthread_cond_wait(void *, void *);
extern int pthread_cond_timedwait(void *, void *, const void *);
extern int pthread_cond_signal(void *);
extern int pthread_cond_broadcast(void *);

__attribute__((visibility("default")))
int phase21i_darwin_calls(void *mutex, void *condition, const void *deadline,
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
