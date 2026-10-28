#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_schedule.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct ios27_21j_schedule {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    uint32_t *plan;
    uint32_t actors, holder_actor;
    pthread_t holder;
    size_t total, completed, waiting;
    int held;
};
static int deadline_after(uint64_t ms, struct timespec *deadline) {
    uint64_t seconds;
    long nanoseconds;
    if (!deadline || ms > 60000u) return EINVAL;
    if (clock_gettime(CLOCK_MONOTONIC, deadline)) return errno;
    seconds = ms / 1000u;
    nanoseconds = (long)((ms % 1000u) * 1000000u);
    deadline->tv_sec += (time_t)seconds;
    deadline->tv_nsec += nanoseconds;
    if (deadline->tv_nsec >= 1000000000L) {
        ++deadline->tv_sec;
        deadline->tv_nsec -= 1000000000L;
    }
    return 0;
}
int ios27_21j_schedule_create(ios27_21j_schedule **out, uint32_t actors,
                               const uint32_t *plan, size_t steps) {
    ios27_21j_schedule *s;
    pthread_condattr_t attr;
    int err;
    if (!out || !plan || !actors || actors > IOS27_21J_MAX_ACTORS ||
        !steps || steps > IOS27_21J_MAX_TURNS) return EINVAL;
    *out = NULL;
    for (size_t i = 0; i < steps; ++i)
        if (plan[i] >= actors) return EINVAL;
    s = calloc(1, sizeof(*s));
    if (!s) return ENOMEM;
    s->plan = malloc(steps * sizeof(*s->plan));
    if (!s->plan) { free(s); return ENOMEM; }
    memcpy(s->plan, plan, steps * sizeof(*plan));
    s->actors = actors;
    s->total = steps;
    err = pthread_mutex_init(&s->mutex, NULL);
    if (err) { free(s->plan); free(s); return err; }
    err = pthread_condattr_init(&attr);
    if (!err) {
        err = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        if (!err) err = pthread_cond_init(&s->changed, &attr);
        (void)pthread_condattr_destroy(&attr);
    }
    if (err) {
        (void)pthread_mutex_destroy(&s->mutex);
        free(s->plan);
        free(s);
        return err;
    }
    *out = s;
    return 0;
}
int ios27_21j_schedule_destroy(ios27_21j_schedule *s) {
    if (!s) return EINVAL;
    (void)pthread_mutex_lock(&s->mutex);
    if (s->held || s->waiting) {
        (void)pthread_mutex_unlock(&s->mutex);
        return EBUSY;
    }
    (void)pthread_mutex_unlock(&s->mutex);
    int err = pthread_cond_destroy(&s->changed);
    if (err) return err;
    err = pthread_mutex_destroy(&s->mutex);
    if (err) return err;
    free(s->plan);
    free(s);
    return 0;
}
/* A waiting thread canceled by POSIX must release the scheduler guard and
   its waiter reference; unlike native async cancellation while holding a
   turn, this path can be made safe with cleanup handlers. */
typedef struct { ios27_21j_schedule *s; } waiting_context;
static void waiting_cleanup(void *value) {
    waiting_context *context = value;
    --context->s->waiting;
    (void)pthread_mutex_unlock(&context->s->mutex);
}
int ios27_21j_turn_acquire(ios27_21j_schedule *s, uint32_t actor,
                            size_t step, uint64_t timeout_ms) {
    struct timespec deadline;
    int err, result = 0;
    if (!s || step >= s->total || actor >= s->actors ||
        s->plan[step] != actor) return EINVAL;
    err = deadline_after(timeout_ms, &deadline);
    if (err) return err;
    (void)pthread_mutex_lock(&s->mutex);
    if (s->held && pthread_equal(s->holder, pthread_self())) {
        (void)pthread_mutex_unlock(&s->mutex);
        return EDEADLK;
    }
    if (step < s->completed) result = EALREADY;
    while (!result && (step > s->completed || s->held)) {
        waiting_context state = {s};
        ++s->waiting;
        pthread_cleanup_push(waiting_cleanup, &state);
        err = pthread_cond_timedwait(&s->changed, &s->mutex, &deadline);
        pthread_cleanup_pop(0);
        --s->waiting;
        if (err) result = err;
        else if (step < s->completed) result = EALREADY;
    }
    if (!result) {
        s->held = 1;
        s->holder_actor = actor;
        s->holder = pthread_self();
    }
    (void)pthread_mutex_unlock(&s->mutex);
    return result;
}
int ios27_21j_turn_release(ios27_21j_schedule *s, uint32_t actor,
                            size_t step) {
    int result;
    if (!s || actor >= s->actors || step >= s->total) return EINVAL;
    (void)pthread_mutex_lock(&s->mutex);
    if (!s->held || !pthread_equal(s->holder, pthread_self()) ||
        s->holder_actor != actor || s->completed != step) result = EPERM;
    else {
        s->held = 0;
        ++s->completed;
        result = pthread_cond_broadcast(&s->changed);
    }
    (void)pthread_mutex_unlock(&s->mutex);
    return result;
}
int ios27_21j_schedule_snapshot(ios27_21j_schedule *s,
                                 size_t *completed, size_t *total,
                                 size_t *waiting, int *held) {
    if (!s) return EINVAL;
    (void)pthread_mutex_lock(&s->mutex);
    if (completed) *completed = s->completed;
    if (total) *total = s->total;
    if (waiting) *waiting = s->waiting;
    if (held) *held = s->held;
    (void)pthread_mutex_unlock(&s->mutex);
    return 0;
}
