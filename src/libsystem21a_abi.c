#define _XOPEN_SOURCE 700
#include "ios27/libsystem21a_abi.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Host locks never occupy the Darwin opaque bytes. All slots are bounded. */
enum object_kind { KIND_MUTEX = 1, KIND_COND = 2, KIND_RWLOCK = 3, KIND_MUTEXATTR = 4 };
enum operation {
    OP_MI, OP_MD, OP_ML, OP_MT, OP_MU,
    OP_CI, OP_CD, OP_CS, OP_CB,
    OP_RI, OP_RD, OP_RL, OP_RWL, OP_RTL, OP_RTWL, OP_RU,
    OP_MAI, OP_MAD, OP_MAS, OP_MAG, OP_UNSUPPORTED
};
typedef struct {
    uint64_t addr, token;
    unsigned kind, pins;
    unsigned mutex_locked, rw_writer_locked, mutex_type, mutex_depth;
    pthread_t mutex_owner, rw_writer;
    pthread_t rw_reader_ids[32];
    unsigned rw_reader_counts[32];
    union {
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        pthread_rwlock_t rwlock;
    } native;
} object_slot;
struct ios27_21a_runtime {
    ios27_21a_region regions[IOS27_21A_MAX_REGIONS];
    size_t region_count;
    uint64_t next_token;
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
    {"pthread_mutexattr_init", OP_MAI},
    {"pthread_mutexattr_destroy", OP_MAD},
    {"pthread_mutexattr_settype", OP_MAS},
    {"pthread_mutexattr_gettype", OP_MAG},
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
        case KIND_MUTEXATTR: return sizeof(ios27_21a_mutexattr);
        default: return 0u;
    }
}
static uint64_t bridge_sig(unsigned kind) {
    return UINT64_C(0x213a0000) + (uint64_t)kind;
}
/* Darwin arm64 guest records are little-endian, independent of host C ABI. */
static uint64_t guest_load64(const unsigned char *p) {
    uint64_t value = 0;
    unsigned i;
    for (i = 0; i < 8u; ++i) value |= (uint64_t)p[i] << (i * 8u);
    return value;
}
static void guest_store64(unsigned char *p, uint64_t value) {
    unsigned i;
    for (i = 0; i < 8u; ++i) p[i] = (unsigned char)(value >> (i * 8u));
}
static void guest_store32(unsigned char *p, uint32_t value) {
    unsigned i;
    for (i = 0; i < 4u; ++i) p[i] = (unsigned char)(value >> (i * 8u));
}
static int guest_view_access(ios27_21a_runtime *rt, uint64_t address,
                             size_t span, size_t alignment, unsigned access,
                             unsigned char **out) {
    size_t i;
    if (!rt || !out || !address || !alignment ||
        (alignment & (alignment - 1u)) ||
        !access || (access & ~(IOS27_21A_READ | IOS27_21A_WRITE))) return EINVAL;
    *out = NULL;
    if (address & (uint64_t)(alignment - 1u)) return EFAULT;
    for (i = 0; i < rt->region_count; ++i) {
        const ios27_21a_region *r = &rt->regions[i];
        uint64_t offset;
        if (address < r->guest_base) continue;
        offset = address - r->guest_base;
        if (offset > (uint64_t)r->size ||
            span > r->size - (size_t)offset) continue;
        if ((r->permissions & access) != access) return EFAULT;
        *out = r->host + (size_t)offset;
        return 0;
    }
    return EFAULT;
}
static int guest_view(ios27_21a_runtime *rt, uint64_t address, size_t span,
                      size_t alignment, unsigned char **out) {
    return guest_view_access(rt, address, span, alignment,
                             IOS27_21A_READ | IOS27_21A_WRITE, out);
}
int ios27_21a_validate_access(ios27_21a_runtime *rt, uint64_t address,
                              size_t span, size_t alignment, unsigned access) {
    unsigned char *p = NULL;
    return guest_view_access(rt, address, span, alignment, access, &p);
}
int ios27_21a_validate(ios27_21a_runtime *rt, uint64_t address,
                       size_t span, size_t alignment) {
    return ios27_21a_validate_access(rt, address, span, alignment,
                                     IOS27_21A_READ | IOS27_21A_WRITE);
}
int ios27_21a_create_regions(ios27_21a_runtime **out,
                             const ios27_21a_region *regions, size_t count) {
    ios27_21a_runtime *rt;
    size_t i, j;
    int error;
    if (!out) return EINVAL;
    *out = NULL;
    if (!regions || count == 0u || count > IOS27_21A_MAX_REGIONS)
        return EINVAL;
    for (i = 0; i < count; ++i) {
        const ios27_21a_region *a = &regions[i];
        uintptr_t ha = (uintptr_t)a->host;
        if (!a->host || !a->size || !a->guest_base ||
            a->guest_base > UINT64_MAX - (uint64_t)a->size ||
            ha > UINTPTR_MAX - a->size ||
            !(a->permissions & IOS27_21A_READ) ||
            (a->permissions & ~(IOS27_21A_READ | IOS27_21A_WRITE)))
            return EINVAL;
        for (j = 0; j < i; ++j) {
            const ios27_21a_region *b = &regions[j];
            uintptr_t hb = (uintptr_t)b->host;
            if ((a->guest_base < b->guest_base + (uint64_t)b->size &&
                 b->guest_base < a->guest_base + (uint64_t)a->size) ||
                (ha < hb + b->size && hb < ha + a->size))
                return EINVAL;
        }
    }
    rt = calloc(1, sizeof(*rt));
    if (!rt) return ENOMEM;
    memcpy(rt->regions, regions, count * sizeof(regions[0]));
    rt->region_count = count;
    rt->next_token = 1u;
    error = pthread_mutex_init(&rt->guard, NULL);
    if (error) { free(rt); return error; }
    *out = rt;
    return 0;
}
int ios27_21a_create(ios27_21a_runtime **out, unsigned char *arena,
                     size_t arena_size, uint64_t guest_base) {
    const ios27_21a_region region = {
        arena, arena_size, guest_base, IOS27_21A_READ | IOS27_21A_WRITE
    };
    return ios27_21a_create_regions(out, &region, 1u);
}
static object_slot *find_slot(ios27_21a_runtime *rt, uint64_t address) {
    size_t i;
    for (i = 0; i < IOS27_21A_MAX_OBJECTS; ++i)
        if (rt->objects[i].kind && rt->objects[i].addr == address)
            return &rt->objects[i];
    return NULL;
}
static int native_init(object_slot *s, unsigned kind, unsigned mutex_type) {
    switch (kind) {
        case KIND_MUTEX: {
            /* Error-checking avoids deadlock on accidental re-lock. */
            pthread_mutexattr_t attr;
            int e = pthread_mutexattr_init(&attr);
            if (e) return e;
            e = pthread_mutexattr_settype(&attr, mutex_type == 2u ?
                                            PTHREAD_MUTEX_RECURSIVE :
                                            PTHREAD_MUTEX_ERRORCHECK);
            if (!e) e = pthread_mutex_init(&s->native.mutex, &attr);
            (void)pthread_mutexattr_destroy(&attr);
            return e;
        }
        case KIND_COND: return pthread_cond_init(&s->native.cond, NULL);
        case KIND_RWLOCK: return pthread_rwlock_init(&s->native.rwlock, NULL);
        case KIND_MUTEXATTR: return 0;
        default: return EINVAL;
    }
}
static int native_destroy(object_slot *s) {
    switch (s->kind) {
        case KIND_MUTEX: return pthread_mutex_destroy(&s->native.mutex);
        case KIND_COND: return pthread_cond_destroy(&s->native.cond);
        case KIND_RWLOCK: return pthread_rwlock_destroy(&s->native.rwlock);
        case KIND_MUTEXATTR: return 0;
        default: return EINVAL;
    }
}
static int slot_init(ios27_21a_runtime *rt, uint64_t address, unsigned kind,
                     int static_initializer, unsigned mutex_type) {
    unsigned char *p;
    object_slot *s = NULL;
    uint64_t sig, token;
    size_t i, sz = object_size(kind);
    int e = guest_view(rt, address, sz, 8u, &p);
    if (e) return e;
    (void)pthread_mutex_lock(&rt->guard);
    if (find_slot(rt, address)) { e = EBUSY; goto done; }
    /* Reject guest ranges overlapping another live opaque object. */
    for (i = 0; i < IOS27_21A_MAX_OBJECTS; ++i) {
        object_slot *other = &rt->objects[i];
        if (other->kind &&
            address < other->addr + object_size(other->kind) &&
            other->addr < address + sz) {
            e = EBUSY;
            goto done;
        }
    }
    sig = guest_load64(p);
    if (static_initializer) {
        int accepted = kind == KIND_MUTEX ?
                (sig == IOS27_21A_MUTEX_STATIC_SIG ||
                 sig == IOS27_21A_MUTEX_ERRORCHECK_SIG ||
                 sig == IOS27_21A_MUTEX_RECURSIVE_SIG) :
                kind == KIND_COND ? sig == IOS27_21A_COND_STATIC_SIG :
                kind == KIND_RWLOCK ? sig == IOS27_21A_RWLOCK_STATIC_SIG : 0;
        if (!accepted) { e = EINVAL; goto done; }
        if (kind == KIND_MUTEX)
            mutex_type = sig == IOS27_21A_MUTEX_RECURSIVE_SIG ? 2u :
                         sig == IOS27_21A_MUTEX_ERRORCHECK_SIG ? 1u : 0u;
        for (i = sizeof(sig); i < sz; ++i)
            if (p[i]) { e = EINVAL; goto done; }
    } else if (sig != 0) { e = EBUSY; goto done; }
    for (i = 0; i < IOS27_21A_MAX_OBJECTS; ++i) {
        if (!rt->objects[i].kind) { s = &rt->objects[i]; break; }
    }
    if (!s) { e = EAGAIN; goto done; }
    if (rt->next_token == UINT64_MAX) { e = EAGAIN; goto done; }
    e = native_init(s, kind, mutex_type);
    if (e) goto done;
    token = rt->next_token++;
    memset(p, 0, sz);
    sig = bridge_sig(kind);
    guest_store64(p, sig);
    guest_store64(p + 8u, token);
    s->addr = address;
    s->token = token;
    s->pins = 0;
    s->mutex_locked = 0;
    s->rw_writer_locked = 0;
    s->mutex_depth = 0;
    s->mutex_type = mutex_type;
    memset(s->rw_reader_counts, 0, sizeof(s->rw_reader_counts));
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
            sig = guest_load64(p);
            (void)pthread_mutex_unlock(&rt->guard);
            expected = kind == KIND_MUTEX ?
                       (sig == IOS27_21A_MUTEX_STATIC_SIG ||
                        sig == IOS27_21A_MUTEX_ERRORCHECK_SIG ||
                        sig == IOS27_21A_MUTEX_RECURSIVE_SIG) :
                       kind == KIND_COND ? sig == IOS27_21A_COND_STATIC_SIG :
                       kind == KIND_RWLOCK ? sig == IOS27_21A_RWLOCK_STATIC_SIG : 0u;
            if (pass == 0u && expected) {
                /* One winner initializes the static Darwin object; races
                 * observe the committed bridge record on second pass. */
                e = slot_init(rt, address, kind, 1, 0u);
                if (e != 0 && e != EBUSY) return e;
                continue;
            }
            return EINVAL;
        }
        if (s->kind != kind) {
            (void)pthread_mutex_unlock(&rt->guard);
            return EINVAL;
        }
        sig = guest_load64(p);
        token = guest_load64(p + 8u);
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
    sig = guest_load64(p);
    token = guest_load64(p + 8u);
    if (sig != bridge_sig(kind) || token != s->token) {
        e = EINVAL;
        goto done;
    }
    if (s->pins || s->mutex_locked || s->rw_writer_locked) {
        e = EBUSY; goto done;
    }
    if (kind == KIND_RWLOCK) {
        size_t i;
        for (i = 0; i < 32u; ++i)
            if (s->rw_reader_counts[i]) { e = EBUSY; goto done; }
    }
    e = native_destroy(s);
    if (e) goto done;
    memset(p, 0, object_size(kind));
    memset(s, 0, sizeof(*s));
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
/* Ownership is recorded separately from host lock memory. Invalid unlock
 * is rejected before calling pthread_rwlock_unlock (undefined otherwise). */
