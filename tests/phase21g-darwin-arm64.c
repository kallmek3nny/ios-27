/* SDK-free genuine Darwin pthread callsite fixture.
 * The host model uses distinct ios27_21g_* names and is NOT a Darwin ABI. */
typedef unsigned long darwin_pthread_t;
typedef struct { long opaque[8]; } darwin_pthread_mutex_t;
typedef struct { long opaque[6]; } darwin_pthread_cond_t;
struct darwin_timespec { long tv_sec, tv_nsec; };
extern int pthread_mutex_lock(darwin_pthread_mutex_t *);
extern int pthread_mutex_unlock(darwin_pthread_mutex_t *);
extern int pthread_cond_wait(darwin_pthread_cond_t *, darwin_pthread_mutex_t *);
extern int pthread_cond_timedwait(darwin_pthread_cond_t *,
                                  darwin_pthread_mutex_t *,
                                  const struct darwin_timespec *);
extern int pthread_cond_signal(darwin_pthread_cond_t *);
extern int pthread_cond_broadcast(darwin_pthread_cond_t *);
extern int pthread_create(darwin_pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_join(darwin_pthread_t, void **);
static darwin_pthread_mutex_t mutex;
static darwin_pthread_cond_t ready, free_space;
static volatile int pending, finished;
static void *worker(void *unused) {
    (void)unused;
    for (int i = 0; i < 100; ++i) {
        (void)pthread_mutex_lock(&mutex);
        while (!pending && !finished)
            (void)pthread_cond_wait(&ready, &mutex);
        if (finished) { (void)pthread_mutex_unlock(&mutex); return 0; }
        --pending;
        (void)pthread_cond_signal(&free_space);
        (void)pthread_mutex_unlock(&mutex);
    }
    return 0;
}
int compiled_darwin_producer_consumer(void) {
    darwin_pthread_t thread;
    (void)pthread_create(&thread, 0, worker, 0);
    for (int i = 0; i < 100; ++i) {
        (void)pthread_mutex_lock(&mutex);
        while (pending > 5) {
            struct darwin_timespec deadline = {500, 0};
            (void)pthread_cond_timedwait(&free_space, &mutex, &deadline);
        }
        ++pending;
        (void)pthread_cond_signal(&ready);
        (void)pthread_mutex_unlock(&mutex);
    }
    (void)pthread_mutex_lock(&mutex);
    finished = 1;
    (void)pthread_cond_broadcast(&ready);
    (void)pthread_mutex_unlock(&mutex);
    (void)pthread_join(thread, 0);
    return pending;
}
