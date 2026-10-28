#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem20d_threads.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    uint32_t generation;
    int occupied, phase, detached, cancelled;
    pthread_t native;
    void *result;
} thread_slot;
typedef struct {
    uint32_t generation;
    enum ios27_20d_sync_kind kind;
    unsigned int pins, holds;
    int once_state;
    pthread_t once_owner;
    union { pthread_mutex_t mutex; pthread_cond_t cond; pthread_rwlock_t rw; } native;
} sync_slot;
typedef struct {
    uint32_t generation;
    int occupied;
    pthread_key_t native;
} key_slot;
struct ios27_20d_runtime {
    pthread_mutex_t guard;
    pthread_cond_t changed;
    size_t capacity, inflight;
    thread_slot threads[IOS27_20D_MAX_THREADS];
    sync_slot syncs[IOS27_20D_MAX_SYNCS];
    key_slot keys[IOS27_20D_MAX_KEYS];
};
typedef struct {
    ios27_20d_runtime *rt;
    ios27_20d_handle handle;
    ios27_20d_start start;
    void *argument;
} worker_data;
static _Thread_local ios27_20d_runtime *current_rt;
static _Thread_local ios27_20d_handle current_id;
static ios27_20d_handle handle_of(size_t index, uint32_t generation) {
    ios27_20d_handle h = {(uint32_t)(index + 1u), generation};
    return h;
}
static void bump(uint32_t *generation) {
    ++*generation;
    if (!*generation) ++*generation;
}
static thread_slot *thread_at(ios27_20d_runtime *rt, ios27_20d_handle h) {
    thread_slot *s;
    if (!h.slot || h.slot > rt->capacity) return NULL;
    s = &rt->threads[h.slot - 1u];
    return s->occupied && s->generation == h.generation ? s : NULL;
}
static sync_slot *sync_at(ios27_20d_runtime *rt, ios27_20d_handle h) {
    sync_slot *s;
    if (!h.slot || h.slot > IOS27_20D_MAX_SYNCS) return NULL;
    s = &rt->syncs[h.slot - 1u];
    return s->kind && s->generation == h.generation ? s : NULL;
}
static key_slot *key_at(ios27_20d_runtime *rt, ios27_20d_handle h) {
    key_slot *s;
    if (!h.slot || h.slot > IOS27_20D_MAX_KEYS) return NULL;
    s = &rt->keys[h.slot - 1u];
    return s->occupied && s->generation == h.generation ? s : NULL;
}
int ios27_20d_create(ios27_20d_runtime **out, size_t max_threads) {
    ios27_20d_runtime *rt;
    int err;
    if (!out || !max_threads || max_threads > IOS27_20D_MAX_THREADS)
        return EINVAL;
    *out = NULL;
    rt = calloc(1u, sizeof(*rt));
    if (!rt) return ENOMEM;
    err = pthread_mutex_init(&rt->guard, NULL);
    if (err) { free(rt); return err; }
    err = pthread_cond_init(&rt->changed, NULL);
    if (err) { (void)pthread_mutex_destroy(&rt->guard); free(rt); return err; }
    rt->capacity = max_threads;
    *out = rt;
    return 0;
}
int ios27_20d_destroy(ios27_20d_runtime *rt) {
    size_t i;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    if (rt->inflight) goto busy;
    for (i = 0; i < rt->capacity; ++i)
        if (rt->threads[i].occupied) goto busy;
    for (i = 0; i < IOS27_20D_MAX_SYNCS; ++i)
        if (rt->syncs[i].kind) goto busy;
    for (i = 0; i < IOS27_20D_MAX_KEYS; ++i)
        if (rt->keys[i].occupied) goto busy;
    (void)pthread_mutex_unlock(&rt->guard);
    (void)pthread_cond_destroy(&rt->changed);
    (void)pthread_mutex_destroy(&rt->guard);
    free(rt);
    return 0;
busy:
    (void)pthread_mutex_unlock(&rt->guard);
    return EBUSY;
}
static void *worker_main(void *data) {
    worker_data *w = data;
    ios27_20d_runtime *rt = w->rt;
    ios27_20d_handle id = w->handle;
    ios27_20d_start start = w->start;
    void *argument = w->argument, *result;
    free(w);
    current_rt = rt;
    current_id = id;
    result = start(argument);
    (void)pthread_mutex_lock(&rt->guard);
    thread_slot *s = thread_at(rt, id);
    if (s) {
        s->result = result;
        if (s->phase != 3) s->phase = 2;
        if (s->detached) { s->occupied = 0; bump(&s->generation); }
    }
    --rt->inflight;
    (void)pthread_cond_broadcast(&rt->changed);
    (void)pthread_mutex_unlock(&rt->guard);
    current_rt = NULL;
    current_id = (ios27_20d_handle){0, 0};
    return result;
}
int ios27_20d_thread_create(ios27_20d_runtime *rt, ios27_20d_handle *out,
                            ios27_20d_start start, void *argument) {
    size_t i;
    worker_data *w;
    int err;
    if (!rt || !out || !start) return EINVAL;
    *out = (ios27_20d_handle){0, 0};
    w = malloc(sizeof(*w));
    if (!w) return ENOMEM;
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i)
        if (!rt->threads[i].occupied) break;
    if (i == rt->capacity) {
        (void)pthread_mutex_unlock(&rt->guard);
        free(w);
        return EAGAIN;
    }
    thread_slot *s = &rt->threads[i];
    if (!s->generation) s->generation = 1u;
    s->occupied = 1;
    s->phase = 1;
    s->detached = s->cancelled = 0;
    s->result = NULL;
    w->rt = rt;
    w->handle = handle_of(i, s->generation);
    w->start = start;
    w->argument = argument;
    ++rt->inflight;
    err = pthread_create(&s->native, NULL, worker_main, w);
    if (err) {
        s->occupied = 0;
        bump(&s->generation);
        --rt->inflight;
        (void)pthread_mutex_unlock(&rt->guard);
        free(w);
        return err;
    }
    *out = handle_of(i, s->generation);
    (void)pthread_mutex_unlock(&rt->guard);
    return 0;
}
int ios27_20d_thread_join(ios27_20d_runtime *rt, ios27_20d_handle h,
                          void **result) {
    thread_slot *s;
    pthread_t native;
    void *value = NULL;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = thread_at(rt, h);
    if (!s) { (void)pthread_mutex_unlock(&rt->guard); return ESRCH; }
    if (s->detached || s->phase == 3) {
        (void)pthread_mutex_unlock(&rt->guard); return EINVAL;
    }
    native = s->native;
    if (pthread_equal(native, pthread_self())) {
        (void)pthread_mutex_unlock(&rt->guard); return EDEADLK;
    }
    s->phase = 3;
    (void)pthread_mutex_unlock(&rt->guard);
    err = pthread_join(native, &value);
    (void)pthread_mutex_lock(&rt->guard);
    s = thread_at(rt, h);
    if (s) {
        if (!err) { s->occupied = 0; bump(&s->generation); }
        else s->phase = 2;
    }
    (void)pthread_mutex_unlock(&rt->guard);
    if (!err && result) *result = value;
    return err;
}
int ios27_20d_thread_detach(ios27_20d_runtime *rt, ios27_20d_handle h) {
    thread_slot *s;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = thread_at(rt, h);
    if (!s) { (void)pthread_mutex_unlock(&rt->guard); return ESRCH; }
    if (s->detached || s->phase == 3) {
        (void)pthread_mutex_unlock(&rt->guard); return EINVAL;
    }
    err = pthread_detach(s->native);
    if (!err) {
        s->detached = 1;
        if (s->phase == 2) { s->occupied = 0; bump(&s->generation); }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
ios27_20d_handle ios27_20d_thread_self(ios27_20d_runtime *rt) {
    if (rt && current_rt == rt) return current_id;
    return (ios27_20d_handle){0, 0};
}
int ios27_20d_cancel_request(ios27_20d_runtime *rt, ios27_20d_handle h) {
    thread_slot *s;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = thread_at(rt, h);
    if (s) s->cancelled = 1;
    (void)pthread_mutex_unlock(&rt->guard);
    return s ? 0 : ESRCH;
}
int ios27_20d_cancel_point(ios27_20d_runtime *rt) {
    thread_slot *s;
    int err;
    if (!rt || current_rt != rt) return ESRCH;
    (void)pthread_mutex_lock(&rt->guard);
    s = thread_at(rt, current_id);
    err = s ? (s->cancelled ? ECANCELED : 0) : ESRCH;
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
static int pin(ios27_20d_runtime *rt, ios27_20d_handle h,
               enum ios27_20d_sync_kind a, enum ios27_20d_sync_kind b,
               sync_slot **out) {
    sync_slot *s;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = sync_at(rt, h);
    if (!s || (s->kind != a && s->kind != b)) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EINVAL;
    }
    ++s->pins;
    *out = s;
    (void)pthread_mutex_unlock(&rt->guard);
    return 0;
}
static void unpin(ios27_20d_runtime *rt, sync_slot *s) {
    (void)pthread_mutex_lock(&rt->guard);
    --s->pins;
    (void)pthread_mutex_unlock(&rt->guard);
}
static void held(ios27_20d_runtime *rt, sync_slot *s, int delta) {
    (void)pthread_mutex_lock(&rt->guard);
    if (delta > 0) ++s->holds;
    else --s->holds;
    (void)pthread_mutex_unlock(&rt->guard);
}
int ios27_20d_sync_create(ios27_20d_runtime *rt, enum ios27_20d_sync_kind kind,
                          ios27_20d_handle *out) {
    size_t i;
    sync_slot *s;
    int err = 0;
    if (!rt || !out || kind < IOS27_20D_MUTEX || kind > IOS27_20D_ONCE)
        return EINVAL;
    *out = (ios27_20d_handle){0, 0};
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < IOS27_20D_MAX_SYNCS; ++i)
        if (!rt->syncs[i].kind) break;
    if (i == IOS27_20D_MAX_SYNCS) {
        (void)pthread_mutex_unlock(&rt->guard); return EAGAIN;
    }
    s = &rt->syncs[i];
    if (kind == IOS27_20D_MUTEX || kind == IOS27_20D_RECURSIVE_MUTEX) {
        pthread_mutexattr_t attr;
        err = pthread_mutexattr_init(&attr);
        if (!err) {
            err = pthread_mutexattr_settype(&attr, kind == IOS27_20D_MUTEX ?
                                            PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_RECURSIVE);
            if (!err) err = pthread_mutex_init(&s->native.mutex, &attr);
            (void)pthread_mutexattr_destroy(&attr);
        }
    } else if (kind == IOS27_20D_CONDITION) {
        err = pthread_cond_init(&s->native.cond, NULL);
    } else if (kind == IOS27_20D_RWLOCK) {
        err = pthread_rwlock_init(&s->native.rw, NULL);
    }
    if (!err) {
        if (!s->generation) s->generation = 1u;
        s->kind = kind;
        s->once_state = 0;
        s->pins = s->holds = 0;
        *out = handle_of(i, s->generation);
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
int ios27_20d_sync_destroy(ios27_20d_runtime *rt, ios27_20d_handle h) {
    sync_slot *s;
    int err = 0;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = sync_at(rt, h);
    if (!s) { (void)pthread_mutex_unlock(&rt->guard); return EINVAL; }
    if (s->pins || s->holds || s->once_state == 1) {
        (void)pthread_mutex_unlock(&rt->guard); return EBUSY;
    }
    if (s->kind == IOS27_20D_MUTEX || s->kind == IOS27_20D_RECURSIVE_MUTEX)
        err = pthread_mutex_destroy(&s->native.mutex);
    else if (s->kind == IOS27_20D_CONDITION)
        err = pthread_cond_destroy(&s->native.cond);
    else if (s->kind == IOS27_20D_RWLOCK)
        err = pthread_rwlock_destroy(&s->native.rw);
    if (!err) { s->kind = 0; bump(&s->generation); }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
static int mutex_op(ios27_20d_runtime *rt, ios27_20d_handle h, int op) {
    sync_slot *s;
    int err = pin(rt, h, IOS27_20D_MUTEX, IOS27_20D_RECURSIVE_MUTEX, &s);
    if (err) return err;
    if (op == 0) err = pthread_mutex_lock(&s->native.mutex);
    else if (op == 1) err = pthread_mutex_trylock(&s->native.mutex);
    else err = pthread_mutex_unlock(&s->native.mutex);
    if (!err) held(rt, s, op == 2 ? -1 : 1);
    unpin(rt, s);
    return err;
}
int ios27_20d_mutex_lock(ios27_20d_runtime *rt, ios27_20d_handle h) {
    return mutex_op(rt, h, 0);
}
int ios27_20d_mutex_trylock(ios27_20d_runtime *rt, ios27_20d_handle h) {
    return mutex_op(rt, h, 1);
}
int ios27_20d_mutex_unlock(ios27_20d_runtime *rt, ios27_20d_handle h) {
    return mutex_op(rt, h, 2);
}
static int rw_op(ios27_20d_runtime *rt, ios27_20d_handle h, int op) {
    sync_slot *s;
    int err = pin(rt, h, IOS27_20D_RWLOCK, IOS27_20D_RWLOCK, &s);
    if (err) return err;
    if (op == 0) err = pthread_rwlock_rdlock(&s->native.rw);
    else if (op == 1) err = pthread_rwlock_wrlock(&s->native.rw);
    else err = pthread_rwlock_unlock(&s->native.rw);
    if (!err) held(rt, s, op == 2 ? -1 : 1);
    unpin(rt, s);
    return err;
}
int ios27_20d_rwlock_rdlock(ios27_20d_runtime *rt, ios27_20d_handle h) {
    return rw_op(rt, h, 0);
}
int ios27_20d_rwlock_wrlock(ios27_20d_runtime *rt, ios27_20d_handle h) {
    return rw_op(rt, h, 1);
}
int ios27_20d_rwlock_unlock(ios27_20d_runtime *rt, ios27_20d_handle h) {
    return rw_op(rt, h, 2);
}
int ios27_20d_cond_signal(ios27_20d_runtime *rt, ios27_20d_handle h,
                          int broadcast) {
    sync_slot *s;
    int err = pin(rt, h, IOS27_20D_CONDITION, IOS27_20D_CONDITION, &s);
    if (err) return err;
    err = broadcast ? pthread_cond_broadcast(&s->native.cond) :
                      pthread_cond_signal(&s->native.cond);
    unpin(rt, s);
    return err;
}
int ios27_20d_cond_wait(ios27_20d_runtime *rt, ios27_20d_handle ch,
                         ios27_20d_handle mh, int64_t deadline_ns) {
    sync_slot *c, *m;
    int err;
    if (deadline_ns < 0) return EINVAL;
    err = pin(rt, ch, IOS27_20D_CONDITION, IOS27_20D_CONDITION, &c);
    if (err) return err;
    err = pin(rt, mh, IOS27_20D_MUTEX, IOS27_20D_RECURSIVE_MUTEX, &m);
    if (err) { unpin(rt, c); return err; }
    if (deadline_ns) {
        struct timespec ts;
        ts.tv_sec = (time_t)(deadline_ns / INT64_C(1000000000));
        ts.tv_nsec = (long)(deadline_ns % INT64_C(1000000000));
        if ((int64_t)ts.tv_sec != deadline_ns / INT64_C(1000000000))
            err = EINVAL;
        else err = pthread_cond_timedwait(&c->native.cond, &m->native.mutex, &ts);
    } else err = pthread_cond_wait(&c->native.cond, &m->native.mutex);
    unpin(rt, m);
    unpin(rt, c);
    return err;
}
int ios27_20d_once(ios27_20d_runtime *rt, ios27_20d_handle h,
                    ios27_20d_initializer init, void *context) {
    sync_slot *s;
    int err;
    if (!init) return EINVAL;
    err = pin(rt, h, IOS27_20D_ONCE, IOS27_20D_ONCE, &s);
    if (err) return err;
    (void)pthread_mutex_lock(&rt->guard);
    while (s->once_state == 1) {
        if (pthread_equal(s->once_owner, pthread_self())) {
            (void)pthread_mutex_unlock(&rt->guard);
            unpin(rt, s);
            return EDEADLK;
        }
        (void)pthread_cond_wait(&rt->changed, &rt->guard);
    }
    if (s->once_state == 2) {
        (void)pthread_mutex_unlock(&rt->guard);
        unpin(rt, s);
        return 0;
    }
    s->once_state = 1;
    s->once_owner = pthread_self();
    (void)pthread_mutex_unlock(&rt->guard);
    init(context);
    (void)pthread_mutex_lock(&rt->guard);
    s->once_state = 2;
    (void)pthread_cond_broadcast(&rt->changed);
    (void)pthread_mutex_unlock(&rt->guard);
    unpin(rt, s);
    return 0;
}
int ios27_20d_key_create(ios27_20d_runtime *rt, ios27_20d_handle *out,
                         ios27_20d_destructor destructor) {
    size_t i;
    int err;
    if (!rt || !out) return EINVAL;
    *out = (ios27_20d_handle){0, 0};
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < IOS27_20D_MAX_KEYS; ++i)
        if (!rt->keys[i].occupied) break;
    if (i == IOS27_20D_MAX_KEYS) {
        (void)pthread_mutex_unlock(&rt->guard); return EAGAIN;
    }
    key_slot *s = &rt->keys[i];
    err = pthread_key_create(&s->native, destructor);
    if (!err) {
        if (!s->generation) s->generation = 1u;
        s->occupied = 1;
        *out = handle_of(i, s->generation);
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
int ios27_20d_key_delete(ios27_20d_runtime *rt, ios27_20d_handle h) {
    key_slot *s;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = key_at(rt, h);
    if (!s) { (void)pthread_mutex_unlock(&rt->guard); return EINVAL; }
    err = pthread_key_delete(s->native);
    if (!err) { s->occupied = 0; bump(&s->generation); }
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
int ios27_20d_setspecific(ios27_20d_runtime *rt, ios27_20d_handle h,
                           const void *value) {
    key_slot *s;
    int err;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = key_at(rt, h);
    err = s ? pthread_setspecific(s->native, value) : EINVAL;
    (void)pthread_mutex_unlock(&rt->guard);
    return err;
}
int ios27_20d_getspecific(ios27_20d_runtime *rt, ios27_20d_handle h,
                           void **out) {
    key_slot *s;
    if (!rt || !out) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = key_at(rt, h);
    if (s) *out = pthread_getspecific(s->native);
    (void)pthread_mutex_unlock(&rt->guard);
    return s ? 0 : EINVAL;
}
void ios27_20d_usage(ios27_20d_runtime *rt, size_t *threads,
                      size_t *syncs, size_t *keys) {
    size_t t = 0, s = 0, k = 0, i;
    if (rt) {
        (void)pthread_mutex_lock(&rt->guard);
        for (i = 0; i < rt->capacity; ++i) t += (size_t)rt->threads[i].occupied;
        for (i = 0; i < IOS27_20D_MAX_SYNCS; ++i) s += (size_t)(rt->syncs[i].kind != 0);
        for (i = 0; i < IOS27_20D_MAX_KEYS; ++i) k += (size_t)rt->keys[i].occupied;
        (void)pthread_mutex_unlock(&rt->guard);
    }
    if (threads) *threads = t;
    if (syncs) *syncs = s;
    if (keys) *keys = k;
}
