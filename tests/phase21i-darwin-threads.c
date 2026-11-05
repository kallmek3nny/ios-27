/* Real Clang-produced Darwin ARM64 producer/consumer callsites. This is an
 * MH_OBJECT and cannot run without a compatible Darwin loader/runtime.
 * Opaque LP64 mutex/cond shapes derive from published Apple libpthread types.
 */
typedef struct { long signature; char opaque[56]; } guest_mutex;
typedef struct { long signature; char opaque[40]; } guest_condition;
typedef struct opaque_pthread *guest_thread;
_Static_assert(sizeof(guest_mutex) == 64, "LP64 mutex shape");
_Static_assert(sizeof(guest_condition) == 48, "LP64 cond shape");
extern int pthread_create(guest_thread *, const void *,
                          void *(*)(void *), void *);
extern int pthread_join(guest_thread, void **);
extern int pthread_mutex_lock(guest_mutex *);
extern int pthread_mutex_unlock(guest_mutex *);
extern int pthread_cond_wait(guest_condition *, guest_mutex *);
extern int pthread_cond_signal(guest_condition *);
extern int pthread_cond_broadcast(guest_condition *);

enum { QUEUE_CAPACITY = 7, ITERATIONS = 512 };
typedef struct {
    guest_mutex mutex;
    guest_condition nonempty, nonfull;
    unsigned values[QUEUE_CAPACITY];
    unsigned next, first, count, total, completed;
    unsigned produced, consumed;
} guest_queue;
typedef struct { guest_queue *q; unsigned id; } producer_arg;

static void *produce(void *context) {
    producer_arg *arg = context;
    guest_queue *q = arg->q;
    for (unsigned i = 0; i < ITERATIONS; ++i) {
        if (pthread_mutex_lock(&q->mutex)) return (void *)1;
        while (q->count == QUEUE_CAPACITY)
            if (pthread_cond_wait(&q->nonfull, &q->mutex)) return (void *)2;
        q->values[q->next] = arg->id * ITERATIONS + i + 1u;
        q->next = (q->next + 1u) % QUEUE_CAPACITY;
        ++q->count;
        ++q->produced;
        if (pthread_cond_signal(&q->nonempty)) return (void *)3;
        if (pthread_mutex_unlock(&q->mutex)) return (void *)4;
    }
    if (pthread_mutex_lock(&q->mutex)) return (void *)5;
    ++q->completed;
    if (pthread_cond_broadcast(&q->nonempty)) return (void *)6;
    if (pthread_mutex_unlock(&q->mutex)) return (void *)7;
    return (void *)0;
}

static void *consume(void *context) {
    guest_queue *q = context;
    for (;;) {
        if (pthread_mutex_lock(&q->mutex)) return (void *)1;
        while (!q->count && q->completed != 2u)
            if (pthread_cond_wait(&q->nonempty, &q->mutex)) return (void *)2;
        if (!q->count && q->completed == 2u) {
            if (pthread_mutex_unlock(&q->mutex)) return (void *)3;
            return (void *)0;
        }
        q->total += q->values[q->first];
        q->first = (q->first + 1u) % QUEUE_CAPACITY;
        --q->count;
        ++q->consumed;
        if (pthread_cond_signal(&q->nonfull)) return (void *)4;
        if (pthread_mutex_unlock(&q->mutex)) return (void *)5;
    }
}
__attribute__((visibility("default")))
int phase21i_darwin_compiled_queue(guest_queue *queue) {
    guest_thread threads[4];
    producer_arg producers[2];
    void *result = (void *)0;
    if (!queue) return -1;
    for (unsigned i = 0; i < 2; ++i) {
        producers[i] = (producer_arg){queue, i};
        if (pthread_create(&threads[i], (void *)0, produce, &producers[i]))
            return -2;
    }
    for (unsigned i = 2; i < 4; ++i)
        if (pthread_create(&threads[i], (void *)0, consume, queue))
            return -3;
    for (unsigned i = 0; i < 4; ++i)
        if (pthread_join(threads[i], &result) || result) return -4;
    if (queue->produced != 2u * ITERATIONS ||
        queue->consumed != 2u * ITERATIONS || queue->count)
        return -5;
    return (int)queue->total;
}
