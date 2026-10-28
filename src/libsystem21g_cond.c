#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21g_cond.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#define NSEC 1000000000L

typedef struct waiter waiter;
struct waiter {
    waiter *previous, *next;
    pthread_cond_t cv;
    int queued, notified;
};
typedef struct {
    enum ios27_21g_kind kind;
    uint32_t generation;
    int retired, locked;
    pthread_t owner;
    pthread_cond_t gate;
    size_t references, queued;
    ios27_21g_handle binding;
    waiter *head, *tail;
    uint64_t wakeups, timeouts;
} slot;
struct ios27_21g_runtime {
    pthread_mutex_t guard;
    size_t capacity;
    slot objects[IOS27_21G_MAX_OBJECTS];
};

__attribute__((noinline))
static slot *lookup(ios27_21g_runtime *rt, ios27_21g_handle handle,
                    enum ios27_21g_kind expected) {
    slot *s;
    if (!handle.slot || handle.slot > rt->capacity || !handle.generation)
        return NULL;
    s = &rt->objects[handle.slot - 1u];
    return s->kind == expected && s->generation == handle.generation ? s : NULL;
}
static int same_handle(ios27_21g_handle a, ios27_21g_handle b) {
    return a.slot == b.slot && a.generation == b.generation;
}
static void enqueue(slot *s, waiter *w) {
    w->previous = s->tail;
    w->next = NULL;
    if (s->tail) s->tail->next = w;
    else s->head = w;
    s->tail = w;
    w->queued = 1;
    ++s->queued;
}
static void dequeue(slot *s, waiter *w) {
    if (!w->queued) return;
    if (w->previous) w->previous->next = w->next;
    else s->head = w->next;
    if (w->next) w->next->previous = w->previous;
    else s->tail = w->previous;
    w->queued = 0;
    --s->queued;
}
/* Both selection and wakeup occur while holding the runtime guard. */
static void notify_one(slot *s) {
    waiter *w = s->head;
    if (!w) return;
    dequeue(s, w);
    w->notified = 1;
    ++s->wakeups;
    (void)pthread_cond_signal(&w->cv);
}
static int acquire(ios27_21g_runtime *rt, slot *s) {
    int error;
    if (s->locked && pthread_equal(s->owner, pthread_self())) return EDEADLK;
    if (s->locked) {
        /* Keep destroy from tearing down a gate with blocked acquirers. */
        ++s->references;
        while (s->locked) {
            error = pthread_cond_wait(&s->gate, &rt->guard);
            if (error) { --s->references; return error; }
        }
        --s->references;
    }
    s->locked = 1;
    s->owner = pthread_self();
    return 0;
}
static void unlock_logical(slot *s) {
    s->locked = 0;
    (void)pthread_cond_signal(&s->gate);
}

