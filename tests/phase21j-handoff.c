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
    fprintf(stderr, "phase21j handoff:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
enum { CYCLES = 1024, STEPS = CYCLES * 3 };
typedef struct {
    ios27_21c_runtime *sync;
    ios27_21c_handle mutex, changed;
    ios27_21j_schedule *schedule;
    ios27_21j_diagnostics *diag;
    unsigned value, signals, wakes, wait_calls;
} state;
static void *waiter(void *arg) {
    state *s = arg;
    for (unsigned i = 0; i < CYCLES; ++i) {
        OK(ios27_21c_mutex_lock(s->sync, s->mutex));
        OK(ios27_21j_turn_acquire(s->schedule, 0, i * 3u, 5000));
        CHECK(s->value == i);
        OK(ios27_21j_turn_release(s->schedule, 0, i * 3u));
        /* Still holds the same native mutex, so the signaler cannot
           publish the event until cond_wait has atomically released it. */
        while (s->value == i) {
            ++s->wait_calls;
            OK(ios27_21c_cond_wait(s->sync, s->changed, s->mutex));
        }
        CHECK(s->value == i + 1u);
        ++s->wakes;
        OK(ios27_21j_record(s->diag, IOS27_21J_WAIT, 0, 0, i));
        OK(ios27_21j_turn_acquire(s->schedule, 0, i * 3u + 2u, 5000));
        OK(ios27_21j_turn_release(s->schedule, 0, i * 3u + 2u));
        OK(ios27_21c_mutex_unlock(s->sync, s->mutex));
    }
    return NULL;
}
static void *signaler(void *arg) {
    state *s = arg;
    for (unsigned i = 0; i < CYCLES; ++i) {
        OK(ios27_21j_turn_acquire(s->schedule, 1, i * 3u + 1u, 5000));
        OK(ios27_21c_mutex_lock(s->sync, s->mutex));
        CHECK(s->value == i);
        ++s->value;
        ++s->signals;
        OK(ios27_21c_cond_signal(s->sync, s->changed));
        OK(ios27_21j_record(s->diag, IOS27_21J_CHECKPOINT, 1, 0, i));
        OK(ios27_21c_mutex_unlock(s->sync, s->mutex));
        OK(ios27_21j_turn_release(s->schedule, 1, i * 3u + 1u));
    }
    return NULL;
}
int main(void) {
    uint32_t plan[STEPS];
    state s = {0};
    pthread_t first, second;
    ios27_21j_snapshot_data snapshot;
    ios27_21j_event events[IOS27_21J_TRACE_CAPACITY];
    size_t count, completed, waiting, total;
    uint64_t start, finish;
    int held;
    for (unsigned i = 0; i < CYCLES; ++i) {
        plan[3u * i] = 0;
        plan[3u * i + 1u] = 1;
        plan[3u * i + 2u] = 0;
    }
    OK(ios27_21c_runtime_create(&s.sync, 2));
    OK(ios27_21c_sync_create(s.sync, IOS27_21C_ERRORCHECK_MUTEX, &s.mutex));
    OK(ios27_21c_sync_create(s.sync, IOS27_21C_CONDITION, &s.changed));
    OK(ios27_21j_schedule_create(&s.schedule, 2, plan, STEPS));
    OK(ios27_21j_create(&s.diag, 2, 0x21fa, 0));
    OK(pthread_create(&first, NULL, waiter, &s));
    OK(pthread_create(&second, NULL, signaler, &s));
    OK(pthread_join(first, NULL));
    OK(pthread_join(second, NULL));
    CHECK(s.value == CYCLES && s.signals == CYCLES && s.wakes == CYCLES);
    CHECK(s.wait_calls >= CYCLES);
    OK(ios27_21j_schedule_snapshot(s.schedule, &completed, &total, &waiting, &held));
    CHECK(completed == STEPS && total == STEPS && waiting == 0 && held == 0);
    OK(ios27_21j_snapshot(s.diag, &snapshot));
    CHECK(snapshot.waits == CYCLES && snapshot.next_sequence == 2u * CYCLES);
    CHECK(snapshot.dropped == 2u * CYCLES - IOS27_21J_TRACE_CAPACITY);
    OK(ios27_21j_export(s.diag, events, IOS27_21J_TRACE_CAPACITY,
                          &count, &start, &finish));
    CHECK(count == IOS27_21J_TRACE_CAPACITY && finish == 2u * CYCLES);
    for (size_t offset = 0; offset < count; ++offset) {
        ios27_21j_event e = events[offset];
        CHECK(e.sequence == start + offset);
        CHECK(e.kind == (offset % 2u ? IOS27_21J_WAIT : IOS27_21J_CHECKPOINT));
        CHECK(e.actor == (offset % 2u ? 0u : 1u));
        CHECK(e.detail == (uint32_t)((start + offset - 1u) / 2u));
    }
    OK(ios27_21c_sync_destroy(s.sync, s.changed));
    OK(ios27_21c_sync_destroy(s.sync, s.mutex));
    OK(ios27_21c_runtime_destroy(s.sync));
    OK(ios27_21j_schedule_destroy(s.schedule));
    OK(ios27_21j_destroy(s.diag));
    printf("phase21j handoff: %u deterministic cond_wait/signal transitions without missed wakeups; PASS\n",
           CYCLES);
    return 0;
}
