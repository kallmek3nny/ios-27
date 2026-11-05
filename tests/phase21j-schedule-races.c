#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_schedule.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j schedule-races:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { CANCEL_CASES = 256, DUPLICATE_CASES = 256, TIMEOUT_CASES = 512 };
typedef struct {
    ios27_21j_schedule *schedule;
    uint64_t timeout_ms;
    int result;
} waiter_arg;
static void *waiter(void *value) {
    waiter_arg *a = value;
    a->result = ios27_21j_turn_acquire(a->schedule, 1, 1, a->timeout_ms);
    if (a->result == 0)
        CHECK(ios27_21j_turn_release(a->schedule, 1, 1) == 0);
    return NULL;
}
static void until_waiting(ios27_21j_schedule *s, size_t target) {
    size_t waiting = 0;
    for (unsigned spin = 0; spin < 500000u; ++spin) {
        CHECK(ios27_21j_schedule_snapshot(s, NULL, NULL, &waiting, NULL) == 0);
        if (waiting == target) return;
        sched_yield();
    }
    CHECK(0 && "missing expected waiter before race");
}
static void cancelled_queues(void) {
    const uint32_t plan[] = {0, 1};
    for (unsigned round = 0; round < CANCEL_CASES; ++round) {
        ios27_21j_schedule *s = NULL;
        pthread_t thread;
        waiter_arg arg;
        void *out = NULL;
        size_t waiters = 9, done = 9;
        int held = 0;
        CHECK(ios27_21j_schedule_create(&s, 2, plan, 2) == 0);
        CHECK(ios27_21j_turn_acquire(s, 0, 0, 5000) == 0);
        arg = (waiter_arg){s, 30000, -1};
        CHECK(pthread_create(&thread, NULL, waiter, &arg) == 0);
        until_waiting(s, 1);
        CHECK(ios27_21j_schedule_destroy(s) == EBUSY);
        CHECK(pthread_cancel(thread) == 0);
        CHECK(pthread_join(thread, &out) == 0);
        CHECK(out == PTHREAD_CANCELED);
        CHECK(ios27_21j_schedule_snapshot(s, &done, NULL, &waiters, &held) == 0);
        CHECK(done == 0 && waiters == 0 && held == 1);
        CHECK(ios27_21j_turn_release(s, 0, 0) == 0);
        CHECK(ios27_21j_turn_acquire(s, 1, 1, 2000) == 0);
        CHECK(ios27_21j_turn_release(s, 1, 1) == 0);
        CHECK(ios27_21j_schedule_destroy(s) == 0);
    }
}
static void duplicate_waiters(void) {
    const uint32_t plan[] = {0, 1};
    for (unsigned round = 0; round < DUPLICATE_CASES; ++round) {
        ios27_21j_schedule *s = NULL;
        pthread_t a, b;
        waiter_arg first, second;
        size_t done = 0, waiters = 0;
        CHECK(ios27_21j_schedule_create(&s, 2, plan, 2) == 0);
        CHECK(ios27_21j_turn_acquire(s, 0, 0, 5000) == 0);
        first = (waiter_arg){s, 5000, -1};
        second = (waiter_arg){s, 5000, -1};
        CHECK(pthread_create(&a, NULL, waiter, &first) == 0);
        CHECK(pthread_create(&b, NULL, waiter, &second) == 0);
        until_waiting(s, 2);
        CHECK(ios27_21j_turn_release(s, 0, 0) == 0);
        CHECK(pthread_join(a, NULL) == 0);
        CHECK(pthread_join(b, NULL) == 0);
        CHECK((first.result == 0 && second.result == EALREADY) ||
              (first.result == EALREADY && second.result == 0));
        CHECK(ios27_21j_schedule_snapshot(s, &done, NULL, &waiters, NULL) == 0);
        CHECK(done == 2 && waiters == 0);
        CHECK(ios27_21j_schedule_destroy(s) == 0);
    }
}
typedef struct {
    ios27_21j_schedule *s;
    pthread_barrier_t *start;
    int result;
} competing_arg;
static void *competing(void *value) {
    competing_arg *a = value;
    int rc = pthread_barrier_wait(a->start);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    a->result = ios27_21j_turn_acquire(a->s, 1, 1, 1);
    if (!a->result)
        CHECK(ios27_21j_turn_release(a->s, 1, 1) == 0);
    return NULL;
}
static void timeout_release_race(void) {
    const uint32_t plan[] = {0, 1};
    unsigned timeouts = 0, acquired = 0;
    for (unsigned round = 0; round < TIMEOUT_CASES; ++round) {
        ios27_21j_schedule *s = NULL;
        pthread_barrier_t start;
        competing_arg arg;
        pthread_t thread;
        size_t steps = 0, pending = 0;
        CHECK(ios27_21j_schedule_create(&s, 2, plan, 2) == 0);
        CHECK(ios27_21j_turn_acquire(s, 0, 0, 5000) == 0);
        CHECK(pthread_barrier_init(&start, NULL, 2) == 0);
        arg = (competing_arg){s, &start, -1};
        CHECK(pthread_create(&thread, NULL, competing, &arg) == 0);
        int rc = pthread_barrier_wait(&start);
        CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
        CHECK(ios27_21j_turn_release(s, 0, 0) == 0);
        CHECK(pthread_join(thread, NULL) == 0);
        CHECK(arg.result == 0 || arg.result == ETIMEDOUT);
        if (arg.result == ETIMEDOUT) {
            ++timeouts;
            CHECK(ios27_21j_turn_acquire(s, 1, 1, 5000) == 0);
            CHECK(ios27_21j_turn_release(s, 1, 1) == 0);
        } else ++acquired;
        CHECK(ios27_21j_schedule_snapshot(s, &steps, NULL, &pending, NULL) == 0);
        CHECK(steps == 2 && pending == 0);
        CHECK(pthread_barrier_destroy(&start) == 0);
        CHECK(ios27_21j_schedule_destroy(s) == 0);
    }
    CHECK(timeouts + acquired == TIMEOUT_CASES);
    printf("phase21j timeout-release race: %u acquired, %u timed out; PASS\n",
           acquired, timeouts);
}
int main(void) {
    cancelled_queues();
    duplicate_waiters();
    timeout_release_race();
    printf("phase21j schedule races: %u cancellation cleanups, %u duplicate wins, %u timeout/release races; PASS\n",
           CANCEL_CASES, DUPLICATE_CASES, TIMEOUT_CASES);
    return 0;
}