int ios27_21g_runtime_create(ios27_21g_runtime **out, size_t capacity) {
    ios27_21g_runtime *rt;
    int error;
    if (!out || !capacity || capacity > IOS27_21G_MAX_OBJECTS) return EINVAL;
    *out = NULL;
    rt = calloc(1u, sizeof(*rt));
    if (!rt) return ENOMEM;
    error = pthread_mutex_init(&rt->guard, NULL);
    if (error) { free(rt); return error; }
    rt->capacity = capacity;
    *out = rt;
    return 0;
}
int ios27_21g_runtime_destroy(ios27_21g_runtime *rt) {
    size_t i;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i) {
        if (rt->objects[i].kind) {
            (void)pthread_mutex_unlock(&rt->guard);
            return EBUSY;
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    (void)pthread_mutex_destroy(&rt->guard);
    free(rt);
    return 0;
}
int ios27_21g_create(ios27_21g_runtime *rt, enum ios27_21g_kind kind,
                     ios27_21g_handle *out) {
    slot *s;
    size_t i;
    int error;
    if (!rt || !out || (kind != IOS27_21G_MUTEX && kind != IOS27_21G_COND))
        return EINVAL;
    *out = (ios27_21g_handle){0, 0};
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i)
        if (!rt->objects[i].kind && !rt->objects[i].retired) break;
    if (i == rt->capacity) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EAGAIN;
    }
    s = &rt->objects[i];
    error = pthread_cond_init(&s->gate, NULL);
    if (!error) {
        if (!s->generation) s->generation = 1u;
        s->kind = kind;
        s->locked = 0;
        s->references = s->queued = 0;
        s->head = s->tail = NULL;
        s->wakeups = s->timeouts = 0;
        s->binding = (ios27_21g_handle){0, 0};
        *out = (ios27_21g_handle){(uint32_t)(i + 1u), s->generation};
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return error;
}
int ios27_21g_destroy(ios27_21g_runtime *rt, ios27_21g_handle handle) {
    slot *s;
    int error;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, handle, IOS27_21G_MUTEX);
    if (!s) s = lookup(rt, handle, IOS27_21G_COND);
    if (!s) error = EINVAL;
    else if (s->locked || s->references || s->queued) error = EBUSY;
    else {
        error = pthread_cond_destroy(&s->gate);
        if (!error) {
            s->kind = 0;
            if (s->generation == UINT32_MAX) s->retired = 1;
            else ++s->generation;
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return error;
}
int ios27_21g_mutex_lock(ios27_21g_runtime *rt, ios27_21g_handle handle) {
    slot *s;
    int error;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, handle, IOS27_21G_MUTEX);
    if (!s) error = EINVAL;
    else {
        int previous;
        /* POSIX mutex_lock is not a cancellation point, although the
         * host condition used for contention is one. */
        (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous);
        error = acquire(rt, s);
        (void)pthread_mutex_unlock(&rt->guard);
        (void)pthread_setcancelstate(previous, NULL);
        return error;
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return error;
}
int ios27_21g_mutex_trylock(ios27_21g_runtime *rt, ios27_21g_handle handle) {
    slot *s;
    int error;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, handle, IOS27_21G_MUTEX);
    if (!s) error = EINVAL;
    else if (s->locked) error = EBUSY;
    else error = acquire(rt, s);
    (void)pthread_mutex_unlock(&rt->guard);
    return error;
}
int ios27_21g_mutex_unlock(ios27_21g_runtime *rt, ios27_21g_handle handle) {
    slot *s;
    int error;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, handle, IOS27_21G_MUTEX);
    if (!s) error = EINVAL;
    else if (!s->locked || !pthread_equal(s->owner, pthread_self()))
        error = EPERM;
    else { unlock_logical(s); error = 0; }
    (void)pthread_mutex_unlock(&rt->guard);
    return error;
}
/* Turn a realtime absolute deadline into the monotonic domain exactly once.
 * No unsigned overflows, and expired timestamps map to 'now'. */
static int deadline_to_monotonic(enum ios27_21g_clock clock,
                                 const struct timespec *absolute,
                                 struct timespec *result) {
    struct timespec wall, mono;
    int64_t seconds;
    long nanoseconds;
    if (!absolute || absolute->tv_sec < 0 || absolute->tv_nsec < 0 ||
        absolute->tv_nsec >= NSEC) return EINVAL;
    if (clock == IOS27_21G_MONOTONIC) { *result = *absolute; return 0; }
    if (clock != IOS27_21G_REALTIME) return EINVAL;
    if (clock_gettime(CLOCK_REALTIME, &wall) != 0 ||
        clock_gettime(CLOCK_MONOTONIC, &mono) != 0) return errno;
    if (absolute->tv_sec < wall.tv_sec ||
        (absolute->tv_sec == wall.tv_sec && absolute->tv_nsec <= wall.tv_nsec)) {
        *result = mono;
        return 0;
    }
    seconds = (int64_t)absolute->tv_sec - (int64_t)wall.tv_sec;
    nanoseconds = absolute->tv_nsec - wall.tv_nsec;
    if (nanoseconds < 0) { --seconds; nanoseconds += NSEC; }
    if (seconds > INT64_MAX - (int64_t)mono.tv_sec - 1) return EINVAL;
    seconds += (int64_t)mono.tv_sec;
    nanoseconds += mono.tv_nsec;
    if (nanoseconds >= NSEC) { ++seconds; nanoseconds -= NSEC; }
    result->tv_sec = (time_t)seconds;
    if ((int64_t)result->tv_sec != seconds) return EINVAL;
    result->tv_nsec = nanoseconds;
    return 0;
}
typedef struct {
    ios27_21g_runtime *rt;
    slot *cond, *mutex;
    waiter *w;
} wait_cleanup;
/* Executed with rt->guard locked (POSIX cond cancellation guarantee).
 * Reacquire the *logical* mutex before user cleanup handlers run. */
