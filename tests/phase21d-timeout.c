#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21d_rw_once.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(call) do { int rc_ = (call); if (rc_) { \
    fprintf(stderr, "%s:%d: %s => %d\n", __FILE__, __LINE__, #call, rc_); abort(); \
} } while (0)
#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: failed %s\n", __FILE__, __LINE__, #condition); abort(); \
} } while (0)

typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    int is_writer;
    long timeout_ms;
    int result;
    atomic_int admitted;
} timed_job;

static int64_t realtime_ns(long delta_ms) {
    struct timespec ts;
    CHECK(clock_gettime(CLOCK_REALTIME, &ts));
    return (int64_t)ts.tv_sec * INT64_C(1000000000) +
           (int64_t)ts.tv_nsec + (int64_t)delta_ms * INT64_C(1000000);
}

static void *timed_worker(void *opaque) {
    timed_job *job = opaque;
    int64_t until = realtime_ns(job->timeout_ms);
    job->result = job->is_writer ? ios27_21d_timedwrlock(job->rt, job->lock, until) :
                                   ios27_21d_timedrdlock(job->rt, job->lock, until);
    if (!job->result) {
        atomic_store_explicit(&job->admitted, 1, memory_order_release);
        CHECK(ios27_21d_unlock(job->rt, job->lock));
    }
    return NULL;
}

/* Verify the upper bound of the signed 64-bit Darwin bridge timestamp
   survives host timespec conversion without overflow on 64-bit hosts. */
static void *far_future_worker(void *opaque) {
    timed_job *job = opaque;
    job->result = ios27_21d_timedrdlock(job->rt, job->lock, INT64_MAX);
    if (!job->result) CHECK(ios27_21d_unlock(job->rt, job->lock));
    return NULL;
}
static void wait_queued(ios27_21d_runtime *rt, size_t target) {
    size_t count = 0;
    for (unsigned i = 0; i < 3000000u; ++i) {
        CHECK(ios27_21d_usage(rt, NULL, &count, NULL));
        if (count >= target) return;
        (void)sched_yield();
    }
    REQUIRE(count >= target);
}

static void verify_timeout_and_stale(ios27_21d_runtime *rt, ios27_21d_handle lock) {
    pthread_t thread;
    timed_job job = {.rt = rt, .lock = lock, .is_writer = 0, .timeout_ms = 25};
    CHECK(ios27_21d_wrlock(rt, lock));
    REQUIRE(ios27_21d_timedrdlock(rt, lock, -1) == EINVAL);
    REQUIRE(ios27_21d_timedwrlock(rt, lock, -5) == EINVAL);
    REQUIRE(ios27_21d_timedrdlock(rt, lock, realtime_ns(100)) == EDEADLK);
    REQUIRE(ios27_21d_timedwrlock(rt, lock, realtime_ns(100)) == EDEADLK);
    CHECK(pthread_create(&thread, NULL, timed_worker, &job));
    CHECK(pthread_join(thread, NULL));
    REQUIRE(job.result == ETIMEDOUT);
    REQUIRE(!atomic_load(&job.admitted));
    size_t queued = 3, holders = 0;
    CHECK(ios27_21d_usage(rt, NULL, &queued, &holders));
    REQUIRE(queued == 0 && holders == 1);
    CHECK(ios27_21d_unlock(rt, lock));
    REQUIRE(ios27_21d_timedrdlock(rt, lock, 0) == 0);
    CHECK(ios27_21d_unlock(rt, lock));
}

static void verify_far_future_deadline(ios27_21d_runtime *rt,
                                       ios27_21d_handle lock) {
    pthread_t thread;
    timed_job job = {.rt = rt, .lock = lock, .result = -1};
    CHECK(ios27_21d_wrlock(rt, lock));
    CHECK(pthread_create(&thread, NULL, far_future_worker, &job));
    wait_queued(rt, 1);
    CHECK(ios27_21d_unlock(rt, lock));
    CHECK(pthread_join(thread, NULL));
    REQUIRE(job.result == 0);
}

