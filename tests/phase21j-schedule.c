#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_schedule.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j schedule:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { ACTORS = 8, STEPS = 1024, EPOCHS = 16 };
typedef struct {
    ios27_21j_schedule *scheduler;
    const uint32_t *plan;
    unsigned actor;
    size_t *next;
} step_arg;
static void *steps_worker(void *value) {
    step_arg *a = value;
    for (size_t i = 0; i < STEPS; ++i) {
        if (a->plan[i] != a->actor) continue;
        CHECK(ios27_21j_turn_acquire(a->scheduler, a->actor, i, 5000) == 0);
        CHECK(*a->next == i);
        ++*a->next;
        CHECK(ios27_21j_turn_release(a->scheduler, a->actor, i) == 0);
    }
    return NULL;
}
static uint64_t seeded(uint64_t *seed) {
    *seed += UINT64_C(0x9e3779b97f4a7c15);
    uint64_t z = *seed;
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}
static void full_schedules(void) {
    uint64_t seed = UINT64_C(0x21a1715aa);
    for (unsigned epoch = 0; epoch < EPOCHS; ++epoch) {
        uint32_t plan[STEPS];
        ios27_21j_schedule *s = NULL;
        pthread_t threads[ACTORS];
        step_arg args[ACTORS];
        size_t completed = 0, total = 0, waiting = 0, next = 0;
        int held = 0;
        for (unsigned i = 0; i < STEPS; ++i)
            plan[i] = (uint32_t)(seeded(&seed) % ACTORS);
        CHECK(ios27_21j_schedule_create(&s, ACTORS, plan, STEPS) == 0);
        for (unsigned actor = 0; actor < ACTORS; ++actor) {
            args[actor] = (step_arg){s, plan, actor, &next};
            CHECK(pthread_create(&threads[actor], NULL,
                                 steps_worker, &args[actor]) == 0);
        }
        for (unsigned actor = 0; actor < ACTORS; ++actor)
            CHECK(pthread_join(threads[actor], NULL) == 0);
        CHECK(next == STEPS);
        CHECK(ios27_21j_schedule_snapshot(s, &completed, &total,
                                           &waiting, &held) == 0);
        CHECK(completed == STEPS && total == STEPS);
        CHECK(waiting == 0 && held == 0);
        CHECK(ios27_21j_schedule_destroy(s) == 0);
    }
}
typedef struct { ios27_21j_schedule *scheduler; int result; } peer;
static void *foreign_release(void *value) {
    peer *p = value;
    p->result = ios27_21j_turn_release(p->scheduler, 0, 0);
    return NULL;
}
static void invalid_and_reentrant(void) {
    const uint32_t plan[] = {0, 1, 0};
    ios27_21j_schedule *s = NULL;
    peer p;
    pthread_t thread;
    size_t completed = 0;
    CHECK(ios27_21j_schedule_create(NULL, 2, plan, 3) == EINVAL);
    CHECK(ios27_21j_schedule_create(&s, 0, plan, 3) == EINVAL);
    CHECK(ios27_21j_schedule_create(&s, 2, plan, 0) == EINVAL);
    CHECK(ios27_21j_schedule_create(&s, 2, plan, 3) == 0);
    CHECK(ios27_21j_turn_acquire(s, 1, 0, 5) == EINVAL);
    CHECK(ios27_21j_turn_acquire(s, 0, 3, 5) == EINVAL);
    CHECK(ios27_21j_turn_acquire(s, 1, 1, 0) == ETIMEDOUT);
    CHECK(ios27_21j_turn_acquire(s, 0, 0, 1000) == 0);
    CHECK(ios27_21j_turn_acquire(s, 0, 0, 1000) == EDEADLK);
    CHECK(ios27_21j_schedule_destroy(s) == EBUSY);
    p = (peer){s, -1};
    CHECK(pthread_create(&thread, NULL, foreign_release, &p) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(p.result == EPERM);
    CHECK(ios27_21j_turn_release(s, 1, 0) == EPERM);
    CHECK(ios27_21j_turn_release(s, 0, 0) == 0);
    CHECK(ios27_21j_turn_acquire(s, 0, 0, 0) == EALREADY);
    CHECK(ios27_21j_turn_acquire(s, 1, 1, 1000) == 0);
    CHECK(ios27_21j_turn_release(s, 1, 1) == 0);
    CHECK(ios27_21j_turn_acquire(s, 0, 2, 1000) == 0);
    CHECK(ios27_21j_turn_release(s, 0, 2) == 0);
    CHECK(ios27_21j_schedule_snapshot(s, &completed, NULL, NULL, NULL) == 0);
    CHECK(completed == 3);
    CHECK(ios27_21j_schedule_destroy(s) == 0);
}
static void *cancelled_waiter(void *value) {
    ios27_21j_schedule *s = value;
    int rc = ios27_21j_turn_acquire(s, 1, 1, 5000);
    CHECK(rc == ETIMEDOUT);
    return NULL;
}
static void cancel_while_queued(void) {
    const uint32_t plan[] = {0, 1};
    ios27_21j_schedule *s = NULL;
    pthread_t thread;
    size_t waiting = 0;
    void *result = NULL;
    CHECK(ios27_21j_schedule_create(&s, 2, plan, 2) == 0);
    CHECK(pthread_create(&thread, NULL, cancelled_waiter, s) == 0);
    for (unsigned spin = 0; spin < 500000u; ++spin) {
        CHECK(ios27_21j_schedule_snapshot(s, NULL, NULL, &waiting, NULL) == 0);
        if (waiting == 1) break;
        sched_yield();
    }
    CHECK(waiting == 1);
    CHECK(ios27_21j_schedule_destroy(s) == EBUSY);
    CHECK(pthread_cancel(thread) == 0);
    CHECK(pthread_join(thread, &result) == 0);
    CHECK(result == PTHREAD_CANCELED);
    CHECK(ios27_21j_schedule_snapshot(s, NULL, NULL, &waiting, NULL) == 0);
    CHECK(waiting == 0);
    CHECK(ios27_21j_schedule_destroy(s) == 0);
}
int main(void) {
    invalid_and_reentrant();
    cancel_while_queued();
    full_schedules();
    printf("phase21j schedules: %u exact seeded turns, cancellation and stale turn handling; PASS\n",
           STEPS * EPOCHS);
    return 0;
}
