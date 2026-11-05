/* SDK-free, genuine compiler-generated Darwin pthread callsite fixture.
 * These declarations intentionally do NOT claim pthread_mutex_t's layout. */
typedef struct darwin_opaque_mutex darwin_opaque_mutex;
extern int pthread_mutex_init(darwin_opaque_mutex *, const void *);
extern int pthread_mutex_lock(darwin_opaque_mutex *);
extern int pthread_mutex_trylock(darwin_opaque_mutex *);
extern int pthread_mutex_unlock(darwin_opaque_mutex *);
extern int pthread_mutex_destroy(darwin_opaque_mutex *);
extern int pthread_create(void **, const void *, void *(*)(void *), void *);
extern int pthread_join(void *, void **);
struct worker_args { darwin_opaque_mutex *mutex; int *value; };
static void *worker(void *arg) {
    struct worker_args *w = (struct worker_args *)arg;
    int err = pthread_mutex_lock(w->mutex);
    if (err) return (void *)1;
    ++*w->value;
    err = pthread_mutex_unlock(w->mutex);
    return err ? (void *)1 : (void *)0;
}
int darwin_mutex_consumer(darwin_opaque_mutex *mutex, int *value) {
    void *thread = (void *)0, *result = (void *)0;
    struct worker_args args = { mutex, value };
    int err = pthread_mutex_init(mutex, (const void *)0);
    if (err) return err;
    err = pthread_mutex_trylock(mutex);
    if (!err) err = pthread_mutex_unlock(mutex);
    if (!err) err = pthread_create(&thread, (const void *)0, worker, &args);
    if (!err) err = pthread_join(thread, &result);
    if (!err && result) err = 1;
    { int cleanup = pthread_mutex_destroy(mutex); if (!err) err = cleanup; }
    return err;
}
