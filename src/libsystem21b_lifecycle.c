#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21b_lifecycle.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum lifecycle { UNUSED, JOINABLE, JOINING, DETACHED, REAPING, RETIRED };
typedef struct {
    pthread_t native;
    uint32_t generation;
    enum lifecycle state;
    int finished;
} worker_slot;
struct ios27_21b_runtime {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    pthread_t reaper;
    size_t quota, live;
    uint64_t owner;
    int shutdown;
    worker_slot slots[IOS27_21B_MAX_THREADS];
};
typedef struct {
    ios27_21b_runtime *rt;
    ios27_21b_thread id;
    ios27_21b_entry entry;
    void *argument;
    void *result;
} worker_args;
static _Atomic uint64_t next_owner = 1;
static _Thread_local ios27_21b_runtime *self_runtime;
static _Thread_local ios27_21b_thread self_id;

static worker_slot *lookup(ios27_21b_runtime *rt, ios27_21b_thread id) {
    worker_slot *s;
    if (!id.slot || !id.generation || id.owner != rt->owner ||
        id.slot > rt->quota) return NULL;
    s = &rt->slots[id.slot - 1u];
    if (s->state == UNUSED || s->state == RETIRED || s->generation != id.generation)
        return NULL;
    return s;
}
static void retire(ios27_21b_runtime *rt, worker_slot *s) {
    if (s->generation == UINT32_MAX) s->state = RETIRED;
    else { ++s->generation; s->state = UNUSED; }
    s->finished = 0;
    --rt->live;
    (void)pthread_cond_broadcast(&rt->changed);
}

