#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21c_sync.h"
#include "ios27/libsystem21j_diagnostics.h"
#include "ios27/libsystem21j_schedule.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j timed handoff:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
#define OK(x) CHECK((x) == 0)
enum { CYCLES = 512, STEPS = CYCLES * 3, EXPIRED_EVERY = 8,
       EXPIRED = CYCLES / EXPIRED_EVERY, EXPECTED_EVENTS = CYCLES * 2 + EXPIRED };
typedef struct {
    ios27_21c_runtime *runtime;
    ios27_21c_handle mutex, condition;
    ios27_21j_schedule *schedule;
    ios27_21j_diagnostics *diagnostics;
    unsigned observed, ready;
} state;
static void *waiting_worker(void *value) {
    state *s = value;
    for (unsigned i = 0; i < CYCLES; ++i) {
        OK(ios27_21c_mutex_lock(s->runtime, s->mutex));
        CHECK(s->observed == i);
        if (i % EXPIRED_EVERY == 0) {
            struct timespec past;
            OK(clock_gettime(CLOCK_MONOTONIC, &past) ? errno : 0);
            --past.tv_sec;
            CHECK(ios27_21c_cond_timedwait(s->runtime, s->condition, s->mutex,
                                            IOS27_21C_MONOTONIC,
                                            &past) == ETIMEDOUT);
            OK(ios27_21j_record(s->diagnostics, IOS27_21J_TIMEOUT,
                                 0, ETIMEDOUT, i));
        }
        OK(ios27_21j_turn_acquire(s->schedule, 0, i * 3u, 5000));
        OK(ios27_21j_turn_release(s->schedule, 0, i * 3u));
        while (s->observed == i) {
            struct timespec future;
            OK(clock_gettime(CLOCK_MONOTONIC, &future) ? errno : 0);
            future.tv_sec += 5;
            CHECK(ios27_21c_cond_timedwait(s->runtime, s->condition, s->mutex,
                                            IOS27_21C_MONOTONIC,
                                            &future) == 0);
        }
        CHECK(s->observed == i + 1u);
        ++s->ready;
        OK(ios27_21j_record(s->diagnostics, IOS27_21J_WAIT, 0, 0, i));
        OK(ios27_21j_turn_acquire(s->schedule, 0, i * 3u + 2u, 5000));
        OK(ios27_21j_turn_release(s->schedule, 0, i * 3u + 2u));
        OK(ios27_21c_mutex_unlock(s->runtime, s->mutex));
    }
    return NULL;
}
static void *signaling_worker(void *value) {
    state *s = value;
    for (unsigned i = 0; i < CYCLES; ++i) {
        OK(ios27_21j_turn_acquire(s->schedule, 1, i * 3u + 1u, 5000));
        OK(ios27_21c_mutex_lock(s->runtime, s->mutex));
        CHECK(s->observed == i);
        ++s->observed;
        OK(ios27_21c_cond_signal(s->runtime, s->condition));
        OK(ios27_21j_record(s->diagnostics, IOS27_21J_CHECKPOINT,
                             1, 0, i));
        OK(ios27_21c_mutex_unlock(s->runtime, s->mutex));
        OK(ios27_21j_turn_release(s->schedule, 1, i * 3u + 1u));
    }
    return NULL;
}
int main(void) {
    uint32_t plan[STEPS];
    state s = {0};
    pthread_t waiter, signaler;
    ios27_21j_snapshot_data stats;
    ios27_21j_event events[IOS27_21J_TRACE_CAPACITY];
    ios27_21j_event expected[EXPECTED_EVENTS];
    size_t length = 0, needed = 0;
    uint64_t first, last;
    for (unsigned i = 0; i < CYCLES; ++i) {
        plan[i * 3u] = 0;
        plan[i * 3u + 1u] = 1;
        plan[i * 3u + 2u] = 0;
    }
    OK(ios27_21c_runtime_create(&s.runtime, 2));
    OK(ios27_21c_sync_create(s.runtime, IOS27_21C_ERRORCHECK_MUTEX, &s.mutex));
    OK(ios27_21c_sync_create(s.runtime, IOS27_21C_CONDITION, &s.condition));
    OK(ios27_21j_create(&s.diagnostics, 2, 0x21c10c, 0));
    OK(ios27_21j_schedule_create(&s.schedule, 2, plan, STEPS));
    OK(pthread_create(&waiter, NULL, waiting_worker, &s));
    OK(pthread_create(&signaler, NULL, signaling_worker, &s));
    OK(pthread_join(waiter, NULL));
    OK(pthread_join(signaler, NULL));
    CHECK(s.observed == CYCLES && s.ready == CYCLES);
    OK(ios27_21j_snapshot(s.diagnostics, &stats));
    CHECK(stats.timeouts == EXPIRED && stats.waits == CYCLES);
    CHECK(stats.next_sequence == EXPECTED_EVENTS);
    for (unsigned i = 0; i < CYCLES; ++i) {
        if (i % EXPIRED_EVERY == 0)
            expected[length++] = (ios27_21j_event){0, 0, 0,
                IOS27_21J_TIMEOUT, ETIMEDOUT, i};
        expected[length++] = (ios27_21j_event){0, 0, 1,
            IOS27_21J_CHECKPOINT, 0, i};
        expected[length++] = (ios27_21j_event){0, 0, 0,
            IOS27_21J_WAIT, 0, i};
    }
    CHECK(length == EXPECTED_EVENTS);
    OK(ios27_21j_export(s.diagnostics, events, IOS27_21J_TRACE_CAPACITY,
                          &needed, &first, &last));
    CHECK(needed == IOS27_21J_TRACE_CAPACITY && last == EXPECTED_EVENTS);
    for (size_t i = 0; i < needed; ++i) {
        const ios27_21j_event *entry = &events[i];
        const ios27_21j_event *reference = &expected[first - 1u + i];
        CHECK(entry->sequence == first + i);
        CHECK(entry->actor == reference->actor &&
              entry->kind == reference->kind &&
              entry->result == reference->result &&
              entry->detail == reference->detail);
    }
    OK(ios27_21c_sync_destroy(s.runtime, s.condition));
    OK(ios27_21c_sync_destroy(s.runtime, s.mutex));
    OK(ios27_21c_runtime_destroy(s.runtime));
    OK(ios27_21j_schedule_destroy(s.schedule));
    OK(ios27_21j_destroy(s.diagnostics));
    printf("phase21j timed handoff: %u guaranteed timeouts, %u atomic timed wakeups; PASS\n",
           EXPIRED, CYCLES);
    return 0;
}
