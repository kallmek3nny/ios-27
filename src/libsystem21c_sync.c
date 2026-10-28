#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21c_sync.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#define BILLION 1000000000L

typedef struct {
    uint32_t generation;
    enum ios27_21c_kind kind;
    int retired, owned;
    pthread_t owner;
    uint32_t depth;
    size_t waiters, references;
    ios27_21c_handle bound_mutex;
    pthread_cond_t wake;
} slot;
struct ios27_21c_runtime {
    pthread_mutex_t guard;
    size_t capacity;
    slot objects[IOS27_21C_MAX_OBJECTS];
};
/* Keep descriptor lookup out of pthread cleanup setjmp frames at -O2. */
__attribute__((noinline))
static slot *find(ios27_21c_runtime *rt, ios27_21c_handle h) {
    slot *s;
    if (!h.slot || h.slot > rt->capacity || !h.generation) return NULL;
    s = &rt->objects[h.slot - 1u];
    return s->kind && s->generation == h.generation ? s : NULL;
}
static int is_mutex(enum ios27_21c_kind kind) {
    return kind == IOS27_21C_NORMAL_MUTEX ||
           kind == IOS27_21C_RECURSIVE_MUTEX ||
           kind == IOS27_21C_ERRORCHECK_MUTEX;
}
int ios27_21c_runtime_create(ios27_21c_runtime **out, size_t cap) {
    ios27_21c_runtime *rt;
    int err;
    if (!out || cap == 0 || cap > IOS27_21C_MAX_OBJECTS) return EINVAL;
    *out = NULL;
    rt = calloc(1, sizeof(*rt));
    if (!rt) return ENOMEM;
    err = pthread_mutex_init(&rt->guard, NULL);
    if (err) { free(rt); return err; }
    rt->capacity = cap;
    *out = rt;
    return 0;
}
int ios27_21c_runtime_destroy(ios27_21c_runtime *rt) {
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
int ios27_21c_sync_create(ios27_21c_runtime *rt, enum ios27_21c_kind kind,
                          ios27_21c_handle *out) {
    pthread_condattr_t attr;
    slot *s;
    size_t i;
    int err;
    if (!rt || !out || (!is_mutex(kind) && kind != IOS27_21C_CONDITION))
        return EINVAL;
    *out = (ios27_21c_handle){0, 0};
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i)
        if (!rt->objects[i].kind && !rt->objects[i].retired) break;
    if (i == rt->capacity) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EAGAIN;
    }
    s = &rt->objects[i];
    err = pthread_condattr_init(&attr);
    if (!err) {
        err = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        if (!err) err = pthread_cond_init(&s->wake, &attr);
        (void)pthread_condattr_destroy(&attr);
    }
    if (!err) {
        if (!s->generation) s->generation = 1u;
        s->kind = kind;
        s->owned = 0;
        s->depth = 0;
        s->waiters = s->references = 0;
        s->bound_mutex = (ios27_21c_handle){0, 0};
        *out = (ios27_21c_handle){(uint32_t)(i + 1u), s->generation};
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
int ios27_21c_sync_destroy(ios27_21c_runtime *rt, ios27_21c_handle h) {
    slot *s;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = find(rt, h);
    if (!s) err = EINVAL;
    else if (s->owned || s->waiters || s->references) err = EBUSY;
    else {
        err = pthread_cond_destroy(&s->wake);
        if (!err) {
            s->kind = 0;
            if (s->generation == UINT32_MAX) s->retired = 1;
            else ++s->generation;
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
typedef struct { ios27_21c_runtime *rt; slot *object; } lock_cancel_state;
/* pthread_cond_wait reacquires the guard before invoking cleanup handlers. */
static void lock_cancel_cleanup(void *context) {
    lock_cancel_state *state = context;
    --state->object->waiters;
    (void)pthread_mutex_unlock(&state->rt->guard);
}
/* Guard held throughout ownership decisions, including condition release. */
static int acquire(ios27_21c_runtime *rt, slot *s) {
    int err;
    if (s->owned && pthread_equal(s->owner, pthread_self())) {
        if (s->kind != IOS27_21C_RECURSIVE_MUTEX) return EDEADLK;
        if (s->depth == UINT32_MAX) return EAGAIN;
        ++s->depth;
        return 0;
    }
    while (s->owned) {
        lock_cancel_state state = {rt, s};
        ++s->waiters;
        pthread_cleanup_push(lock_cancel_cleanup, &state);
        err = pthread_cond_wait(&s->wake, &rt->guard);
        pthread_cleanup_pop(0);
        --s->waiters;
        if (err) return err;
    }
    s->owner = pthread_self();
    s->owned = 1;
    s->depth = 1;
    return 0;
}
int ios27_21c_mutex_lock(ios27_21c_runtime *rt, ios27_21c_handle h) {
    slot *s;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = find(rt, h);
    err = s && is_mutex(s->kind) ? acquire(rt, s) : EINVAL;
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
int ios27_21c_mutex_trylock(ios27_21c_runtime *rt, ios27_21c_handle h) {
    slot *s;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = find(rt, h);
    if (!s || !is_mutex(s->kind)) err = EINVAL;
    else if (!s->owned) err = acquire(rt, s);
    else if (pthread_equal(s->owner, pthread_self()) &&
             s->kind == IOS27_21C_RECURSIVE_MUTEX) err = acquire(rt, s);
    else err = EBUSY;
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
int ios27_21c_mutex_unlock(ios27_21c_runtime *rt, ios27_21c_handle h) {
    slot *s;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = find(rt, h);
    if (!s || !is_mutex(s->kind)) err = EINVAL;
    else if (!s->owned || !pthread_equal(s->owner, pthread_self()))
        err = EPERM;
    else {
        err = 0;
        if (--s->depth == 0) {
            s->owned = 0;
            (void)pthread_cond_signal(&s->wake);
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
static int notify(ios27_21c_runtime *rt, ios27_21c_handle h, int all) {
    slot *s;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = find(rt, h);
    if (!s || s->kind != IOS27_21C_CONDITION) err = EINVAL;
    else err = all ? pthread_cond_broadcast(&s->wake) :
                     pthread_cond_signal(&s->wake);
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
int ios27_21c_cond_signal(ios27_21c_runtime *rt, ios27_21c_handle h) {
    return notify(rt, h, 0);
}
int ios27_21c_cond_broadcast(ios27_21c_runtime *rt, ios27_21c_handle h) {
    return notify(rt, h, 1);
}
/* Convert an absolute realtime deadline once to the monotonic host clock. */
static int convert(enum ios27_21c_clock clock, const struct timespec *deadline,
                   struct timespec *out) {
    struct timespec wall, mono;
    int64_t sec;
    long ns;
    if (!deadline || deadline->tv_sec < 0 ||
        deadline->tv_nsec < 0 || deadline->tv_nsec >= BILLION) return EINVAL;
    if (clock == IOS27_21C_MONOTONIC) {
        *out = *deadline;
        return 0;
    }
    if (clock != IOS27_21C_REALTIME) return EINVAL;
    if (clock_gettime(CLOCK_REALTIME, &wall) ||
        clock_gettime(CLOCK_MONOTONIC, &mono)) return errno;
    if (deadline->tv_sec < wall.tv_sec ||
        (deadline->tv_sec == wall.tv_sec &&
         deadline->tv_nsec <= wall.tv_nsec)) {
        *out = mono;
        return 0;
    }
    sec = (int64_t)deadline->tv_sec - (int64_t)wall.tv_sec;
    ns = deadline->tv_nsec - wall.tv_nsec;
    if (ns < 0) { --sec; ns += BILLION; }
    if (sec > INT64_MAX - (int64_t)mono.tv_sec - 1) return EINVAL;
    sec += (int64_t)mono.tv_sec;
    ns += mono.tv_nsec;
    if (ns >= BILLION) { ++sec; ns -= BILLION; }
    out->tv_sec = (time_t)sec;
    if ((int64_t)out->tv_sec != sec) return EINVAL;
    out->tv_nsec = ns;
    return 0;
}
typedef struct {
    ios27_21c_runtime *rt;
    slot *condition, *mutex;
} condition_cancel_state;
static void condition_cancel_cleanup(void *context) {
    condition_cancel_state *state = context;
    int previous;
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous);
    /* POSIX requires reacquiring the associated mutex before invoking
     * user-installed cleanup handlers on cancellation of a wait. */
    (void)acquire(state->rt, state->mutex);
    --state->condition->waiters;
    if (!state->condition->waiters)
        state->condition->bound_mutex = (ios27_21c_handle){0, 0};
    --state->mutex->references;
    (void)pthread_mutex_unlock(&state->rt->guard);
    (void)pthread_setcancelstate(previous, NULL);
}
static int wait_common(ios27_21c_runtime *rt, ios27_21c_handle ch,
                       ios27_21c_handle mh, enum ios27_21c_clock clock,
                       const struct timespec *deadline) {
    slot *c, *m;
    struct timespec mono;
    int err, relock, previous;
    condition_cancel_state state;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    c = find(rt, ch);
    m = find(rt, mh);
    if (!c || c->kind != IOS27_21C_CONDITION ||
        !m || !is_mutex(m->kind)) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EINVAL;
    }
    if (!m->owned || !pthread_equal(m->owner, pthread_self())) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EPERM;
    }
    if (m->depth != 1u) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EINVAL;
    }
    if (deadline) {
        err = convert(clock, deadline, &mono);
        if (err) {
            (void)pthread_mutex_unlock(&rt->guard);
            return err;
        }
    }
    /* POSIX does not permit concurrent waits on one condition with
     * different mutexes. Reject instead of permitting ambiguous state. */
    if (c->waiters && (c->bound_mutex.slot != mh.slot ||
                       c->bound_mutex.generation != mh.generation)) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EINVAL;
    }
    c->bound_mutex = mh;
    ++c->waiters;
    ++m->references;
    state = (condition_cancel_state){rt, c, m};
    m->owned = 0;
    m->depth = 0;
    (void)pthread_cond_signal(&m->wake);
    pthread_cleanup_push(condition_cancel_cleanup, &state);
    err = deadline ? pthread_cond_timedwait(&c->wake, &rt->guard, &mono) :
                     pthread_cond_wait(&c->wake, &rt->guard);
    /* A pending cancellation must not interrupt logical mutex reacquisition
     * and leave the condition association or references behind. */
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous);
    relock = acquire(rt, m);
    --c->waiters;
    if (!c->waiters) c->bound_mutex = (ios27_21c_handle){0, 0};
    --m->references;
    pthread_cleanup_pop(0);
    (void)pthread_mutex_unlock(&rt->guard);
    (void)pthread_setcancelstate(previous, NULL);
    return relock ? relock : err;
}
int ios27_21c_cond_wait(ios27_21c_runtime *rt, ios27_21c_handle c,
                        ios27_21c_handle m) {
    return wait_common(rt, c, m, IOS27_21C_MONOTONIC, NULL);
}
int ios27_21c_cond_timedwait(ios27_21c_runtime *rt, ios27_21c_handle c,
                             ios27_21c_handle m, enum ios27_21c_clock clock,
                             const struct timespec *absolute) {
    if (!absolute) return EINVAL;
    return wait_common(rt, c, m, clock, absolute);
}
void ios27_21c_usage(ios27_21c_runtime *rt, size_t *used) {
    size_t count = 0, i;
    if (!used) return;
    if (!rt) { *used = 0; return; }
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i)
        if (rt->objects[i].kind) ++count;
    (void)pthread_mutex_unlock(&rt->guard);
    *used = count;
}
