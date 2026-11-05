#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21d_rw_once.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

typedef struct reader_owner {
    pthread_t thread;
    struct reader_owner *next;
} reader_owner;

typedef struct waiter {
    pthread_t thread;
    pthread_cond_t changed;
    struct waiter *next;
    reader_owner *reader;
    unsigned char is_writer;
    unsigned char granted;
} waiter;

typedef struct object_slot {
    uint32_t generation;
    enum ios27_21d_kind kind;
    pthread_cond_t changed;
    waiter *first;
    waiter *last;
    reader_owner *owners;
    size_t readers;
    size_t queued;
    unsigned char writer;
    pthread_t writer_thread;
    unsigned char once_state; /* 0 empty, 1 initializing, 2 initialized */
    pthread_t once_thread;
    size_t once_waiters;
} object_slot;

typedef struct once_edge {
    pthread_t thread;
    struct object_slot *target;
    struct once_edge *next;
} once_edge;

struct ios27_21d_runtime {
    pthread_mutex_t guard;
    uint64_t cookie;
    once_edge *once_waiting;
    size_t capacity;
    object_slot slots[IOS27_21D_MAX_OBJECTS];
};

static _Atomic uint64_t next_runtime_cookie = 1u;

/* Retire a slot at generation exhaustion instead of ever reviving a stale
   handle after wraparound. An exhausted slot cannot be allocated again. */
static void next_generation(uint32_t *value) {
    if (*value != UINT32_MAX) ++*value;
}

/* Always called under the runtime guard. */
static object_slot *find_slot(ios27_21d_runtime *rt, ios27_21d_handle h,
                              enum ios27_21d_kind kind) {
    object_slot *slot;
    if (h.runtime_cookie != rt->cookie ||
        h.slot == 0u || h.slot > rt->capacity) return NULL;
    slot = &rt->slots[h.slot - 1u];
    if (slot->kind != kind || slot->generation != h.generation) return NULL;
    return slot;
}

static int owns_read(const object_slot *s, pthread_t thread) {
    const reader_owner *it;
    for (it = s->owners; it; it = it->next)
        if (pthread_equal(it->thread, thread)) return 1;
    return 0;
}

static void enqueue(object_slot *s, waiter *entry) {
    entry->next = NULL;
    if (s->last) s->last->next = entry;
    else s->first = entry;
    s->last = entry;
    ++s->queued;
}

static waiter *dequeue(object_slot *s) {
    waiter *entry = s->first;
    s->first = entry->next;
    if (!s->first) s->last = NULL;
    --s->queued;
    return entry;
}

/* FIFO, with contiguous reader requests admitted together. No newcomer
   bypasses a queued waiter; therefore waiting writers and readers progress
   once existing owners release (subject to OS scheduling fairness). */
static void grant_queue(object_slot *s) {
    if (s->writer) return;
    while (s->first) {
        waiter *entry = s->first;
        if (entry->is_writer) {
            if (s->readers != 0u) break;
            (void)dequeue(s);
            s->writer = 1u;
            s->writer_thread = entry->thread;
            entry->granted = 1u;
            (void)pthread_cond_signal(&entry->changed);
            break;
        }
        (void)dequeue(s);
        entry->reader->next = s->owners;
        s->owners = entry->reader;
        ++s->readers;
        entry->granted = 1u;
        (void)pthread_cond_signal(&entry->changed);
    }
}

/* Defensive rollback if the host condvar reports an error. */
static void unlink_waiter(object_slot *s, waiter *target) {
    waiter **cursor = &s->first;
    while (*cursor && *cursor != target) cursor = &(*cursor)->next;
    if (*cursor) {
        *cursor = target->next;
        if (s->last == target) {
            waiter *it = s->first;
            s->last = NULL;
            while (it) { s->last = it; it = it->next; }
        }
        --s->queued;
        grant_queue(s);
    }
}

