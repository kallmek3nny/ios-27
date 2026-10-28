#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21i_abi.h"
#include "ios27/libsystem21c_sync.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SLOTS 128u
struct entry {
    uint64_t address;
    size_t extent, refs;
    uint32_t generation;
    int kind, retired;
    ios27_21c_handle backend;
};
struct ios27_21i_runtime {
    pthread_mutex_t guard;
    ios27_21c_runtime *sync;
    ios27_21i_guest guest;
    size_t capacity;
    struct entry entries[MAX_SLOTS];
};
static int power_of_two(size_t n) { return n && !(n & (n - 1u)); }
static int region(const ios27_21i_guest *g, uint64_t addr,
                  size_t length, size_t alignment) {
    uint64_t offset;
    if (!addr || !alignment || addr % alignment || addr < g->base) return EINVAL;
    offset = addr - g->base;
    if (offset > g->length || length > g->length - (size_t)offset)
        return EFAULT;
    return 0;
}
static struct entry *find(ios27_21i_runtime *rt, uint64_t addr, int kind) {
    for (size_t i = 0; i < rt->capacity; ++i) {
        struct entry *e = &rt->entries[i];
        if (e->kind == kind && e->address == addr) return e;
    }
    return NULL;
}
static int intersects(uint64_t a, size_t an, uint64_t b, size_t bn) {
    /* All ranges were verified to fit inside the same bounded guest region. */
    return a <= b ? (b - a < an) : (a - b < bn);
}
int ios27_21i_create(ios27_21i_runtime **out, const ios27_21i_guest *g,
                     size_t capacity) {
    ios27_21i_runtime *rt;
    int rc;
    if (!out || !g || !g->memory || !capacity || capacity > MAX_SLOTS ||
        !g->length || !g->mutex_extent || !g->cond_extent ||
        g->mutex_extent > g->length || g->cond_extent > g->length ||
        !power_of_two(g->mutex_alignment) ||
        !power_of_two(g->cond_alignment) ||
        g->base > UINT64_MAX - (uint64_t)g->length)
        return EINVAL;
    *out = NULL;
    rt = calloc(1, sizeof(*rt));
    if (!rt) return ENOMEM;
    rc = pthread_mutex_init(&rt->guard, NULL);
    if (rc) { free(rt); return rc; }
    rt->guest = *g;
    rt->capacity = capacity;
    rc = ios27_21c_runtime_create(&rt->sync, capacity);
    if (rc) { (void)pthread_mutex_destroy(&rt->guard); free(rt); return rc; }
    *out = rt;
    return 0;
}
int ios27_21i_destroy(ios27_21i_runtime *rt) {
    int rc;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    for (size_t i = 0; i < rt->capacity; ++i)
        if (rt->entries[i].kind || rt->entries[i].refs) {
            (void)pthread_mutex_unlock(&rt->guard);
            return EBUSY;
        }
    rc = ios27_21c_runtime_destroy(rt->sync);
    (void)pthread_mutex_unlock(&rt->guard);
    if (rc) return rc;
    (void)pthread_mutex_destroy(&rt->guard);
    free(rt);
    return 0;
}
static int create_object(ios27_21i_runtime *rt, uint64_t addr,
                         uint64_t attributes, int kind) {
    struct entry *e = NULL;
    size_t extent = kind == IOS27_21I_MUTEX ? rt->guest.mutex_extent :
                                               rt->guest.cond_extent;
    size_t alignment = kind == IOS27_21I_MUTEX ? rt->guest.mutex_alignment :
                                                  rt->guest.cond_alignment;
    int rc;
    if (attributes) return ENOTSUP; /* No unvalidated guest attr decoding. */
    rc = region(&rt->guest, addr, extent, alignment);
    if (rc) return rc;
    (void)pthread_mutex_lock(&rt->guard);
    for (size_t i = 0; i < rt->capacity; ++i) {
        struct entry *current = &rt->entries[i];
        if (current->kind && intersects(addr, extent, current->address,
                                        current->extent)) {
            (void)pthread_mutex_unlock(&rt->guard);
            return EBUSY;
        }
        if (!e && !current->kind && !current->retired) e = current;
    }
    if (!e) rc = EAGAIN;
    else {
        rc = ios27_21c_sync_create(rt->sync,
                                   kind == IOS27_21I_MUTEX ?
                                   IOS27_21C_NORMAL_MUTEX : IOS27_21C_CONDITION,
                                   &e->backend);
        if (!rc) {
            e->address = addr;
            e->extent = extent;
            e->kind = kind;
            if (!e->generation) e->generation = 1;
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return rc;
}
static int destroy_object(ios27_21i_runtime *rt, uint64_t addr, int kind) {
    struct entry *e;
    int rc;
    (void)pthread_mutex_lock(&rt->guard);
    e = find(rt, addr, kind);
    if (!e) rc = EINVAL;
    else if (e->refs) rc = EBUSY;
    else {
        rc = ios27_21c_sync_destroy(rt->sync, e->backend);
        if (!rc) {
            e->kind = 0;
            e->address = 0;
            e->extent = 0;
            if (e->generation == UINT32_MAX) e->retired = 1;
            else ++e->generation;
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return rc;
}
int ios27_21i_lookup(ios27_21i_runtime *rt, uint64_t addr,
                     int kind, ios27_21i_token *out) {
    struct entry *e;
    if (!rt || !out || (kind != IOS27_21I_MUTEX &&
                       kind != IOS27_21I_CONDITION)) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    e = find(rt, addr, kind);
    if (e) *out = (ios27_21i_token){(uint32_t)(e - rt->entries + 1),
                                    e->generation};
    (void)pthread_mutex_unlock(&rt->guard);
    return e ? 0 : EINVAL;
}
int ios27_21i_verify(ios27_21i_runtime *rt, uint64_t addr,
                     int kind, ios27_21i_token token) {
    struct entry *e;
    int rc;
    if (!rt || !token.slot || token.slot > rt->capacity || !token.generation)
        return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    e = &rt->entries[token.slot - 1u];
    rc = (e->kind == kind && e->address == addr &&
          e->generation == token.generation) ? 0 : EINVAL;
    (void)pthread_mutex_unlock(&rt->guard);
    return rc;
}
struct operation { ios27_21i_runtime *rt; struct entry *a, *b; };
static void release_operation(void *arg) {
    struct operation *op = arg;
    (void)pthread_mutex_lock(&op->rt->guard);
    if (op->a) --op->a->refs;
    if (op->b) --op->b->refs;
    (void)pthread_mutex_unlock(&op->rt->guard);
}
static uint64_t read_le64(const uint8_t *p) {
    uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) result |= (uint64_t)p[i] << (8u*i);
    return result;
}
static int get_deadline(ios27_21i_runtime *rt, uint64_t guest_addr,
                        struct timespec *out) {
    uint64_t offset, sec, nanosec;
    int rc = region(&rt->guest, guest_addr, 16, 8);
    if (rc) return rc;
    offset = guest_addr - rt->guest.base;
    sec = read_le64(rt->guest.memory + (size_t)offset);
    nanosec = read_le64(rt->guest.memory + (size_t)offset + 8u);
    if (sec > INT64_MAX || nanosec >= 1000000000ull) return EINVAL;
    out->tv_sec = (time_t)sec;
    out->tv_nsec = (long)nanosec;
    if ((uint64_t)out->tv_sec != sec) return EINVAL;
    return 0;
}
/* No host address is calculated until validated. No pthread host layout is
 * exposed in guest memory; all state lives in generation-checked sidecars. */
int ios27_21i_dispatch(ios27_21i_runtime *rt, const char *symbol,
                       uint64_t a0, uint64_t a1, uint64_t a2) {
    enum action { INVALID, MUTEX_INIT, COND_INIT, MUTEX_DESTROY, COND_DESTROY,
                  LOCK, TRYLOCK, UNLOCK, WAIT, TIMEDWAIT, SIGNAL, BROADCAST };
    static const struct { const char *name; enum action action; } symbols[] = {
        {"_pthread_mutex_init", MUTEX_INIT},
        {"_pthread_cond_init", COND_INIT},
        {"_pthread_mutex_destroy", MUTEX_DESTROY},
        {"_pthread_cond_destroy", COND_DESTROY},
        {"_pthread_mutex_lock", LOCK},
        {"_pthread_mutex_trylock", TRYLOCK},
        {"_pthread_mutex_unlock", UNLOCK},
        {"_pthread_cond_wait", WAIT},
        {"_pthread_cond_timedwait", TIMEDWAIT},
        {"_pthread_cond_signal", SIGNAL},
        {"_pthread_cond_broadcast", BROADCAST}
    };
    enum action action = INVALID;
    struct entry *e, *m = NULL;
    struct operation op;
    struct timespec deadline;
    ios27_21c_handle primary, mutex = {0, 0};
    int kind, rc;
    if (!rt || !symbol) return EINVAL;
    for (size_t i = 0; i < sizeof(symbols)/sizeof(symbols[0]); ++i)
        if (!strcmp(symbol, symbols[i].name)) { action = symbols[i].action; break; }
    if (action == INVALID) return ENOSYS;
    if (action == MUTEX_INIT || action == COND_INIT)
        return create_object(rt, a0, a1, action == MUTEX_INIT ?
                             IOS27_21I_MUTEX : IOS27_21I_CONDITION);
    if (action == MUTEX_DESTROY || action == COND_DESTROY)
        return destroy_object(rt, a0, action == MUTEX_DESTROY ?
                              IOS27_21I_MUTEX : IOS27_21I_CONDITION);
    kind = (action == LOCK || action == TRYLOCK || action == UNLOCK) ?
           IOS27_21I_MUTEX : IOS27_21I_CONDITION;
    /* Snapshot absolute timeout before taking in-flight references. */
    if (action == TIMEDWAIT) {
        rc = get_deadline(rt, a2, &deadline);
        if (rc) return rc;
    }
    (void)pthread_mutex_lock(&rt->guard);
    e = find(rt, a0, kind);
    if (e && (action == WAIT || action == TIMEDWAIT))
        m = find(rt, a1, IOS27_21I_MUTEX);
    if (!e || ((action == WAIT || action == TIMEDWAIT) && !m)) {
        (void)pthread_mutex_unlock(&rt->guard);
        return EINVAL;
    }
    ++e->refs;
    primary = e->backend;
    if (m) { ++m->refs; mutex = m->backend; }
    (void)pthread_mutex_unlock(&rt->guard);
    op = (struct operation){rt, e, m};
    pthread_cleanup_push(release_operation, &op);
    switch (action) {
    case LOCK: rc = ios27_21c_mutex_lock(rt->sync, primary); break;
    case TRYLOCK: rc = ios27_21c_mutex_trylock(rt->sync, primary); break;
    case UNLOCK: rc = ios27_21c_mutex_unlock(rt->sync, primary); break;
    case WAIT: rc = ios27_21c_cond_wait(rt->sync, primary, mutex); break;
    case TIMEDWAIT:
        rc = ios27_21c_cond_timedwait(rt->sync, primary, mutex,
                                     IOS27_21C_REALTIME, &deadline); break;
    case SIGNAL: rc = ios27_21c_cond_signal(rt->sync, primary); break;
    case BROADCAST: rc = ios27_21c_cond_broadcast(rt->sync, primary); break;
    default: rc = ENOSYS; break;
    }
    pthread_cleanup_pop(1);
    return rc;
}