int ios27_21b_attr_init(ios27_21b_attrs *a) {
    if (!a) return EINVAL;
    *a = (ios27_21b_attrs){0};
    a->version = IOS27_21B_ATTR_VERSION;
    return 0;
}
int ios27_21b_attr_validate(const ios27_21b_attrs *a) {
    long page;
    uint64_t minimum;
    if (!a || a->version != IOS27_21B_ATTR_VERSION || a->detached > 1u)
        return EINVAL;
    if (a->stack_address || a->scheduling || a->priority) return ENOTSUP;
    page = sysconf(_SC_PAGESIZE);
    if (page <= 0) return ENOTSUP;
    minimum = (uint64_t)page * 4u;
#ifdef PTHREAD_STACK_MIN
    if (minimum < (uint64_t)PTHREAD_STACK_MIN) minimum = PTHREAD_STACK_MIN;
#endif
    if (a->stack_bytes && (a->stack_bytes < minimum ||
        a->stack_bytes > IOS27_21B_STACK_MAX ||
        a->stack_bytes % (uint64_t)page != 0u ||
        a->stack_bytes > (uint64_t)SIZE_MAX)) return EINVAL;
    if (a->guard_bytes && (a->guard_bytes % (uint64_t)page != 0u ||
        a->guard_bytes > IOS27_21B_STACK_MAX / 2u ||
        a->guard_bytes > (uint64_t)SIZE_MAX)) return EINVAL;
    if (a->stack_bytes && a->guard_bytes > a->stack_bytes / 2u)
        return EINVAL;
    return 0;
}
static void completed(void *context) {
    worker_args *a = context;
    ios27_21b_runtime *rt = a->rt;
    (void)pthread_mutex_lock(&rt->lock);
    worker_slot *slot = lookup(rt, a->id);
    if (slot) slot->finished = 1;
    (void)pthread_cond_broadcast(&rt->changed);
    (void)pthread_mutex_unlock(&rt->lock);
    self_runtime = NULL;
    self_id = (ios27_21b_thread){0};
    free(a);
}
static void *worker(void *context) {
    worker_args *a = context;
    void *result = NULL;
    self_runtime = a->rt;
    self_id = a->id;
    /* Wait for pthread_create to publish native identity under the guard. */
    (void)pthread_mutex_lock(&a->rt->lock);
    (void)pthread_mutex_unlock(&a->rt->lock);
    pthread_cleanup_push(completed, a);
    result = a->entry(a->argument);
    pthread_cleanup_pop(1);
    return result;
}
static void *reaper_main(void *context) {
    ios27_21b_runtime *rt = context;
    (void)pthread_mutex_lock(&rt->lock);
    for (;;) {
        size_t i;
        pthread_t target;
        for (i = 0; i < rt->quota; ++i)
            if (rt->slots[i].state == DETACHED) break;
        if (i == rt->quota) {
            if (rt->shutdown) break;
            (void)pthread_cond_wait(&rt->changed, &rt->lock);
            continue;
        }
        rt->slots[i].state = REAPING;
        target = rt->slots[i].native;
        (void)pthread_mutex_unlock(&rt->lock);
        /* Never pthread_detach: reaper joins even detached guest workers,
           so host TLS teardown finishes before we release the slot. */
        int err = pthread_join(target, NULL);
        (void)pthread_mutex_lock(&rt->lock);
        if (!err) retire(rt, &rt->slots[i]);
        else {
            rt->slots[i].state = RETIRED;
            (void)pthread_cond_broadcast(&rt->changed);
        }
    }
    (void)pthread_mutex_unlock(&rt->lock);
    return NULL;
}
int ios27_21b_runtime_create(ios27_21b_runtime **out, size_t quota) {
    ios27_21b_runtime *rt;
    int err;
    if (!out || !quota || quota > IOS27_21B_MAX_THREADS) return EINVAL;
    *out = NULL;
    rt = calloc(1, sizeof(*rt));
    if (!rt) return ENOMEM;
    rt->quota = quota;
    uint64_t current = atomic_load_explicit(&next_owner, memory_order_relaxed);
    do {
        if (current == UINT64_MAX) { free(rt); return EAGAIN; }
    } while (!atomic_compare_exchange_weak_explicit(&next_owner, &current,
        current + 1u, memory_order_relaxed, memory_order_relaxed));
    rt->owner = current;
    err = pthread_mutex_init(&rt->lock, NULL);
    if (err) { free(rt); return err; }
    err = pthread_cond_init(&rt->changed, NULL);
    if (err) { (void)pthread_mutex_destroy(&rt->lock); free(rt); return err; }
    err = pthread_create(&rt->reaper, NULL, reaper_main, rt);
    if (err) {
        (void)pthread_cond_destroy(&rt->changed);
        (void)pthread_mutex_destroy(&rt->lock);
        free(rt);
        return err;
    }
    *out = rt;
    return 0;
}
int ios27_21b_runtime_destroy(ios27_21b_runtime *rt) {
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->lock);
    if (rt->live) { (void)pthread_mutex_unlock(&rt->lock); return EBUSY; }
    rt->shutdown = 1;
    (void)pthread_cond_signal(&rt->changed);
    (void)pthread_mutex_unlock(&rt->lock);
    err = pthread_join(rt->reaper, NULL);
    if (err) return err;
    (void)pthread_cond_destroy(&rt->changed);
    (void)pthread_mutex_destroy(&rt->lock);
    free(rt);
    return 0;
}
int ios27_21b_thread_create(ios27_21b_runtime *rt, ios27_21b_thread *out,
                             const ios27_21b_attrs *attr,
                             ios27_21b_entry entry, void *argument) {
    ios27_21b_attrs defaults;
    pthread_attr_t native;
    worker_args *args;
    worker_slot *s;
    ios27_21b_thread id;
    size_t i;
    int err;
    if (!rt || !out || !entry) return EINVAL;
    *out = (ios27_21b_thread){0};
    if (!attr) { (void)ios27_21b_attr_init(&defaults); attr = &defaults; }
    err = ios27_21b_attr_validate(attr);
    if (err) return err;
    args = malloc(sizeof(*args));
    if (!args) return ENOMEM;
    err = pthread_attr_init(&native);
    if (err) { free(args); return err; }
    if (attr->stack_bytes) err = pthread_attr_setstacksize(&native, (size_t)attr->stack_bytes);
    if (!err && attr->guard_bytes)
        err = pthread_attr_setguardsize(&native, (size_t)attr->guard_bytes);
    if (err) { (void)pthread_attr_destroy(&native); free(args); return err; }
    (void)pthread_mutex_lock(&rt->lock);
    for (i = 0; i < rt->quota; ++i)
        if (rt->slots[i].state == UNUSED) break;
    if (i == rt->quota || rt->shutdown) {
        (void)pthread_mutex_unlock(&rt->lock);
        (void)pthread_attr_destroy(&native);
        free(args);
        return EAGAIN;
    }
    s = &rt->slots[i];
    if (!s->generation) s->generation = 1u;
    s->state = attr->detached ? DETACHED : JOINABLE;
    s->finished = 0;
    args->rt = rt;
    id = (ios27_21b_thread){(uint32_t)i + 1u, s->generation, rt->owner};
    args->id = id;
    args->entry = entry;
    args->argument = argument;
    args->result = NULL;
    ++rt->live;
    err = pthread_create(&s->native, &native, worker, args);
    if (err) retire(rt, s);
    else {
        *out = id;
        (void)pthread_cond_signal(&rt->changed);
    }
    (void)pthread_mutex_unlock(&rt->lock);
    (void)pthread_attr_destroy(&native);
    if (err) free(args);
    return err;
}
int ios27_21b_thread_join(ios27_21b_runtime *rt, ios27_21b_thread id,
                           void **result) {
    worker_slot *s;
    pthread_t native;
    void *value = NULL;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->lock);
    s = lookup(rt, id);
    if (!s) { (void)pthread_mutex_unlock(&rt->lock); return ESRCH; }
    if (s->state != JOINABLE) {
        (void)pthread_mutex_unlock(&rt->lock); return EINVAL;
    }
    native = s->native;
    if (pthread_equal(native, pthread_self())) {
        (void)pthread_mutex_unlock(&rt->lock); return EDEADLK;
    }
    s->state = JOINING;
    (void)pthread_mutex_unlock(&rt->lock);
    err = pthread_join(native, &value);
    (void)pthread_mutex_lock(&rt->lock);
    s = lookup(rt, id);
    if (s) {
        if (!err) retire(rt, s);
        else s->state = JOINABLE;
    }
    (void)pthread_mutex_unlock(&rt->lock);
    if (!err && result) *result = value;
    return err;
}
int ios27_21b_thread_detach(ios27_21b_runtime *rt, ios27_21b_thread id) {
    worker_slot *s;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->lock);
    s = lookup(rt, id);
    if (!s) { (void)pthread_mutex_unlock(&rt->lock); return ESRCH; }
    if (s->state != JOINABLE) {
        (void)pthread_mutex_unlock(&rt->lock); return EINVAL;
    }
    s->state = DETACHED;
    (void)pthread_cond_signal(&rt->changed);
    (void)pthread_mutex_unlock(&rt->lock);
    return 0;
}
int ios27_21b_thread_info(ios27_21b_runtime *rt, ios27_21b_thread id,
                           ios27_21b_info *out) {
    worker_slot *s;
    if (!rt || !out) return EINVAL;
    (void)pthread_mutex_lock(&rt->lock);
    s = lookup(rt, id);
    if (s) *out = (ios27_21b_info){!s->finished,
        s->state == DETACHED || s->state == REAPING, s->state == JOINING};
    (void)pthread_mutex_unlock(&rt->lock);
    return s ? 0 : ESRCH;
}
ios27_21b_thread ios27_21b_thread_self(ios27_21b_runtime *rt) {
    if (rt && self_runtime == rt) return self_id;
    return (ios27_21b_thread){0};
}
int ios27_21b_thread_equal(ios27_21b_thread a, ios27_21b_thread b) {
    return a.slot && b.slot && a.slot == b.slot &&
        a.generation && a.generation == b.generation &&
        a.owner && a.owner == b.owner;
}
int ios27_21b_runtime_usage(ios27_21b_runtime *rt, size_t *out) {
    if (!rt || !out) return EINVAL;
    (void)pthread_mutex_lock(&rt->lock);
    *out = rt->live;
    (void)pthread_mutex_unlock(&rt->lock);
    return 0;
}
int ios27_21b_runtime_wait_idle(ios27_21b_runtime *rt, uint64_t timeout_ms) {
    struct timespec deadline;
    uint64_t seconds, nanos;
    int err = 0;
    if (!rt) return EINVAL;
    if (clock_gettime(CLOCK_REALTIME, &deadline)) return errno;
    seconds = timeout_ms / 1000u;
    nanos = (timeout_ms % 1000u) * UINT64_C(1000000) + (uint64_t)deadline.tv_nsec;
    seconds += nanos / UINT64_C(1000000000);
    if (seconds > (uint64_t)INT64_MAX - (uint64_t)deadline.tv_sec) return EINVAL;
    deadline.tv_sec += (time_t)seconds;
    deadline.tv_nsec = (long)(nanos % UINT64_C(1000000000));
    (void)pthread_mutex_lock(&rt->lock);
    while (rt->live && !err) {
        if (!timeout_ms) err = ETIMEDOUT;
        else err = pthread_cond_timedwait(&rt->changed, &rt->lock, &deadline);
    }
    (void)pthread_mutex_unlock(&rt->lock);
    return err;
}
