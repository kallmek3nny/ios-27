#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_schedule.h"
#include "ios27/libsystem21j_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j schedule limits:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { ACTORS = IOS27_21J_MAX_ACTORS, STEPS = IOS27_21J_MAX_TURNS };
typedef struct { ios27_21j_schedule *schedule;
                 ios27_21j_diagnostics *diag; uint32_t actor; } argument;
static void *worker(void *value) {
    argument *a = value;
    for (size_t step = a->actor; step < STEPS; step += ACTORS) {
        CHECK(ios27_21j_turn_acquire(a->schedule, a->actor, step, 30000) == 0);
        CHECK(ios27_21j_record(a->diag, IOS27_21J_CHECKPOINT,
                               a->actor, 0, (uint32_t)step) == 0);
        CHECK(ios27_21j_turn_release(a->schedule, a->actor, step) == 0);
    }
    return NULL;
}
int main(void) {
    uint32_t plan[STEPS];
    ios27_21j_schedule *schedule = NULL;
    ios27_21j_diagnostics *diag = NULL;
    ios27_21j_snapshot_data stats;
    ios27_21j_event *events;
    pthread_t threads[ACTORS];
    argument arguments[ACTORS];
    uint64_t first, last;
    size_t count, completed, total, waiting;
    int held;
    for (unsigned i = 0; i < STEPS; ++i) plan[i] = i % ACTORS;
    CHECK(ios27_21j_schedule_create(&schedule, ACTORS, plan, STEPS) == 0);
    CHECK(ios27_21j_create(&diag, 1, 171, 0) == 0);
    for (unsigned actor = 0; actor < ACTORS; ++actor) {
        arguments[actor] = (argument){schedule, diag, actor};
        CHECK(pthread_create(&threads[actor], NULL, worker,
                             &arguments[actor]) == 0);
    }
    for (unsigned actor = 0; actor < ACTORS; ++actor)
        CHECK(pthread_join(threads[actor], NULL) == 0);
    CHECK(ios27_21j_schedule_snapshot(schedule, &completed, &total,
                                      &waiting, &held) == 0);
    CHECK(completed == STEPS && total == STEPS && waiting == 0 && !held);
    CHECK(ios27_21j_snapshot(diag, &stats) == 0);
    CHECK(stats.next_sequence == STEPS && stats.active == 0 &&
          stats.dropped == STEPS - IOS27_21J_TRACE_CAPACITY);
    events = calloc(IOS27_21J_TRACE_CAPACITY, sizeof(*events));
    CHECK(events != NULL);
    CHECK(ios27_21j_export(diag, events, IOS27_21J_TRACE_CAPACITY,
                           &count, &first, &last) == 0);
    CHECK(first == STEPS - IOS27_21J_TRACE_CAPACITY + 1u);
    CHECK(last == STEPS && count == IOS27_21J_TRACE_CAPACITY);
    for (size_t i = 0; i < count; ++i) {
        uint64_t step = first + i - 1u;
        CHECK(events[i].sequence == first + i);
        CHECK(events[i].kind == IOS27_21J_CHECKPOINT &&
              events[i].actor == (uint32_t)(step % ACTORS) &&
              events[i].detail == step);
    }
    free(events);
    CHECK(ios27_21j_schedule_destroy(schedule) == 0);
    CHECK(ios27_21j_destroy(diag) == 0);
    printf("phase21j scheduler limits: %u live actors and %u exact ordered turns; PASS\n",
           (unsigned)ACTORS, (unsigned)STEPS);
    return 0;
}
