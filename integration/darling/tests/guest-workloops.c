/* Real native workloop callbacks, debounce/ownership, timer identity, and
 * blocking/wake-before-wait synchronization. Also built for iOS Simulator. */
#define PRIVATE 1
#define __PTHREAD_EXPOSE_INTERNALS__ 1
#include <sys/event.h>
#include <pthread.h>
#include <pthread/workqueue_private.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static unsigned checks, errors, callbacks, calls[8];
static bool waiter_ready, waiter_done;
static int waiter_result;
#define BASE 0x1000ull
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s errno=%d\n", __LINE__, #c, errno); exit(1); } } while (0)

static struct timespec deadline(long ms) {
    struct timespec value;
    CHECK(clock_gettime(CLOCK_REALTIME, &value) == 0);
    value.tv_sec += ms / 1000;
    value.tv_nsec += ms % 1000 * 1000000;
    if (value.tv_nsec >= 1000000000) { ++value.tv_sec; value.tv_nsec -= 1000000000; }
    return value;
}

static struct kevent_qos_s request(uint16_t flags, uint32_t command) {
    return (struct kevent_qos_s){ .ident = 17, .filter = EVFILT_WORKLOOP,
        .flags = flags, .fflags = command,
        .qos = (int32_t)_pthread_qos_class_encode(QOS_CLASS_DEFAULT, 0, 0), .udata = 55 };
}

static int update(uint64_t id, struct kevent_qos_s *change, struct kevent_qos_s *receipt) {
    return kevent_id(id, change, 1, receipt, receipt ? 1 : 0, NULL, NULL,
                     KEVENT_FLAG_WORKLOOP | KEVENT_FLAG_ERROR_EVENTS);
}

static void worker(pthread_priority_t priority) {
    (void)priority;
    pthread_mutex_lock(&lock); ++errors; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&lock);
}
static void kevents(void **events, int *count) {
    (void)events; *count = 0;
    pthread_mutex_lock(&lock); ++errors; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&lock);
}

static void workloop(uint64_t *id, void **buffer, int *count) {
    struct kevent_qos_s *events = *buffer;
    pthread_mutex_lock(&lock);
    if (id != (uint64_t *)events - 1 || *id <= BASE || *id >= BASE + 8 || *count != 1 ||
            (events[0].flags & EV_ERROR) || qos_class_self() != QOS_CLASS_DEFAULT) ++errors;
    else {
        unsigned index = (unsigned)(*id - BASE);
        if (index == 2 || index == 3) {
            if (events[0].filter != EVFILT_TIMER || events[0].ident != 1 || events[0].udata != 7) ++errors;
        } else if (events[0].filter != EVFILT_WORKLOOP || events[0].udata != 55) ++errors;
        ++calls[index];
    }
    ++callbacks;
    *count = 0;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
}

static void wait_calls(unsigned index, unsigned value) {
    struct timespec end = deadline(2000);
    CHECK(pthread_mutex_lock(&lock) == 0);
    while (calls[index] < value && !errors) CHECK(pthread_cond_timedwait(&changed, &lock, &end) == 0);
    CHECK(calls[index] == value && errors == 0);
    CHECK(pthread_mutex_unlock(&lock) == 0);
}

static void unchanged(unsigned index, unsigned value) {
    struct timespec end = deadline(100);
    CHECK(pthread_mutex_lock(&lock) == 0);
    while (calls[index] == value && !errors) {
        int result = pthread_cond_timedwait(&changed, &lock, &end);
        CHECK(result == 0 || result == ETIMEDOUT);
        if (result == ETIMEDOUT) break;
    }
    CHECK(calls[index] == value && errors == 0);
    CHECK(pthread_mutex_unlock(&lock) == 0);
}