static void cleanup_wait(void *argument) {
    wait_cleanup *state = argument;
    int previous;
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous);
    if (state->w->queued) dequeue(state->cond, state->w);
    else if (state->w->notified) {
        /* A canceled designated receiver must not strand another waiter. */
        notify_one(state->cond);
    }
    (void)acquire(state->rt, state->mutex);
    if (--state->cond->references == 0)
        state->cond->binding = (ios27_21g_handle){0, 0};
    --state->mutex->references;
    (void)pthread_cond_destroy(&state->w->cv);
    (void)pthread_mutex_unlock(&state->rt->guard);
    (void)pthread_setcancelstate(previous, NULL);
}
static int wait_common(ios27_21g_runtime *rt, ios27_21g_handle ch,
                       ios27_21g_handle mh, enum ios27_21g_clock clock,
                       const struct timespec *absolute) {
    slot *c, *m;
    struct timespec mono;
    pthread_condattr_t attr;
    waiter w = {0};
    wait_cleanup state;
    int error = 0, relock, previous;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    c = lookup(rt, ch, IOS27_21G_COND);
    m = lookup(rt, mh, IOS27_21G_MUTEX);
    if (!c || !m) { error = EINVAL; goto exit; }
    if (!m->locked || !pthread_equal(m->owner, pthread_self())) {
        error = EPERM; goto exit;
    }
    if (c->references && !same_handle(c->binding, mh)) {
        error = EINVAL; goto exit;
    }
    if (absolute) {
        error = deadline_to_monotonic(clock, absolute, &mono);
        if (error) goto exit;
    }
    error = pthread_condattr_init(&attr);
    if (error) goto exit;
    error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!error) error = pthread_cond_init(&w.cv, &attr);
    (void)pthread_condattr_destroy(&attr);
    if (error) goto exit;
    c->binding = mh;
    ++c->references;
    ++m->references;
    enqueue(c, &w);
    state = (wait_cleanup){rt, c, m, &w};
    /* This release and wait registration are atomic under rt->guard. */
    unlock_logical(m);
    pthread_cleanup_push(cleanup_wait, &state);
    for (;;) {
        if (w.notified) { error = 0; break; }
        error = absolute ? pthread_cond_timedwait(&w.cv, &rt->guard, &mono) :
                           pthread_cond_wait(&w.cv, &rt->guard);
        if (w.notified) { error = 0; break; }
        if (error) break;
    }
    /* No cancellation between receiving a signal and reacquiring m. */
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous);
    if (w.queued) {
        dequeue(c, &w);
        if (error == ETIMEDOUT) ++c->timeouts;
    }
    relock = acquire(rt, m);
    if (--c->references == 0) c->binding = (ios27_21g_handle){0, 0};
    --m->references;
    pthread_cleanup_pop(0);
    if (relock) error = relock;
    /* The waiter is no longer visible to any signaler before destruction. */
    (void)pthread_cond_destroy(&w.cv);
    (void)pthread_mutex_unlock(&rt->guard);
    /* Re-enable cancellation only after releasing the internal guard;
     * a pending cancellation can now run caller cleanup handlers safely. */
    (void)pthread_setcancelstate(previous, NULL);
    return error;
exit:
    (void)pthread_mutex_unlock(&rt->guard);
    return error;
}
int ios27_21g_cond_wait(ios27_21g_runtime *rt, ios27_21g_handle c,
                        ios27_21g_handle m) {
    return wait_common(rt, c, m, IOS27_21G_MONOTONIC, NULL);
}
int ios27_21g_cond_timedwait(ios27_21g_runtime *rt, ios27_21g_handle c,
                             ios27_21g_handle m, enum ios27_21g_clock clock,
                             const struct timespec *absolute) {
    if (!absolute) return EINVAL;
    return wait_common(rt, c, m, clock, absolute);
}
static int notify(ios27_21g_runtime *rt, ios27_21g_handle handle, int all) {
    slot *s;
    int error;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, handle, IOS27_21G_COND);
    error = s ? 0 : EINVAL;
    if (s) {
        if (all) while (s->head) notify_one(s);
        else notify_one(s);
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return error;
}
int ios27_21g_cond_signal(ios27_21g_runtime *rt, ios27_21g_handle h) {
    return notify(rt, h, 0);
}
int ios27_21g_cond_broadcast(ios27_21g_runtime *rt, ios27_21g_handle h) {
    return notify(rt, h, 1);
}
int ios27_21g_cond_stats(ios27_21g_runtime *rt, ios27_21g_handle h,
                         ios27_21g_stats *out) {
    slot *s;
    int error;
    if (!rt || !out) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, h, IOS27_21G_COND);
    error = s ? 0 : EINVAL;
    if (s) *out = (ios27_21g_stats){s->queued, s->references,
                                    s->wakeups, s->timeouts};
    (void)pthread_mutex_unlock(&rt->guard);
    return error;
}
