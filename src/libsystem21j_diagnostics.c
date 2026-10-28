#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

typedef struct { uint32_t generation; int used, retired; } lease_slot;
typedef struct {
    uint32_t stage, actor;
    uint64_t ordinal;
    int inject;
} exact_rule;
struct ios27_21j_diagnostics {
    pthread_mutex_t mutex;
    size_t capacity;
    uint64_t id, seed;
    unsigned failure_per_10000;
    exact_rule rules[IOS27_21J_MAX_FAULT_RULES];
    size_t nrules;
    int rules_frozen;
    lease_slot slots[IOS27_21J_MAX_LEASES];
    ios27_21j_event events[IOS27_21J_TRACE_CAPACITY];
    ios27_21j_snapshot_data counters;
};
static _Atomic uint64_t next_identity = 1;

static uint64_t mix(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static void append(ios27_21j_diagnostics *d, uint32_t kind,
                   uint32_t actor, int32_t result, uint32_t detail) {
    uint64_t seq = ++d->counters.next_sequence;
    ios27_21j_event *event = &d->events[(seq - 1) % IOS27_21J_TRACE_CAPACITY];
    *event = (ios27_21j_event){seq, monotonic_ns(), actor, kind, result, detail};
    if (seq > IOS27_21J_TRACE_CAPACITY) ++d->counters.dropped;
    if (kind == IOS27_21J_CONTENTION) ++d->counters.contention;
    if (kind == IOS27_21J_WAIT) ++d->counters.waits;
    if (kind == IOS27_21J_TIMEOUT) ++d->counters.timeouts;
}
int ios27_21j_create(ios27_21j_diagnostics **out, size_t capacity,
                     uint64_t seed, unsigned failure_per_10000) {
    ios27_21j_diagnostics *d;
    uint64_t id;
    int error;
    if (!out || !capacity || capacity > IOS27_21J_MAX_LEASES ||
        failure_per_10000 > 10000u) return EINVAL;
    *out = NULL;
    d = calloc(1, sizeof(*d));
    if (!d) return ENOMEM;
    error = pthread_mutex_init(&d->mutex, NULL);
    if (error) { free(d); return error; }
    id = atomic_load_explicit(&next_identity, memory_order_relaxed);
    for (;;) {
        if (!id || id == UINT64_MAX) {
            (void)pthread_mutex_destroy(&d->mutex);
            free(d);
            return EAGAIN;
        }
        /* Never wrap the runtime identity allocator: a wrapped ID could
           validate a lease from a destroyed, different runtime. */
        if (atomic_compare_exchange_weak_explicit(
                &next_identity, &id, id + 1u,
                memory_order_relaxed, memory_order_relaxed)) break;
    }
    d->id = id;
    d->seed = seed;
    d->capacity = capacity;
    d->failure_per_10000 = failure_per_10000;
    d->counters.capacity = capacity;
    *out = d;
    return 0;
}
int ios27_21j_destroy(ios27_21j_diagnostics *d) {
    if (!d) return EINVAL;
    (void)pthread_mutex_lock(&d->mutex);
    if (d->counters.active) {
        (void)pthread_mutex_unlock(&d->mutex);
        return EBUSY;
    }
    (void)pthread_mutex_unlock(&d->mutex);
    int error = pthread_mutex_destroy(&d->mutex);
    if (error) return error;
    free(d);
    return 0;
}
int ios27_21j_rule_add(ios27_21j_diagnostics *d, uint32_t stage,
                      uint32_t actor, uint64_t ordinal, int inject) {
    int rc = 0;
    if (!d || !stage || (inject != 0 && inject != 1)) return EINVAL;
    (void)pthread_mutex_lock(&d->mutex);
    if (d->rules_frozen) rc = EBUSY;
    else {
        for (size_t i = 0; i < d->nrules; ++i) {
            const exact_rule *rule = &d->rules[i];
            if (rule->stage == stage && rule->actor == actor &&
                rule->ordinal == ordinal) {
                rc = EEXIST;
                break;
            }
        }
        if (!rc && d->nrules == IOS27_21J_MAX_FAULT_RULES)
            rc = ENOSPC;
        if (!rc)
            d->rules[d->nrules++] = (exact_rule){stage, actor, ordinal, inject};
    }
    (void)pthread_mutex_unlock(&d->mutex);
    return rc;
}
int ios27_21j_rules_clear(ios27_21j_diagnostics *d) {
    if (!d) return EINVAL;
    (void)pthread_mutex_lock(&d->mutex);
    if (d->rules_frozen) {
        (void)pthread_mutex_unlock(&d->mutex);
        return EBUSY;
    }
    d->nrules = 0;
    (void)pthread_mutex_unlock(&d->mutex);
    return 0;
}
int ios27_21j_probe(ios27_21j_diagnostics *d, uint32_t stage,
                    uint32_t actor, uint64_t ordinal) {
    uint64_t value;
    int inject;
    if (!d || !stage) return EINVAL;
    value = mix(d->seed ^ mix(((uint64_t)stage << 32) | actor) ^ mix(ordinal));
    inject = (unsigned)(value % 10000u) < d->failure_per_10000;
    (void)pthread_mutex_lock(&d->mutex);
    d->rules_frozen = 1;
    for (size_t i = 0; i < d->nrules; ++i) {
        const exact_rule *rule = &d->rules[i];
        if (rule->stage == stage && rule->actor == actor &&
            rule->ordinal == ordinal) {
            inject = rule->inject;
            break;
        }
    }
    ++d->counters.probes;
    if (inject) {
        ++d->counters.injections;
        append(d, IOS27_21J_INJECTION, actor, ENOMEM, stage);
    }
    (void)pthread_mutex_unlock(&d->mutex);
    return inject ? ENOMEM : 0;
}
int ios27_21j_acquire(ios27_21j_diagnostics *d, ios27_21j_lease *out) {
    size_t i;
    lease_slot *slot;
    if (!d || !out) return EINVAL;
    *out = (ios27_21j_lease){0, 0, 0};
    (void)pthread_mutex_lock(&d->mutex);
    for (i = 0; i < d->capacity; ++i)
        if (!d->slots[i].used && !d->slots[i].retired) break;
    if (i == d->capacity) {
        (void)pthread_mutex_unlock(&d->mutex);
        return EAGAIN;
    }
    slot = &d->slots[i];
    if (!slot->generation) slot->generation = 1;
    slot->used = 1;
    ++d->counters.active;
    ++d->counters.leases_created;
    if (d->counters.active > d->counters.peak)
        d->counters.peak = d->counters.active;
    *out = (ios27_21j_lease){(uint32_t)(i + 1), slot->generation, d->id};
    append(d, IOS27_21J_CREATE, 0, 0, (uint32_t)(i + 1));
    (void)pthread_mutex_unlock(&d->mutex);
    return 0;
}
int ios27_21j_acquire_injected(ios27_21j_diagnostics *d,
                              uint32_t stage, uint32_t actor,
                              uint64_t ordinal, ios27_21j_lease *out) {
    int rc;
    if (!d || !out || !stage) return EINVAL;
    *out = (ios27_21j_lease){0, 0, 0};
    rc = ios27_21j_probe(d, stage, actor, ordinal);
    return rc ? rc : ios27_21j_acquire(d, out);
}
int ios27_21j_release(ios27_21j_diagnostics *d, ios27_21j_lease lease) {
    lease_slot *slot;
    if (!d || !lease.slot || lease.slot > d->capacity ||
        !lease.generation || lease.owner != d->id) return EINVAL;
    (void)pthread_mutex_lock(&d->mutex);
    slot = &d->slots[lease.slot - 1];
    if (!slot->used || slot->generation != lease.generation) {
        (void)pthread_mutex_unlock(&d->mutex);
        return EINVAL;
    }
    slot->used = 0;
    if (slot->generation == UINT32_MAX) slot->retired = 1;
    else ++slot->generation;
    --d->counters.active;
    ++d->counters.leases_released;
    append(d, IOS27_21J_RELEASE, 0, 0, lease.slot);
    (void)pthread_mutex_unlock(&d->mutex);
    return 0;
}
int ios27_21j_record(ios27_21j_diagnostics *d, uint32_t kind,
                     uint32_t actor, int32_t result, uint32_t detail) {
    if (!d || kind < IOS27_21J_CREATE || kind > IOS27_21J_INJECTION)
        return EINVAL;
    (void)pthread_mutex_lock(&d->mutex);
    append(d, kind, actor, result, detail);
    (void)pthread_mutex_unlock(&d->mutex);
    return 0;
}
int ios27_21j_note_contention(ios27_21j_diagnostics *d,
                              uint32_t actor, uint64_t duration_ns) {
    size_t bucket;
    if (!d) return EINVAL;
    if (duration_ns < 1000u) bucket = 0;
    else if (duration_ns < 10000u) bucket = 1;
    else if (duration_ns < 100000u) bucket = 2;
    else if (duration_ns < 1000000u) bucket = 3;
    else bucket = 4;
    (void)pthread_mutex_lock(&d->mutex);
    if (UINT64_MAX - d->counters.contention_ns_total < duration_ns)
        d->counters.contention_ns_total = UINT64_MAX;
    else d->counters.contention_ns_total += duration_ns;
    if (duration_ns > d->counters.contention_ns_max)
        d->counters.contention_ns_max = duration_ns;
    ++d->counters.contention_ns_buckets[bucket];
    append(d, IOS27_21J_CONTENTION, actor, 0,
           (uint32_t)(duration_ns > UINT32_MAX ? UINT32_MAX : duration_ns));
    (void)pthread_mutex_unlock(&d->mutex);
    return 0;
}
int ios27_21j_snapshot(ios27_21j_diagnostics *d,
                       ios27_21j_snapshot_data *out) {
    if (!d || !out) return EINVAL;
    (void)pthread_mutex_lock(&d->mutex);
    *out = d->counters;
    (void)pthread_mutex_unlock(&d->mutex);
    return 0;
}
int ios27_21j_read(ios27_21j_diagnostics *d, uint64_t sequence,
                   ios27_21j_event *out) {
    int result = 0;
    if (!d || !out || !sequence) return EINVAL;
    (void)pthread_mutex_lock(&d->mutex);
    if (sequence > d->counters.next_sequence ||
        d->counters.next_sequence - sequence >= IOS27_21J_TRACE_CAPACITY)
        result = ERANGE;
    else *out = d->events[(sequence - 1) % IOS27_21J_TRACE_CAPACITY];
    (void)pthread_mutex_unlock(&d->mutex);
    return result;
}

int ios27_21j_export(ios27_21j_diagnostics *d,
                     ios27_21j_event *out, size_t capacity,
                     size_t *count, uint64_t *first, uint64_t *last) {
    size_t available;
    uint64_t start, finish;
    if (!d || !count || (!out && capacity)) return EINVAL;
    (void)pthread_mutex_lock(&d->mutex);
    finish = d->counters.next_sequence;
    available = (size_t)(finish < IOS27_21J_TRACE_CAPACITY ?
                         finish : IOS27_21J_TRACE_CAPACITY);
    start = available ? finish - available + 1u : 0u;
    *count = available;
    if (first) *first = start;
    if (last) *last = finish;
    if (available > capacity || (!out && available)) {
        (void)pthread_mutex_unlock(&d->mutex);
        return ENOSPC;
    }
    for (size_t i = 0; i < available; ++i)
        out[i] = d->events[(start - 1u + i) % IOS27_21J_TRACE_CAPACITY];
    (void)pthread_mutex_unlock(&d->mutex);
    return 0;
}

int ios27_21j_audit(ios27_21j_diagnostics *d,
                    ios27_21j_audit_data *out) {
    ios27_21j_audit_data result = {0};
    uint64_t histogram = 0;
    if (!d || !out) return EINVAL;
    (void)pthread_mutex_lock(&d->mutex);
    for (size_t i = 0; i < d->capacity; ++i) {
        const lease_slot *slot = &d->slots[i];
        if (slot->used) {
            ++result.occupied;
            if (!slot->generation || slot->retired)
                result.violations |= IOS27_21J_AUDIT_SLOTS;
        }
        if (slot->retired) {
            ++result.retired;
            if (slot->generation != UINT32_MAX || slot->used)
                result.violations |= IOS27_21J_AUDIT_SLOTS;
        }
    }
    if (result.occupied != d->counters.active ||
        result.occupied + result.retired > d->capacity)
        result.violations |= IOS27_21J_AUDIT_SLOTS;
    if (d->counters.leases_created < d->counters.leases_released ||
        d->counters.leases_created - d->counters.leases_released !=
            d->counters.active ||
        d->counters.peak < d->counters.active ||
        d->counters.peak > d->capacity ||
        d->counters.capacity != d->capacity ||
        d->counters.injections > d->counters.probes)
        result.violations |= IOS27_21J_AUDIT_LEASE_COUNTS;
    result.last = d->counters.next_sequence;
    result.resident = (size_t)(result.last < IOS27_21J_TRACE_CAPACITY ?
                               result.last : IOS27_21J_TRACE_CAPACITY);
    result.first = result.resident ?
                   result.last - result.resident + 1u : 0u;
    if (d->counters.dropped != result.last - result.resident)
        result.violations |= IOS27_21J_AUDIT_TRACE;
    for (size_t i = 0; i < result.resident; ++i) {
        uint64_t expected = result.first + i;
        const ios27_21j_event *event =
            &d->events[(expected - 1u) % IOS27_21J_TRACE_CAPACITY];
        if (event->sequence != expected ||
            event->kind < IOS27_21J_CREATE ||
            event->kind > IOS27_21J_INJECTION)
            result.violations |= IOS27_21J_AUDIT_TRACE;
    }
    for (size_t bucket = 0; bucket < 5u; ++bucket) {
        uint64_t count = d->counters.contention_ns_buckets[bucket];
        if (UINT64_MAX - histogram < count)
            histogram = UINT64_MAX;
        else histogram += count;
    }
    if (histogram > d->counters.contention ||
        d->counters.contention_ns_total <
            d->counters.contention_ns_max)
        result.violations |= IOS27_21J_AUDIT_CONTENTION;
    *out = result;
    (void)pthread_mutex_unlock(&d->mutex);
    return result.violations ? EPROTO : 0;
}
