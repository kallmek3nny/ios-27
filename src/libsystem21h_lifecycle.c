#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21h_lifecycle.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef IOS27_21H_MAX_GENERATION
#define IOS27_21H_MAX_GENERATION UINT32_MAX
#endif
#if IOS27_21H_MAX_GENERATION < 2 || IOS27_21H_MAX_GENERATION > UINT32_MAX
#error "invalid generation bound"
#endif
#define IOS27_21H_LIMIT 4096u

typedef struct {
    pthread_cond_t wake;
    uint32_t generation;
    unsigned char live, closing, held, retired;
    pthread_t owner;
    size_t waiters;
} object;
struct ios27_21h_runtime {
    pthread_mutex_t guard;
    size_t capacity;
    object slots[];
};

/* All slot state is accessed only under the single runtime guard. */
static object *lookup(ios27_21h_runtime *rt, ios27_21h_handle h) {
    object *s;
    if (!h.slot || h.slot > rt->capacity || !h.generation) return NULL;
    s = &rt->slots[h.slot - 1u];
    return s->live && s->generation == h.generation ? s : NULL;
}

int ios27_21h_runtime_create(ios27_21h_runtime **out, size_t capacity) {
    ios27_21h_runtime *rt;
    int rc;
    if (!out || !capacity || capacity > IOS27_21H_LIMIT ||
        capacity > (SIZE_MAX - sizeof(*rt)) / sizeof(object)) return EINVAL;
    *out = NULL;
    rt = calloc(1, sizeof(*rt) + capacity * sizeof(object));
    if (!rt) return ENOMEM;
    rc = pthread_mutex_init(&rt->guard, NULL);
    if (rc) { free(rt); return rc; }
    rt->capacity = capacity;
    *out = rt;
    return 0;
}

int ios27_21h_runtime_destroy(ios27_21h_runtime *rt) {
    size_t i;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i) {
        if (rt->slots[i].live || rt->slots[i].waiters) {
            (void)pthread_mutex_unlock(&rt->guard);
            return EBUSY;
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    (void)pthread_mutex_destroy(&rt->guard);
    free(rt);
    return 0;
}

int ios27_21h_object_create(ios27_21h_runtime *rt, ios27_21h_handle *out) {
    size_t i;
    int rc;
    object *s;
    if (!rt || !out) return EINVAL;
    *out = (ios27_21h_handle){0, 0};
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i)
        if (!rt->slots[i].live && !rt->slots[i].retired) break;
    if (i == rt->capacity) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EAGAIN;
    }
    s = &rt->slots[i];
    /* Prepare before publishing a handle: failed allocation has no footprint. */
    rc = pthread_cond_init(&s->wake, NULL);
    if (!rc) {
        if (!s->generation) s->generation = 1;
        s->live = 1;
        s->closing = s->held = 0;
        s->waiters = 0;
        *out = (ios27_21h_handle){(uint32_t)(i + 1u), s->generation};
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return rc;
}

int ios27_21h_object_close(ios27_21h_runtime *rt, ios27_21h_handle h) {
    object *s;
    int rc;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, h);
    if (!s) rc = EINVAL;
    else if (s->closing) rc = EALREADY;
    else {
        s->closing = 1;
        rc = pthread_cond_broadcast(&s->wake);
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return rc;
}

int ios27_21h_object_destroy(ios27_21h_runtime *rt, ios27_21h_handle h) {
    object *s;
    int rc;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, h);
    if (!s) rc = EINVAL;
    else if (s->held || s->waiters) rc = EBUSY;
    else {
        rc = pthread_cond_destroy(&s->wake);
        if (!rc) {
            s->live = s->closing = 0;
            if (s->generation == IOS27_21H_MAX_GENERATION) s->retired = 1;
            else ++s->generation;
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return rc;
}

typedef struct { ios27_21h_runtime *rt; object *slot; } wait_cleanup;
static void cleanup_wait(void *context) {
    wait_cleanup *c = context;
    /* POSIX condition cancellation reacquires guard before cleanup runs. */
    --c->slot->waiters;
    (void)pthread_mutex_unlock(&c->rt->guard);
}

int ios27_21h_lock(ios27_21h_runtime *rt, ios27_21h_handle h) {
    object *s;
    int rc = 0;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, h);
    if (!s) rc = EINVAL;
    else if (s->closing) rc = ECANCELED;
    else if (s->held && pthread_equal(s->owner, pthread_self())) rc = EDEADLK;
    else {
        while (s->held && !s->closing) {
            wait_cleanup c = {rt, s};
            ++s->waiters;
            pthread_cleanup_push(cleanup_wait, &c);
            rc = pthread_cond_wait(&s->wake, &rt->guard);
            pthread_cleanup_pop(0);
            --s->waiters;
            if (rc) break;
        }
        if (!rc && s->closing) rc = ECANCELED;
        if (!rc) {
            s->held = 1;
            s->owner = pthread_self();
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return rc;
}

int ios27_21h_trylock(ios27_21h_runtime *rt, ios27_21h_handle h) {
    object *s;
    int rc;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, h);
    if (!s) rc = EINVAL;
    else if (s->closing) rc = ECANCELED;
    else if (s->held) rc = EBUSY;
    else { s->held = 1; s->owner = pthread_self(); rc = 0; }
    (void)pthread_mutex_unlock(&rt->guard);
    return rc;
}

int ios27_21h_unlock(ios27_21h_runtime *rt, ios27_21h_handle h) {
    object *s;
    int rc;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    s = lookup(rt, h);
    if (!s) rc = EINVAL;
    else if (!s->held || !pthread_equal(s->owner, pthread_self())) rc = EPERM;
    else {
        s->held = 0;
        rc = pthread_cond_signal(&s->wake);
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return rc;
}

int ios27_21h_get_stats(ios27_21h_runtime *rt, ios27_21h_stats *out) {
    ios27_21h_stats st = {0};
    size_t i;
    if (!rt || !out) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < rt->capacity; ++i) {
        const object *s = &rt->slots[i];
        st.live += s->live;
        st.closing += s->live && s->closing;
        st.owned += s->live && s->held;
        st.waiting += s->waiters;
        st.retired += s->retired;
    }
    *out = st;
    (void)pthread_mutex_unlock(&rt->guard);
    return 0;
}