static void *waiter(void *unused) {
    (void)unused;
    struct kevent_qos_s change = request(EV_ADD, NOTE_WL_SYNC_WAIT);
    pthread_mutex_lock(&lock); waiter_ready = true; pthread_cond_broadcast(&changed); pthread_mutex_unlock(&lock);
    int result = update(BASE + 6, &change, NULL);
    pthread_mutex_lock(&lock);
    waiter_result = result; waiter_done = true;
    pthread_cond_broadcast(&changed); pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void) {
    alarm(12);
    CHECK(_pthread_workqueue_init_with_workloop(worker, kevents, workloop, 0, 0) == 0);
    struct kevent_qos_s change = request(EV_ADD | EV_CLEAR, NOTE_WL_THREAD_REQUEST), receipt;
    CHECK(update(BASE + 1, &change, NULL) == 0);
    wait_calls(1, 1);
    unchanged(1, 1);
    change.flags = EV_ENABLE;
    CHECK(update(BASE + 1, &change, NULL) == 0);
    wait_calls(1, 2);
    change.flags = EV_DELETE;
    CHECK(update(BASE + 1, &change, NULL) == 0);

    change = (struct kevent_qos_s){ .ident = 1, .filter = EVFILT_TIMER, .flags = EV_ADD | EV_ONESHOT,
        .qos = (int32_t)_pthread_qos_class_encode(QOS_CLASS_DEFAULT, 0, 0), .data = 20, .udata = 7 };
    CHECK(update(BASE + 2, &change, NULL) == 0);
    CHECK(update(BASE + 3, &change, NULL) == 0);
    wait_calls(2, 1); wait_calls(3, 1);

    uint64_t state = pthread_mach_thread_np(pthread_self());
    change = request(EV_ADD | EV_CLEAR, NOTE_WL_THREAD_REQUEST | NOTE_WL_DISCOVER_OWNER);
    change.ext[EV_EXTIDX_WL_ADDR] = (uintptr_t)&state;
    change.ext[EV_EXTIDX_WL_MASK] = UINT64_MAX;
    change.ext[EV_EXTIDX_WL_VALUE] = state;
    CHECK(update(BASE + 4, &change, NULL) == 0);
    unchanged(4, 0);
    change.flags = EV_ENABLE;
    change.fflags = NOTE_WL_THREAD_REQUEST | NOTE_WL_END_OWNERSHIP;
    CHECK(update(BASE + 4, &change, NULL) == 0);
    wait_calls(4, 1);
    change.flags = EV_DELETE;
    CHECK(update(BASE + 4, &change, NULL) == 0);

    change.flags = EV_ADD;
    change.fflags = NOTE_WL_THREAD_REQUEST;
    change.ext[EV_EXTIDX_WL_VALUE] = state ^ 0x100;
    CHECK(update(BASE + 5, &change, &receipt) == 1 && receipt.data == ESTALE);
    CHECK((receipt.flags & EV_ERROR) && receipt.ext[EV_EXTIDX_WL_VALUE] == state);
    change.fflags |= NOTE_WL_IGNORE_ESTALE;
    CHECK(update(BASE + 5, &change, &receipt) == 0);

    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, waiter, NULL) == 0);
    struct timespec end = deadline(2000);
    CHECK(pthread_mutex_lock(&lock) == 0);
    while (!waiter_ready) CHECK(pthread_cond_timedwait(&changed, &lock, &end) == 0);
    end = deadline(100);
    while (!waiter_done) {
        int result = pthread_cond_timedwait(&changed, &lock, &end);
        CHECK(result == 0 || result == ETIMEDOUT);
        if (result == ETIMEDOUT) break;
    }
    CHECK(!waiter_done);
    CHECK(pthread_mutex_unlock(&lock) == 0);
    change = request(EV_ADD, NOTE_WL_SYNC_WAKE);
    CHECK(update(BASE + 6, &change, NULL) == 0);
    CHECK(pthread_join(thread, NULL) == 0 && waiter_done && waiter_result == 0);
    change.flags = EV_DELETE;
    CHECK(update(BASE + 6, &change, NULL) == 0);

    change = request(EV_ADD, NOTE_WL_SYNC_WAKE);
    CHECK(update(BASE + 7, &change, NULL) == 0);
    change.flags = 0; change.fflags = NOTE_WL_SYNC_WAIT;
    CHECK(update(BASE + 7, &change, NULL) == 0);
    change.flags = EV_DELETE;
    CHECK(update(BASE + 7, &change, NULL) == 0);
    CHECK(callbacks == 5 && errors == 0);
    printf("DARWIN_WORKLOOPS_OK callbacks=%u checks=%u feature_mask=0x%x\n", callbacks, checks, _pthread_workqueue_supported());
    return 0;
}