static int rw_self_deadlock(ios27_21a_runtime *rt, object_slot *s,
                             int wants_writer, int try_only) {
    pthread_t self = pthread_self();
    unsigned i;
    int conflicting;
    (void)pthread_mutex_lock(&rt->guard);
    conflicting = s->rw_writer_locked && pthread_equal(s->rw_writer, self);
    if (wants_writer && !conflicting) {
        for (i = 0; i < 32u; ++i) {
            if (s->rw_reader_counts[i] &&
                pthread_equal(s->rw_reader_ids[i], self)) {
                conflicting = 1;
                break;
            }
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return conflicting ? (try_only ? EBUSY : EDEADLK) : 0;
}
static int rw_record_acquire(ios27_21a_runtime *rt, object_slot *s,
                             int writer) {
    pthread_t self = pthread_self();
    size_t i, empty = 32u;
    int e = 0;
    (void)pthread_mutex_lock(&rt->guard);
    if (writer) {
        s->rw_writer = self;
        s->rw_writer_locked = 1u;
    } else {
        for (i = 0; i < 32u; ++i) {
            if (s->rw_reader_counts[i] &&
                pthread_equal(s->rw_reader_ids[i], self)) {
                if (s->rw_reader_counts[i] == UINT32_MAX) e = EAGAIN;
                else ++s->rw_reader_counts[i];
                break;
            }
            if (!s->rw_reader_counts[i] && empty == 32u) empty = i;
        }
        if (i == 32u) {
            if (empty == 32u) e = EAGAIN;
            else {
                s->rw_reader_ids[empty] = self;
                s->rw_reader_counts[empty] = 1u;
            }
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    if (e) (void)pthread_rwlock_unlock(&s->native.rwlock);
    return e;
}
static int rw_owned_unlock(ios27_21a_runtime *rt, object_slot *s) {
    pthread_t self = pthread_self();
    size_t i;
    int e = EPERM;
    (void)pthread_mutex_lock(&rt->guard);
    if (s->rw_writer_locked && pthread_equal(s->rw_writer, self)) {
        e = pthread_rwlock_unlock(&s->native.rwlock);
        if (!e) s->rw_writer_locked = 0u;
    } else {
        for (i = 0; i < 32u; ++i) {
            if (s->rw_reader_counts[i] &&
                pthread_equal(s->rw_reader_ids[i], self)) {
                e = pthread_rwlock_unlock(&s->native.rwlock);
                if (!e) --s->rw_reader_counts[i];
                break;
            }
        }
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return e;
}
static int mutex_owned_unlock(ios27_21a_runtime *rt, object_slot *s) {
    int e = EPERM;
    (void)pthread_mutex_lock(&rt->guard);
    if (s->mutex_locked && pthread_equal(s->mutex_owner, pthread_self())) {
        e = pthread_mutex_unlock(&s->native.mutex);
        if (!e && --s->mutex_depth == 0u) s->mutex_locked = 0u;
    }
    (void)pthread_mutex_unlock(&rt->guard);
    return e;
}
static int call_native(ios27_21a_runtime *rt, uint64_t address,
                       unsigned kind, enum operation op) {
    object_slot *s = NULL;
    int e = slot_pin(rt, address, kind, &s);
    if (e) return e;
    switch (op) {
        case OP_ML:
        case OP_MT:
            e = (op == OP_ML) ? pthread_mutex_lock(&s->native.mutex) :
                               pthread_mutex_trylock(&s->native.mutex);
            if (!e) {
                (void)pthread_mutex_lock(&rt->guard);
                if (s->mutex_locked && s->mutex_type == 2u &&
                    pthread_equal(s->mutex_owner, pthread_self()))
                    ++s->mutex_depth;
                else {
                    s->mutex_locked = 1u;
                    s->mutex_depth = 1u;
                    s->mutex_owner = pthread_self();
                }
                (void)pthread_mutex_unlock(&rt->guard);
            }
            break;
        case OP_MU: e = mutex_owned_unlock(rt, s); break;
        case OP_CS: e = pthread_cond_signal(&s->native.cond); break;
        case OP_CB: e = pthread_cond_broadcast(&s->native.cond); break;
        case OP_RL:
        case OP_RWL:
        case OP_RTL:
        case OP_RTWL:
            e = rw_self_deadlock(rt, s,
                                 op == OP_RWL || op == OP_RTWL,
                                 op == OP_RTL || op == OP_RTWL);
            if (e) break;
            if (op == OP_RL) e = pthread_rwlock_rdlock(&s->native.rwlock);
            else if (op == OP_RWL) e = pthread_rwlock_wrlock(&s->native.rwlock);
            else if (op == OP_RTL) e = pthread_rwlock_tryrdlock(&s->native.rwlock);
            else e = pthread_rwlock_trywrlock(&s->native.rwlock);
            if (!e) e = rw_record_acquire(rt, s, op == OP_RWL || op == OP_RTWL);
            break;
        case OP_RU: e = rw_owned_unlock(rt, s); break;
        default: e = ENOTSUP; break;
    }
    slot_unpin(rt, s);
    return e;
}
/* Guest out-parameters cannot alias any active opaque object. Check and
 * write under the registry guard to avoid racing bridge metadata updates. */
static int guest_store_u32(ios27_21a_runtime *rt, uint64_t address, uint32_t value) {
    unsigned char *p;
    size_t i;
    int e = guest_view(rt, address, sizeof(value), 4u, &p);
    if (e) return e;
    (void)pthread_mutex_lock(&rt->guard);
    for (i = 0; i < IOS27_21A_MAX_OBJECTS; ++i) {
        object_slot *s = &rt->objects[i];
        if (s->kind && address < s->addr + object_size(s->kind) &&
            s->addr < address + sizeof(value)) {
            (void)pthread_mutex_unlock(&rt->guard);
            return EINVAL;
        }
    }
    guest_store32(p, value);
    (void)pthread_mutex_unlock(&rt->guard);
    return 0;
}
static int attr_value(ios27_21a_runtime *rt, uint64_t address,
                      int setter, unsigned *value) {
    object_slot *s = NULL;
    int e = slot_pin(rt, address, KIND_MUTEXATTR, &s);
    if (e) return e;
    (void)pthread_mutex_lock(&rt->guard);
    if (setter) s->mutex_type = *value;
    else *value = s->mutex_type;
    (void)pthread_mutex_unlock(&rt->guard);
    slot_unpin(rt, s);
    return 0;
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
    if (op == OP_MAI) {
        e = slot_init(rt, args[0], KIND_MUTEXATTR, 0, 0u);
    } else if (op == OP_MAD) {
        e = slot_destroy(rt, args[0], KIND_MUTEXATTR);
    } else if (op == OP_MAS || op == OP_MAG) {
        unsigned type = (unsigned)args[1];
        if (op == OP_MAS && args[1] > 2u) e = EINVAL;
        else if (op == OP_MAG) {
            e = attr_value(rt, args[0], 0, &type);
            if (!e) e = guest_store_u32(rt, args[1], (uint32_t)type);
        } else e = attr_value(rt, args[0], 1, &type);
    } else {
        kind = (op <= OP_MU) ? KIND_MUTEX :
               (op <= OP_CB) ? KIND_COND : KIND_RWLOCK;
        if (op == OP_MI || op == OP_CI || op == OP_RI) {
            unsigned type = 0u;
            if (op == OP_MI && args[1]) {
                e = attr_value(rt, args[1], 0, &type);
                if (e) goto result;
            } else if (args[1]) {
                e = ENOTSUP;
                goto result;
            }
            e = slot_init(rt, args[0], kind, 0, type);
        } else if (op == OP_MD || op == OP_CD || op == OP_RD) {
        e = slot_destroy(rt, args[0], kind);
        } else {
            e = call_native(rt, args[0], kind, op);
        }
    }
result:
    *x0 = (uint64_t)e;
    return e;
}
