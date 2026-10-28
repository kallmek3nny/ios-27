/* Genuine freestanding ARM64 Darwin compiler output using pthread callsites.
 * The pthread object pointers are supplied externally: no guessed Darwin
 * opaque pthread object layout or Apple SDK implementation is assumed.
 */
typedef struct opaque_darwin_thread *darwin_pthread_t;
extern int pthread_create(darwin_pthread_t *thread, const void *attributes,
                          void *(*entry)(void *), void *argument);
extern int pthread_join(darwin_pthread_t thread, void **result);
extern int pthread_mutex_lock(void *opaque_mutex);
extern int pthread_mutex_unlock(void *opaque_mutex);
extern int pthread_cond_wait(void *opaque_condition, void *opaque_mutex);
extern int pthread_cond_signal(void *opaque_condition);
extern int pthread_cond_broadcast(void *opaque_condition);

typedef struct {
    void *mutex;
    void *condition;
    volatile unsigned *counter;
    unsigned repetitions;
} darwin_queue_state;

/* The compiler must emit branch relocations to external pthread symbols. */
static void *darwin_worker(void *argument) {
    darwin_queue_state *state = (darwin_queue_state *)argument;
    unsigned i;
    for (i = 0; i < state->repetitions; ++i) {
        if (pthread_mutex_lock(state->mutex)) return (void *)1;
        ++*state->counter;
        if (pthread_cond_signal(state->condition)) {
            (void)pthread_mutex_unlock(state->mutex);
            return (void *)2;
        }
        if (pthread_mutex_unlock(state->mutex)) return (void *)3;
    }
    return (void *)0;
}
__attribute__((visibility("default")))
int ios27_phase21c_darwin_threaded_calls(darwin_queue_state *state,
                                         unsigned expected) {
    darwin_pthread_t threads[4];
    unsigned i;
    void *result;
    if (!state || !state->mutex || !state->condition || !state->counter)
        return -1;
    for (i = 0; i < 4; ++i) {
        if (pthread_create(&threads[i], (const void *)0,
                           darwin_worker, state))
            return -2;
    }
    if (pthread_mutex_lock(state->mutex)) return -3;
    while (*state->counter != expected) {
        if (pthread_cond_wait(state->condition, state->mutex)) {
            (void)pthread_mutex_unlock(state->mutex);
            return -4;
        }
    }
    if (pthread_cond_broadcast(state->condition)) return -5;
    if (pthread_mutex_unlock(state->mutex)) return -6;
    for (i = 0; i < 4; ++i) {
        if (pthread_join(threads[i], &result) || result) return -7;
    }
    return 0;
}
