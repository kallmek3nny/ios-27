#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21f_mutex.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

struct waiter;
typedef struct mutex_slot {
    uint32_t generation;
    ios27_21f_kind kind;
    int retired, owned;
    pthread_t owner;
    uint32_t depth;
    struct waiter *first, *last;
    size_t queued;
    uint64_t handoffs;
} mutex_slot;
typedef struct waiter {
    pthread_t thread;
    mutex_slot *target;
    pthread_cond_t wake;
    struct waiter *next, *prev;
    struct waiter *all_next;
    int queued, granted;
} waiter;
struct ios27_21f_runtime {
    pthread_mutex_t guard;
    size_t capacity, pending, live_waiters, peak;
    uint64_t grants, handoffs, deadlocks, canceled;
    waiter *all;
    mutex_slot slots[IOS27_21F_MAX_MUTEXES];
};
static mutex_slot *lookup(ios27_21f_runtime *rt, ios27_21f_handle id) {
    mutex_slot *m;
    if (!id.slot || id.slot > rt->capacity || !id.generation) return NULL;
    m = &rt->slots[id.slot - 1u];
    return m->kind && m->generation == id.generation ? m : NULL;
}
static int kind_ok(ios27_21f_kind kind) {
    return kind == IOS27_21F_NORMAL || kind == IOS27_21F_RECURSIVE ||
           kind == IOS27_21F_ERRORCHECK;
}
int ios27_21f_runtime_create(ios27_21f_runtime **out, size_t capacity) {
    ios27_21f_runtime *rt;
    int err;
    if (!out || !capacity || capacity > IOS27_21F_MAX_MUTEXES) return EINVAL;
    *out = NULL;
    rt = calloc(1, sizeof(*rt));
    if (!rt) return ENOMEM;
    err = pthread_mutex_init(&rt->guard, NULL);
    if (err) { free(rt); return err; }
    rt->capacity = capacity;
    *out = rt;
    return 0;
}
int ios27_21f_runtime_destroy(ios27_21f_runtime *rt) {
    size_t i;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i)
        if (rt->slots[i].kind) break;
    if (i != rt->capacity || rt->live_waiters) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EBUSY;
    }
    (void)pthread_mutex_unlock(&rt->guard);
    (void)pthread_mutex_destroy(&rt->guard);
    free(rt);
    return 0;
}
int ios27_21f_mutex_create(ios27_21f_runtime *rt, ios27_21f_kind kind,
                           ios27_21f_handle *out) {
    size_t i;
    mutex_slot *m;
    if (!rt || !out || !kind_ok(kind)) return EINVAL;
    *out = (ios27_21f_handle){0, 0};
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i)
        if (!rt->slots[i].kind && !rt->slots[i].retired) break;
    if (i == rt->capacity) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EAGAIN;
    }
    m = &rt->slots[i];
    if (!m->generation) m->generation = 1u;
    m->kind = kind;
    m->owned = 0;
    m->depth = 0;
    m->first = m->last = NULL;
    m->queued = 0;
    m->handoffs = 0;
    *out = (ios27_21f_handle){(uint32_t)(i + 1u), m->generation};
    (void)pthread_mutex_unlock(&rt->guard);
    return 0;
}
int ios27_21f_mutex_destroy(ios27_21f_runtime *rt, ios27_21f_handle id) {
    mutex_slot *m;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    m = lookup(rt, id);
    if (!m) err = EINVAL;
    else if (m->owned || m->queued) err = EBUSY;
    else {
        m->kind = 0;
        if (m->generation == UINT32_MAX) m->retired = 1;
        else ++m->generation;
        err = 0;
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
/* All queue and ownership transitions run under rt->guard. */
static void dequeue(ios27_21f_runtime *rt, waiter *w) {
    mutex_slot *m = w->target;
    if (!w->queued) return;
    if (w->prev) w->prev->next = w->next;
    else m->first = w->next;
    if (w->next) w->next->prev = w->prev;
    else m->last = w->prev;
    w->queued = 0;
    --m->queued;
    --rt->pending;
}
static void handoff(ios27_21f_runtime *rt, mutex_slot *m) {
    waiter *w = m->first;
    if (!w) { m->owned = 0; m->depth = 0; return; }
    dequeue(rt, w);
    m->owned = 1;
    m->owner = w->thread;
    m->depth = 1;
    w->granted = 1;
    ++m->handoffs;
    ++rt->handoffs;
    ++rt->grants;
    (void)pthread_cond_signal(&w->wake);
}
static void remove_all(ios27_21f_runtime *rt, waiter *w) {
    waiter **p = &rt->all;
    while (*p && *p != w) p = &(*p)->all_next;
    if (*p) *p = w->all_next;
    --rt->live_waiters;
}
/* Follow owner -> currently blocked mutex -> owner, looking for a cycle. */
static int would_deadlock(ios27_21f_runtime *rt, pthread_t self,
                          pthread_t owner) {
    size_t hops;
    for (hops = 0; hops < IOS27_21F_MAX_WAITERS; ++hops) {
        waiter *w;
        if (pthread_equal(self, owner)) return 1;
        for (w = rt->all; w; w = w->all_next)
            if (w->queued && pthread_equal(w->thread, owner)) break;
        if (!w || !w->target->owned) return 0;
        owner = w->target->owner;
    }
    /* Refuse an unbounded or inconsistent dependency chain. */
    return 1;
}
typedef struct {
    ios27_21f_runtime *rt;
    waiter *w;
} wait_cleanup_state;
/* pthread_cond_wait reacquires rt->guard before running this handler. */
static void canceled_wait(void *argument) {
    wait_cleanup_state *state = argument;
    ios27_21f_runtime *rt = state->rt;
    waiter *w = state->w;
    if (w->queued) dequeue(rt, w);
    if (w->granted) handoff(rt, w->target);
    remove_all(rt, w);
    ++rt->canceled;
    (void)pthread_mutex_unlock(&rt->guard);
    (void)pthread_cond_destroy(&w->wake);
}
static int acquire(ios27_21f_runtime *rt, mutex_slot *m, int try_only) {
    pthread_t self = pthread_self();
    waiter w;
    wait_cleanup_state state;
    volatile int err = 0;
    if (m->owned && pthread_equal(self, m->owner)) {
        if (m->kind == IOS27_21F_RECURSIVE) {
            if (m->depth == UINT32_MAX) return EAGAIN;
            ++m->depth;
            ++rt->grants;
            return 0;
        }
        if (try_only) return EBUSY;
        ++rt->deadlocks;
        return EDEADLK;
    }
    if (!m->owned && !m->first) {
        m->owned = 1;
        m->owner = self;
        m->depth = 1;
        ++rt->grants;
        return 0;
    }
    if (try_only) return EBUSY;
    if (rt->live_waiters >= IOS27_21F_MAX_WAITERS) return EAGAIN;
    if (m->owned && would_deadlock(rt, self, m->owner)) {
        ++rt->deadlocks;
        return EDEADLK;
    }
    err = pthread_cond_init(&w.wake, NULL);
    if (err) return err;
    w.thread = self;
    w.target = m;
    w.next = NULL;
    w.prev = m->last;
    w.queued = 1;
    w.granted = 0;
    w.all_next = rt->all;
    rt->all = &w;
    ++rt->live_waiters;
    ++rt->pending;
    if (rt->pending > rt->peak) rt->peak = rt->pending;
    if (m->last) m->last->next = &w;
    else m->first = &w;
    m->last = &w;
    ++m->queued;
    state.rt = rt;
    state.w = &w;
    pthread_cleanup_push(canceled_wait, &state);
    while (!w.granted && !err)
        err = pthread_cond_wait(&w.wake, &rt->guard);
    pthread_cleanup_pop(0);
    if (err) {
        /* Unusual host wait errors must roll back queue membership. */
        if (w.queued) dequeue(rt, &w);
        if (w.granted) handoff(rt, m);
        ++rt->canceled;
    }
    remove_all(rt, &w);
    (void)pthread_cond_destroy(&w.wake);
    return err;
}
static int call_acquire(ios27_21f_runtime *rt, ios27_21f_handle id,
                        int try_only) {
    mutex_slot *m;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    m = lookup(rt, id);
    err = m ? acquire(rt, m, try_only) : EINVAL;
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
int ios27_21f_mutex_lock(ios27_21f_runtime *rt, ios27_21f_handle id) {
    return call_acquire(rt, id, 0);
}
int ios27_21f_mutex_trylock(ios27_21f_runtime *rt, ios27_21f_handle id) {
    return call_acquire(rt, id, 1);
}
int ios27_21f_mutex_unlock(ios27_21f_runtime *rt, ios27_21f_handle id) {
    mutex_slot *m;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    m = lookup(rt, id);
    if (!m) err = EINVAL;
    else if (!m->owned || !pthread_equal(m->owner, pthread_self()))
        err = EPERM;
    else {
        err = 0;
        if (--m->depth == 0) handoff(rt, m);
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
int ios27_21f_mutex_info(ios27_21f_runtime *rt, ios27_21f_handle id,
                          ios27_21f_info *out) {
    mutex_slot *m;
    int found;
    if (!rt || !out) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    m = lookup(rt, id);
    found = m != NULL;
    if (found) *out = (ios27_21f_info){m->owned, m->depth, m->queued,
                                      m->handoffs};
    (void)pthread_mutex_unlock(&rt->guard);
    return found ? 0 : EINVAL;
}
int ios27_21f_runtime_stats(ios27_21f_runtime *rt, ios27_21f_stats *out) {
    if (!rt || !out) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    *out = (ios27_21f_stats){rt->grants, rt->handoffs, rt->deadlocks,
                             rt->canceled, rt->pending, rt->peak};
    (void)pthread_mutex_unlock(&rt->guard);
    return 0;
}
