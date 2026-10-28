/* SDK-free ARM64 Darwin Mach-O fixture. Constants come from Apple's public
 * libpthread include/pthread/pthread_impl.h, not Linux pthread types. */
typedef struct { long signature; unsigned char opaque[56]; } darwin_mutex;
typedef struct { long signature; unsigned char opaque[40]; } darwin_condition;
_Static_assert(sizeof(darwin_mutex) == 64, "Apple LP64 mutex shape");
_Static_assert(sizeof(darwin_condition) == 48, "Apple LP64 cond shape");
extern int pthread_mutex_lock(darwin_mutex *);
extern int pthread_mutex_trylock(darwin_mutex *);
extern int pthread_mutex_unlock(darwin_mutex *);
extern int pthread_mutex_destroy(darwin_mutex *);
extern int pthread_cond_wait(darwin_condition *, darwin_mutex *);
extern int pthread_cond_signal(darwin_condition *);
extern int pthread_cond_broadcast(darwin_condition *);
extern int pthread_cond_destroy(darwin_condition *);
__attribute__((used,visibility("default")))
darwin_mutex phase21i_static_normal = {0x32aaaba7L, {0}};
__attribute__((used,visibility("default")))
darwin_mutex phase21i_static_recursive = {0x32aaaba2L, {0}};
__attribute__((used,visibility("default")))
darwin_mutex phase21i_static_errorcheck = {0x32aaaba1L, {0}};
__attribute__((used,visibility("default")))
darwin_mutex phase21i_static_firstfit = {0x32aaaba3L, {0}};
__attribute__((used,visibility("default")))
darwin_condition phase21i_static_condition = {0x3cb0b1bbL, {0}};

__attribute__((visibility("default")))
int phase21i_darwin_static_calls(volatile int *ready) {
    int rc = pthread_mutex_lock(&phase21i_static_normal);
    if (rc) return rc;
    rc = pthread_mutex_trylock(&phase21i_static_recursive);
    if (rc) goto unlock;
    if (pthread_mutex_unlock(&phase21i_static_recursive)) return -1;
    rc = pthread_mutex_lock(&phase21i_static_errorcheck);
    if (rc) goto unlock;
    if (pthread_mutex_unlock(&phase21i_static_errorcheck)) return -2;
    while (!*ready && !rc)
        rc = pthread_cond_wait(&phase21i_static_condition,
                               &phase21i_static_normal);
    if (!rc) rc = pthread_cond_signal(&phase21i_static_condition);
    if (!rc) rc = pthread_cond_broadcast(&phase21i_static_condition);
unlock:
    if (pthread_mutex_unlock(&phase21i_static_normal)) return -3;
    if (pthread_cond_destroy(&phase21i_static_condition)) return -4;
    if (pthread_mutex_destroy(&phase21i_static_normal)) return -5;
    return rc;
}
