#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21c_sync.h"
#include "ios27/libsystem21j_diagnostics.h"
#include "ios27/libsystem21j_schedule.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j replay:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
enum { ACTORS = 8, STEPS = 4096, QUEUE_LIMIT = 11, PAIRS = STEPS / 2 };
typedef struct {
    ios27_21j_schedule *schedule;
    ios27_21j_diagnostics *diagnostics;
    ios27_21c_runtime *sync;
    ios27_21c_handle mutex, changed;
    uint32_t plan[STEPS], ring[QUEUE_LIMIT];
    size_t head, tail, length, produced, consumed;
    uint64_t observed_hash, produced_sum, consumed_sum;
} replay;
typedef struct { replay *state; unsigned actor; } actor_arg;

static uint64_t mix(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static uint32_t value_for(size_t step, unsigned actor) {
    return (uint32_t)(mix(((uint64_t)step << 8) | actor) & UINT32_MAX);
}
static uint64_t digest(uint64_t old, size_t step, unsigned actor, uint32_t value) {
    return mix(old ^ mix(((uint64_t)step << 32) ^ ((uint64_t)actor << 24) ^ value));
}
static void *replay_worker(void *arg) {
    actor_arg *a = arg;
    replay *r = a->state;
    for (size_t i = 0; i < STEPS; ++i) {
        if (r->plan[i] != a->actor) continue;
        OK(ios27_21j_turn_acquire(r->schedule, a->actor, i, 15000));
        OK(ios27_21c_mutex_lock(r->sync, r->mutex));
        uint32_t value;
        if (a->actor < 4) {
            CHECK(r->length < QUEUE_LIMIT);
            value = value_for(i, a->actor);
            r->ring[r->tail] = value;
            r->tail = (r->tail + 1u) % QUEUE_LIMIT;
            ++r->length;
            ++r->produced;
            r->produced_sum += value;
            OK(ios27_21c_cond_signal(r->sync, r->changed));
        } else {
            CHECK(r->length);
            size_t producer_step = (i / 16u) * 16u + (i % 16u) - 8u;
            unsigned producer = r->plan[producer_step];
            value = r->ring[r->head];
            CHECK(value == value_for(producer_step, producer));
            r->head = (r->head + 1u) % QUEUE_LIMIT;
            --r->length;
            ++r->consumed;
            r->consumed_sum += value;
            OK(ios27_21c_cond_broadcast(r->sync, r->changed));
        }
        r->observed_hash = digest(r->observed_hash, i, a->actor, value);
        OK(ios27_21j_record(r->diagnostics, IOS27_21J_CHECKPOINT, a->actor,
                             0, (uint32_t)i));
        OK(ios27_21c_mutex_unlock(r->sync, r->mutex));
        OK(ios27_21j_turn_release(r->schedule, a->actor, i));
    }
    return NULL;
}
static uint64_t run(uint64_t seed) {
    replay r = {0};
    pthread_t threads[ACTORS];
    actor_arg args[ACTORS];
    ios27_21j_snapshot_data snapshot;
    uint64_t expected = 0, state = seed, expected_sum = 0;
    size_t next = 0, completed = 0, pending = 0, total = 0;
    unsigned produced_in_block = 0, consumed_in_block = 0;
    uint32_t expected_queue[QUEUE_LIMIT] = {0};
    int held = 0;
    for (size_t i = 0; i < STEPS; ++i) {
        uint32_t v;
        if ((i % 16u) < 8u) r.plan[i] = (uint32_t)(mix(state++) % 4u);
        else r.plan[i] = 4u + (uint32_t)(mix(state++) % 4u);
        if (r.plan[i] < 4) {
            v = value_for(i, r.plan[i]);
            expected_queue[produced_in_block++] = v;
            expected_sum += v;
        } else {
            v = expected_queue[consumed_in_block++];
        }
        expected = digest(expected, i, r.plan[i], v);
        if (i % 16u == 15u) { produced_in_block = 0; consumed_in_block = 0; }
    }
    OK(ios27_21j_schedule_create(&r.schedule, ACTORS, r.plan, STEPS));
    OK(ios27_21j_create(&r.diagnostics, 8, seed, 0));
    OK(ios27_21c_runtime_create(&r.sync, 2));
    OK(ios27_21c_sync_create(r.sync, IOS27_21C_ERRORCHECK_MUTEX, &r.mutex));
    OK(ios27_21c_sync_create(r.sync, IOS27_21C_CONDITION, &r.changed));
    for (unsigned i = 0; i < ACTORS; ++i) {
        args[i] = (actor_arg){&r, i};
        OK(pthread_create(&threads[i], NULL, replay_worker, &args[i]));
    }
    for (unsigned i = 0; i < ACTORS; ++i) OK(pthread_join(threads[i], NULL));
    CHECK(r.length == 0 && r.produced == PAIRS && r.consumed == PAIRS);
    CHECK(r.produced_sum == expected_sum && r.consumed_sum == expected_sum);
    CHECK(r.observed_hash == expected);
    OK(ios27_21j_schedule_snapshot(r.schedule, &completed, &total, &pending, &held));
    CHECK(completed == STEPS && total == STEPS && !pending && !held);
    OK(ios27_21j_snapshot(r.diagnostics, &snapshot));
    CHECK(snapshot.next_sequence == STEPS && snapshot.dropped == STEPS - IOS27_21J_TRACE_CAPACITY);
    for (uint64_t sequence = STEPS - IOS27_21J_TRACE_CAPACITY + 1u;
         sequence <= STEPS; ++sequence) {
        ios27_21j_event event;
        OK(ios27_21j_read(r.diagnostics, sequence, &event));
        CHECK(event.sequence == sequence && event.kind == IOS27_21J_CHECKPOINT);
        CHECK(event.detail == sequence - 1u && event.actor == r.plan[sequence - 1u]);
    }
    CHECK(ios27_21j_read(r.diagnostics, 1, &(ios27_21j_event){0}) == ERANGE);
    ios27_21c_usage(r.sync, &next);
    CHECK(next == 2);
    OK(ios27_21c_sync_destroy(r.sync, r.changed));
    OK(ios27_21c_sync_destroy(r.sync, r.mutex));
    OK(ios27_21c_runtime_destroy(r.sync));
    OK(ios27_21j_schedule_destroy(r.schedule));
    OK(ios27_21j_destroy(r.diagnostics));
    return r.observed_hash;
}
int main(void) {
    const uint64_t a = run(UINT64_C(0x21feed));
    const uint64_t b = run(UINT64_C(0x21feed));
    const uint64_t c = run(UINT64_C(0x21fede));
    CHECK(a == b && a != c);
    printf("phase21j replay: 3 x 4096 scheduled mutex/cond transitions, reproducible journal; PASS\n");
    return 0;
}