int ios27_21d_runtime_create(ios27_21d_runtime **out, size_t capacity) {
    ios27_21d_runtime *rt;
    int err;
    if (!out || !capacity || capacity > IOS27_21D_MAX_OBJECTS) return EINVAL;
    *out = NULL;
    rt = calloc(1u, sizeof(*rt));
    if (!rt) return ENOMEM;
    err = pthread_mutex_init(&rt->guard, NULL);
    if (err) { free(rt); return err; }
    uint64_t expected = atomic_load_explicit(&next_runtime_cookie, memory_order_relaxed);
    for (;;) {
        if (expected == UINT64_MAX) {
            (void)pthread_mutex_destroy(&rt->guard);
            free(rt);
            return EAGAIN;
        }
        if (atomic_compare_exchange_weak_explicit(&next_runtime_cookie, &expected,
                                                  expected + 1u,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) break;
    }
    rt->cookie = expected;
    rt->capacity = capacity;
    *out = rt;
    return 0;
}

int ios27_21d_runtime_destroy(ios27_21d_runtime *rt) {
    size_t i;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i) {
        if (rt->slots[i].kind != 0) {
            (void)pthread_mutex_unlock(&rt->guard);
            return EBUSY;
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    (void)pthread_mutex_destroy(&rt->guard);
    free(rt);
    return 0;
}

int ios27_21d_create(ios27_21d_runtime *rt, enum ios27_21d_kind kind,
                     ios27_21d_handle *out) {
    size_t i;
    object_slot *s;
    int err;
    if (!rt || !out || (kind != IOS27_21D_RWLOCK && kind != IOS27_21D_ONCE))
        return EINVAL;
    *out = (ios27_21d_handle){0u, 0u, 0u};
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity &&
         (rt->slots[i].kind != 0 || rt->slots[i].generation == UINT32_MAX);
         ++i) { }
    if (i == rt->capacity) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EAGAIN;
    }
    s = &rt->slots[i];
    err = kind == IOS27_21D_ONCE ? pthread_cond_init(&s->changed, NULL) : 0;
    if (!err) {
        if (!s->generation) s->generation = 1u;
        s->kind = kind;
        s->first = s->last = NULL;
        s->owners = NULL;
        s->readers = s->queued = s->once_waiters = 0u;
        s->writer = s->once_state = 0u;
        *out = (ios27_21d_handle){(uint32_t)(i + 1u), s->generation,
                                  rt->cookie};
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}

int ios27_21d_destroy(ios27_21d_runtime *rt, ios27_21d_handle h) {
    object_slot *s;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    if (h.runtime_cookie != rt->cookie || !h.slot || h.slot > rt->capacity) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EINVAL;
    }
    s = &rt->slots[h.slot - 1u];
    if (!s->kind || s->generation != h.generation) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EINVAL;
    }
    if (s->first || s->readers || s->writer || s->once_waiters ||
        s->once_state == 1u) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EBUSY;
    }
    err = s->kind == IOS27_21D_ONCE ? pthread_cond_destroy(&s->changed) : 0;
    if (!err) { s->kind = 0; next_generation(&s->generation); }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}