static void verify_timeout_removes_head(ios27_21d_runtime *rt, ios27_21d_handle lock) {
    pthread_t first, second;
    timed_job writer = {.rt = rt, .lock = lock, .is_writer = 1, .timeout_ms = 1500};
    timed_job reader = {.rt = rt, .lock = lock, .is_writer = 0, .timeout_ms = 5000};
    CHECK(ios27_21d_rdlock(rt, lock));
    CHECK(pthread_create(&first, NULL, timed_worker, &writer));
    wait_queued(rt, 1);
    CHECK(pthread_create(&second, NULL, timed_worker, &reader));
    wait_queued(rt, 2);
    CHECK(pthread_join(first, NULL));
    REQUIRE(writer.result == ETIMEDOUT);
    /* First waiter timed out, so a previously queued reader can be admitted
       even while the original reader still owns its lock. */
    CHECK(pthread_join(second, NULL));
    REQUIRE(reader.result == 0 && atomic_load(&reader.admitted) == 1);
    CHECK(ios27_21d_unlock(rt, lock));
}

/* Mixed timeout and admission races. A head waiter may time out while other
   readers/writers contend; no wait entry survives its owning stack frame. */
typedef struct {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock;
    int try_result;
    int timed_result;
} probe_arg;
static void *probe_reader_barge(void *opaque) {
    probe_arg *p = opaque;
    p->try_result = ios27_21d_tryrdlock(p->rt, p->lock);
    p->timed_result = ios27_21d_timedrdlock(p->rt, p->lock, realtime_ns(-10));
    return NULL;
}

static void verify_writer_anti_barging(ios27_21d_runtime *rt, ios27_21d_handle lock) {
    pthread_t queued, probing;
    timed_job writer = {.rt = rt, .lock = lock, .is_writer = 1, .timeout_ms = 5000};
    probe_arg probe = {.rt = rt, .lock = lock, .try_result = -1, .timed_result = -1};
    CHECK(ios27_21d_rdlock(rt, lock));
    CHECK(pthread_create(&queued, NULL, timed_worker, &writer));
    wait_queued(rt, 1);
    /* A newcomer must not slip in front of the waiting writer, even though
       the lock currently has only shared reader owners. */
    CHECK(pthread_create(&probing, NULL, probe_reader_barge, &probe));
    CHECK(pthread_join(probing, NULL));
    REQUIRE(probe.try_result == EBUSY);
    REQUIRE(probe.timed_result == ETIMEDOUT);
    size_t queued_count = 0;
    CHECK(ios27_21d_usage(rt, NULL, &queued_count, NULL));
    REQUIRE(queued_count == 1);
    CHECK(ios27_21d_unlock(rt, lock));
    CHECK(pthread_join(queued, NULL));
    REQUIRE(writer.result == 0 && atomic_load(&writer.admitted) == 1);
}

static void verify_race_churn(ios27_21d_runtime *rt, ios27_21d_handle lock) {
    for (int round = 0; round < 320; ++round) {
        pthread_t thread;
        timed_job job = {.rt = rt, .lock = lock, .is_writer = round % 2,
                         .timeout_ms = round % 5 == 0 ? 1 : 5000};
        CHECK(ios27_21d_wrlock(rt, lock));
        CHECK(pthread_create(&thread, NULL, timed_worker, &job));
        if (round % 5 == 0) {
            /* Deliberately keep the writer locked across the deadline. */
            CHECK(pthread_join(thread, NULL));
            REQUIRE(job.result == ETIMEDOUT);
            CHECK(ios27_21d_unlock(rt, lock));
        } else {
            wait_queued(rt, 1);
            CHECK(ios27_21d_unlock(rt, lock));
            CHECK(pthread_join(thread, NULL));
            REQUIRE(job.result == 0);
        }
        size_t waiting = 0, holders = 0;
        CHECK(ios27_21d_usage(rt, NULL, &waiting, &holders));
        REQUIRE(waiting == 0 && holders == 0);
    }
}

int main(void) {
    ios27_21d_runtime *rt;
    ios27_21d_handle lock, stale;
    CHECK(ios27_21d_runtime_create(&rt, 1));
    CHECK(ios27_21d_create(rt, IOS27_21D_RWLOCK, &lock));
    verify_timeout_and_stale(rt, lock);
    verify_far_future_deadline(rt, lock);
    verify_timeout_removes_head(rt, lock);
    verify_writer_anti_barging(rt, lock);
    verify_race_churn(rt, lock);
    stale = lock;
    CHECK(ios27_21d_destroy(rt, lock));
    REQUIRE(ios27_21d_timedrdlock(rt, stale, realtime_ns(100)) == EINVAL);
    CHECK(ios27_21d_runtime_destroy(rt));
    puts("phase21d: timed lock races, FIFO timeout cleanup, and 320 churn iterations passed");
    return 0;
}
