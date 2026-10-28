#define _XOPEN_SOURCE 700
#include "ios27/libsystem21a_abi.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Host locks never occupy the Darwin opaque bytes. All slots are bounded. */
enum object_kind { KIND_MUTEX = 1, KIND_COND = 2, KIND_RWLOCK = 3 };
enum operation {
    OP_MI, OP_MD, OP_ML, OP_MT, OP_MU,
    OP_CI, OP_CD, OP_CS, OP_CB,
    OP_RI, OP_RD, OP_RL, OP_RWL, OP_RTL, OP_RTWL, OP_RU,
    OP_UNSUPPORTED
};
typedef struct {
    uint64_t addr, token;
    unsigned kind, pins;
    union {
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        pthread_rwlock_t rwlock;
    } native;
} object_slot;
struct ios27_21a_runtime {
    unsigned char *arena;
    size_t size;
    uint64_t base, next_token;
    pthread_mutex_t guard;
    object_slot objects[IOS27_21A_MAX_OBJECTS];
};
typedef struct { const char *symbol; enum operation op; } entry;
static const entry symbols[] = {
    {"pthread_mutex_init", OP_MI},
    {"pthread_mutex_destroy", OP_MD},
    {"pthread_mutex_lock", OP_ML},
    {"pthread_mutex_trylock", OP_MT},
    {"pthread_mutex_unlock", OP_MU},
    {"pthread_cond_init", OP_CI},
    {"pthread_cond_destroy", OP_CD},
    {"pthread_cond_signal", OP_CS},
    {"pthread_cond_broadcast", OP_CB},
    {"pthread_rwlock_init", OP_RI},
    {"pthread_rwlock_destroy", OP_RD},
    {"pthread_rwlock_rdlock", OP_RL},
    {"pthread_rwlock_wrlock", OP_RWL},
    {"pthread_rwlock_tryrdlock", OP_RTL},
    {"pthread_rwlock_trywrlock", OP_RTWL},
    {"pthread_rwlock_unlock", OP_RU},
    {"pthread_once", OP_UNSUPPORTED},
    {"pthread_create", OP_UNSUPPORTED},
    {"pthread_join", OP_UNSUPPORTED},
    {"pthread_detach", OP_UNSUPPORTED},
    {"pthread_self", OP_UNSUPPORTED},
    {"pthread_equal", OP_UNSUPPORTED},
    {"pthread_key_create", OP_UNSUPPORTED},
    {"pthread_key_delete", OP_UNSUPPORTED},
    {"pthread_getspecific", OP_UNSUPPORTED},
    {"pthread_setspecific", OP_UNSUPPORTED},
    {"pthread_cond_wait", OP_UNSUPPORTED},
    {"pthread_cond_timedwait", OP_UNSUPPORTED}
};
static size_t object_size(unsigned kind) {
    switch (kind) {
        case KIND_MUTEX: return sizeof(ios27_21a_mutex);
        case KIND_COND: return sizeof(ios27_21a_cond);
        case KIND_RWLOCK: return sizeof(ios27_21a_rwlock);
        default: return 0u;
    }
}
static uint64_t bridge_sig(unsigned kind) {
    return UINT64_C(0x213a0000) + (uint64_t)kind;
}
static int guest_view(ios27_21a_runtime *rt, uint64_t address, size_t span,
                      size_t alignment, unsigned char **out) {
    uint64_t offset;
    if (!rt || !out || !address || !alignment ||
        (alignment & (alignment - 1u))) return EINVAL;
    *out = NULL;
    if ((address & (uint64_t)(alignment - 1u)) || address < rt->base)
        return EFAULT;
    offset = address - rt->base;
    if (offset > (uint64_t)rt->size ||
        span > rt->size - (size_t)offset) return EFAULT;
    *out = rt->arena + (size_t)offset;
    return 0;
}
int ios27_21a_validate(ios27_21a_runtime *rt, uint64_t address,
                       size_t span, size_t alignment) {
    unsigned char *p = NULL;
    return guest_view(rt, address, span, alignment, &p);
}
int ios27_21a_create(ios27_21a_runtime **out, unsigned char *arena,
                     size_t arena_size, uint64_t guest_base) {
    ios27_21a_runtime *rt;
    int error;
    if (!out || !arena || !guest_base || !arena_size ||
        guest_base > UINT64_MAX - (uint64_t)arena_size) return EINVAL;
    *out = NULL;
    rt = calloc(1, sizeof(*rt));
    if (!rt) return ENOMEM;
    rt->arena = arena;
    rt->size = arena_size;
    rt->base = guest_base;
    rt->next_token = 1u;
    error = pthread_mutex_init(&rt->guard, NULL);
    if (error) { free(rt); return error; }
    *out = rt;
    return 0;
}
static object_slot *find_slot(ios27_21a_runtime *rt, uint64_t address) {
    size_t i;
    for (i = 0; i < IOS27_21A_MAX_OBJECTS; ++i)
        if (rt->objects[i].kind && rt->objects[i].addr == address)
            return &rt->objects[i];
    return NULL;
}
static int native_init(object_slot *s, unsigned kind) {
    switch (kind) {
        case KIND_MUTEX: {
            /* Error-checking avoids deadlock on accidental re-lock. */
            pthread_mutexattr_t attr;
            int e = pthread_mutexattr_init(&attr);
            if (e) return e;
            e = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
            if (!e) e = pthread_mutex_init(&s->native.mutex, &attr);
            (void)pthread_mutexattr_destroy(&attr);
            return e;
        }
        case KIND_COND: return pthread_cond_init(&s->native.cond, NULL);
        case KIND_RWLOCK: return pthread_rwlock_init(&s->native.rwlock, NULL);
        default: return EINVAL;
    }
}
static int native_destroy(object_slot *s) {
    switch (s->kind) {
        case KIND_MUTEX: return pthread_mutex_destroy(&s->native.mutex);
        case KIND_COND: return pthread_cond_destroy(&s->native.cond);
        case KIND_RWLOCK: return pthread_rwlock_destroy(&s->native.rwlock);
        default: return EINVAL;
    }
}
static int slot_init(ios27_21a_runtime *rt, uint64_t address, unsigned kind,
                     int static_initializer) {
    unsigned char *p;
    object_slot *s = NULL;
    uint64_t sig, token;
    size_t i, sz = object_size(kind);
    int e = guest_view(rt, address, sz, 8u, &p);
    if (e) return e;
    (void)pthread_mutex_lock(&rt->guard);
    if (find_slot(rt, address)) { e = EBUSY; goto done; }
    memcpy(&sig, p, sizeof(sig));
    if (static_initializer) {
        uint64_t expected = kind == KIND_MUTEX ? IOS27_21A_MUTEX_STATIC_SIG :
                            kind == KIND_COND ? IOS27_21A_COND_STATIC_SIG : 0;
        if (!expected || sig != expected) { e = EINVAL; goto done; }
        for (i = sizeof(sig); i < sz; ++i)
            if (p[i]) { e = EINVAL; goto done; }
    } else if (sig != 0) { e = EBUSY; goto done; }
    for (i = 0; i < IOS27_21A_MAX_OBJECTS; ++i) {
        if (!rt->objects[i].kind) { s = &rt->objects[i]; break; }
    }
    if (!s) { e = EAGAIN; goto done; }
    if (rt->next_token == UINT64_MAX) { e = EAGAIN; goto done; }
    e = native_init(s, kind);
    if (e) goto done;
    token = rt->next_token++;
    memset(p, 0, sz);
    sig = bridge_sig(kind);
    memcpy(p, &sig, sizeof(sig));
    memcpy(p + sizeof(sig), &token, sizeof(token));
    s->addr = address;
    s->token = token;
    s->pins = 0;
    s->kind = kind;
done:
    (void)pthread_mutex_unlock(&rt->guard);
    return e;
}
static int slot_pin(ios27_21a_runtime *rt, uint64_t address, unsigned kind,
                    object_slot **out) {
    unsigned char *p;
    object_slot *s;
    uint64_t sig, token, expected;
    unsigned pass;
    int e = guest_view(rt, address, object_size(kind), 8u, &p);
    if (e) return e;
    *out = NULL;
    for (pass = 0; pass < 2u; ++pass) {
        (void)pthread_mutex_lock(&rt->guard);
        s = find_slot(rt, address);
        if (!s) {
            memcpy(&sig, p, sizeof(sig));
            (void)pthread_mutex_unlock(&rt->guard);
            expected = kind == KIND_MUTEX ? IOS27_21A_MUTEX_STATIC_SIG :
                       kind == KIND_COND ? IOS27_21A_COND_STATIC_SIG : 0u;
            if (pass == 0u && expected && sig == expected) {
                /* One winner initializes the static Darwin object; races
                 * observe the committed bridge record on second pass. */
                e = slot_init(rt, address, kind, 1);
                if (e != 0 && e != EBUSY) return e;
                continue;
            }
            return EINVAL;
        }
        if (s->kind != kind) {
            (void)pthread_mutex_unlock(&rt->guard);
            return EINVAL;
        }
        memcpy(&sig, p, sizeof(sig));
        memcpy(&token, p + sizeof(sig), sizeof(token));
        if (sig != bridge_sig(kind) || token != s->token) {
            (void)pthread_mutex_unlock(&rt->guard);
            return EINVAL;
        }
        if (s->pins == UINT32_MAX) {
            (void)pthread_mutex_unlock(&rt->guard);
            return EAGAIN;
        }
        ++s->pins;
        *out = s;
        (void)pthread_mutex_unlock(&rt->guard);
        return 0;
    }
    return EINVAL;
}
static void slot_unpin(ios27_21a_runtime *rt, object_slot *s) {
    (void)pthread_mutex_lock(&rt->guard);
    --s->pins;
    (void)pthread_mutex_unlock(&rt->guard);
}
static int slot_destroy(ios27_21a_runtime *rt, uint64_t address, unsigned kind) {
    unsigned char *p;
    object_slot *s;
    uint64_t sig, token;
    int e = guest_view(rt, address, object_size(kind), 8u, &p);
    if (e) return e;
    (void)pthread_mutex_lock(&rt->guard);
    s = find_slot(rt, address);
    if (!s || s->kind != kind) { e = EINVAL; goto done; }
    memcpy(&sig, p, sizeof(sig));
    memcpy(&token, p + sizeof(sig), sizeof(token));
    if (sig != bridge_sig(kind) || token != s->token) {
        e = EINVAL;
        goto done;
    }
    if (s->pins) { e = EBUSY; goto done; }
    e = native_destroy(s);
    if (e) goto done;
    memset(p, 0, object_size(kind));
    s->kind = 0;
    s->addr = 0;
    s->token = 0;
done:
    (void)pthread_mutex_unlock(&rt->guard);
    return e;
}
int ios27_21a_destroy(ios27_21a_runtime *rt) {
    size_t i;
    if (!rt) return EINVAL;
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < IOS27_21A_MAX_OBJECTS; ++i) {
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
static int call_native(ios27_21a_runtime *rt, uint64_t address,
                       unsigned kind, enum operation op) {
    object_slot *s = NULL;
    int e = slot_pin(rt, address, kind, &s);
    if (e) return e;
    switch (op) {
        case OP_ML: e = pthread_mutex_lock(&s->native.mutex); break;
        case OP_MT: e = pthread_mutex_trylock(&s->native.mutex); break;
        case OP_MU: e = pthread_mutex_unlock(&s->native.mutex); break;
        case OP_CS: e = pthread_cond_signal(&s->native.cond); break;
        case OP_CB: e = pthread_cond_broadcast(&s->native.cond); break;
        case OP_RL: e = pthread_rwlock_rdlock(&s->native.rwlock); break;
        case OP_RWL: e = pthread_rwlock_wrlock(&s->native.rwlock); break;
        case OP_RTL: e = pthread_rwlock_tryrdlock(&s->native.rwlock); break;
        case OP_RTWL: e = pthread_rwlock_trywrlock(&s->native.rwlock); break;
        case OP_RU: e = pthread_rwlock_unlock(&s->native.rwlock); break;
        default: e = ENOTSUP; break;
    }
    slot_unpin(rt, s);
    return e;
}
int ios27_21a_call(ios27_21a_runtime *rt, const char *symbol,
                   const uint64_t args[6], uint64_t *x0) {
    size_t i;
    enum operation op = OP_UNSUPPORTED;
    int matched = 0, e;
    unsigned kind;
    if (!rt || !symbol || !args || !x0) return EINVAL;
    *x0 = (uint64_t)ENOSYS;
    /* Mach-O leading underscore is stripped by the trusted resolver. */
    for (i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i) {
        if (strcmp(symbol, symbols[i].symbol) == 0) {
            matched = 1; op = symbols[i].op; break;
        }
    }
    if (!matched) return ENOSYS;
    if (op == OP_UNSUPPORTED) { *x0 = ENOTSUP; return ENOTSUP; }
    kind = (op <= OP_MU) ? KIND_MUTEX :
           (op <= OP_CB) ? KIND_COND : KIND_RWLOCK;
    if (op == OP_MI || op == OP_CI || op == OP_RI) {
        /* Null attributes only. Never interpret host attribute layouts. */
        e = args[1] ? ENOTSUP : slot_init(rt, args[0], kind, 0);
    } else if (op == OP_MD || op == OP_CD || op == OP_RD) {
        e = slot_destroy(rt, args[0], kind);
    } else {
        e = call_native(rt, args[0], kind, op);
    }
    *x0 = (uint64_t)e;
    return e;
}