static int acquire(ios27_21d_runtime *rt, ios27_21d_handle h,
                   int writer, int nonblocking, const struct timespec *until) {
    object_slot *s;
    reader_owner *owner = NULL;
    pthread_t self;
    waiter entry;
    int err = 0, old_cancel;
    if (!rt) return EINVAL;
    /* Disable cancellation even across owner allocation, so asynchronous
       cancellation cannot orphan allocated ownership or queued stack nodes. */
    err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel);
    if (err) return err;
    if (!writer) {
        owner = malloc(sizeof(*owner));
        if (!owner) {
            (void)pthread_setcancelstate(old_cancel, NULL);
            return ENOMEM;
        }
    }
    self = pthread_self();
    (void)pthread_mutex_lock(&rt->guard);
    s = find_slot(rt, h, IOS27_21D_RWLOCK);
    if (!s) { err = EINVAL; goto done; }
    if ((s->writer && pthread_equal(s->writer_thread, self)) ||
        owns_read(s, self)) { err = EDEADLK; goto done; }
    if (nonblocking && (s->writer || s->first || (writer && s->readers))) {
        err = EBUSY;
        goto done;
    }
    if (!s->first && !s->writer && (!writer || s->readers == 0u)) {
        if (writer) {
            s->writer = 1u;
            s->writer_thread = self;
        } else {
            owner->thread = self;
            owner->next = s->owners;
            s->owners = owner;
            ++s->readers;
            owner = NULL;
        }
        goto done;
    }
    if (owner) owner->thread = self;
    entry = (waiter){.thread = self, .next = NULL,
                     .reader = owner, .is_writer = (unsigned char)writer,
                     .granted = 0u};
    err = pthread_cond_init(&entry.changed, NULL);
    if (err) goto done;
    enqueue(s, &entry);
    grant_queue(s);
    while (!entry.granted) {
        err = until ? pthread_cond_timedwait(&entry.changed, &rt->guard, until) :
                      pthread_cond_wait(&entry.changed, &rt->guard);
        if (err) {
            /* A concurrent unlock may admit this waiter just as the clock
               reaches its deadline. A confirmed grant wins that race: the
               caller owns the lock and must get success, not ETIMEDOUT. */
            if (err == ETIMEDOUT && entry.granted) { err = 0; break; }
            if (entry.granted) {
                /* Exceptional host-wait failure after admission must not
                   strand a lock held by a caller that will return an error. */
                if (writer) {
                    s->writer = 0u;
                } else {
                    reader_owner **it = &s->owners;
                    while (*it && *it != owner) it = &(*it)->next;
                    if (*it) { *it = owner->next; --s->readers; }
                }
                grant_queue(s);
            } else {
                unlink_waiter(s, &entry);
            }
            break;
        }
    }
    if (!err && !writer) owner = NULL; /* transferred under guard */
    (void)pthread_cond_destroy(&entry.changed);
done:
    (void)pthread_mutex_unlock(&rt->guard);
    free(owner);
    (void)pthread_setcancelstate(old_cancel, NULL);
    return err;
}

int ios27_21d_rdlock(ios27_21d_runtime *rt, ios27_21d_handle h) {
    return acquire(rt, h, 0, 0, NULL);
}
int ios27_21d_wrlock(ios27_21d_runtime *rt, ios27_21d_handle h) {
    return acquire(rt, h, 1, 0, NULL);
}
int ios27_21d_tryrdlock(ios27_21d_runtime *rt, ios27_21d_handle h) {
    return acquire(rt, h, 0, 1, NULL);
}
int ios27_21d_trywrlock(ios27_21d_runtime *rt, ios27_21d_handle h) {
    return acquire(rt, h, 1, 1, NULL);
}

/* CLOCK_REALTIME absolute nanoseconds, including zero (the Unix epoch).
   Perform narrowing checks before any object is queued or mutated. */
static int timed_acquire(ios27_21d_runtime *rt, ios27_21d_handle h,
                         int writer, int64_t deadline_ns) {
    struct timespec until;
    int64_t sec;
    if (deadline_ns < 0) return EINVAL;
    sec = deadline_ns / INT64_C(1000000000);
    until.tv_sec = (time_t)sec;
    until.tv_nsec = (long)(deadline_ns % INT64_C(1000000000));
    if ((int64_t)until.tv_sec != sec) return EINVAL;
    return acquire(rt, h, writer, 0, &until);
}

int ios27_21d_timedrdlock(ios27_21d_runtime *rt, ios27_21d_handle h,
                          int64_t deadline_ns) {
    return timed_acquire(rt, h, 0, deadline_ns);
}

int ios27_21d_timedwrlock(ios27_21d_runtime *rt, ios27_21d_handle h,
                          int64_t deadline_ns) {
    return timed_acquire(rt, h, 1, deadline_ns);
}

int ios27_21d_unlock(ios27_21d_runtime *rt, ios27_21d_handle h) {
    object_slot *s;
    reader_owner **it, *released;
    pthread_t self;
    int err = 0;
    if (!rt) return EINVAL;
    self = pthread_self();
    (void)pthread_mutex_lock(&rt->guard);
    s = find_slot(rt, h, IOS27_21D_RWLOCK);
    if (!s) { err = EINVAL; goto done; }
    if (s->writer && pthread_equal(s->writer_thread, self)) {
        s->writer = 0u;
        grant_queue(s);
        goto done;
    }
    it = &s->owners;
    while (*it && !pthread_equal((*it)->thread, self)) it = &(*it)->next;
    if (!*it) { err = EPERM; goto done; }
    released = *it;
    *it = released->next;
    free(released);
    --s->readers;
    grant_queue(s);
done:
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}


/* Detect A -> B -> ... -> A between concurrent once initializers. All edges
   and object owners are guarded by rt->guard; wait-edge nodes live on the
   blocked callers' stacks until they resume with the guard held. */
static int would_cycle(ios27_21d_runtime *rt, pthread_t current,
                       const object_slot *target) {
    pthread_t owner = target->once_thread;
    for (size_t hop = 0; hop <= rt->capacity; ++hop) {
        once_edge *edge;
        if (pthread_equal(owner, current)) return 1;
        for (edge = rt->once_waiting; edge; edge = edge->next)
            if (pthread_equal(edge->thread, owner)) break;
        if (!edge || edge->target->once_state != 1u) return 0;
        owner = edge->target->once_thread;
    }
    return 1; /* corrupted or cyclic owner graph: fail closed */
}

static void remove_edge(ios27_21d_runtime *rt, once_edge *target) {
    once_edge **it = &rt->once_waiting;
    while (*it && *it != target) it = &(*it)->next;
    if (*it) *it = target->next;
}

int ios27_21d_once(ios27_21d_runtime *rt, ios27_21d_handle h,
                   ios27_21d_initializer initialize, void *context) {
    object_slot *s;
    int err, old_cancel, outcome;
    pthread_t self;
    if (!rt || !initialize) return EINVAL;
    err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel);
    if (err) return err;
    self = pthread_self();
    (void)pthread_mutex_lock(&rt->guard);
    s = find_slot(rt, h, IOS27_21D_ONCE);
    if (!s) { err = EINVAL; goto done; }
    while (s->once_state == 1u) {
        once_edge edge;
        if (would_cycle(rt, self, s)) { err = EDEADLK; goto done; }
        edge = (once_edge){.thread = self, .target = s,
                           .next = rt->once_waiting};
        rt->once_waiting = &edge;
        ++s->once_waiters;
        err = pthread_cond_wait(&s->changed, &rt->guard);
        --s->once_waiters;
        remove_edge(rt, &edge);
        if (err) goto done;
    }
    if (s->once_state == 2u) { err = 0; goto done; }
    s->once_state = 1u;
    s->once_thread = self;
    (void)pthread_mutex_unlock(&rt->guard);
    outcome = initialize(context);
    (void)pthread_mutex_lock(&rt->guard);
    /* Current initializer pins the object by once_state=1. */
    s->once_state = outcome == 0 ? 2u : 0u;
    (void)pthread_cond_broadcast(&s->changed);
    /* The initializer ABI uses positive POSIX error numbers. A negative
       callback result still rolls back but maps to deterministic EINVAL. */
    err = outcome < 0 ? EINVAL : outcome;
done:
    (void)pthread_mutex_unlock(&rt->guard);
    (void)pthread_setcancelstate(old_cancel, NULL);
    return err;
}

int ios27_21d_usage(ios27_21d_runtime *rt, size_t *objects,
                    size_t *waiting, size_t *holders) {
    size_t i, o = 0, w = 0, h = 0;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i) {
        const object_slot *s = &rt->slots[i];
        o += (size_t)(s->kind != 0);
        w += s->queued + s->once_waiters;
        h += s->readers + (size_t)s->writer + (size_t)(s->once_state == 1u);
    }
    (void)pthread_mutex_unlock(&rt->guard);
    if (objects) *objects = o;
    if (waiting) *waiting = w;
    if (holders) *holders = h;
    return 0;
}
