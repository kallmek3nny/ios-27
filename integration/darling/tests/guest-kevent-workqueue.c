/* Exercise real asynchronous workqueue delivery, concurrent callbacks,
 * event-manager serialization, and changes returned by a callback. */
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
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static unsigned checks, callbacks, errors, managers_started, managers_running, managers_done, rearmed;
static bool blocker_started, second_done, ordinary_seen, release_managers;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s errno=%d\n", __LINE__, #c, errno); exit(1); } } while (0)

static struct timespec deadline(long milliseconds) {
    struct timespec result;
    CHECK(clock_gettime(CLOCK_REALTIME, &result) == 0);
    result.tv_sec += milliseconds / 1000;
    result.tv_nsec += milliseconds % 1000 * 1000000;
    if (result.tv_nsec >= 1000000000) { ++result.tv_sec; result.tv_nsec -= 1000000000; }
    return result;
}

static struct kevent_qos_s note(uint64_t id, uint16_t flags, uint32_t trigger) {
    return (struct kevent_qos_s){ .ident = id, .filter = EVFILT_USER, .flags = flags,
        .qos = (int32_t)_pthread_qos_class_encode(QOS_CLASS_DEFAULT, 0,
            id == 3 || id == 4 ? _PTHREAD_PRIORITY_EVENT_MANAGER_FLAG : 0),
        .udata = id, .fflags = trigger };
}

static void submit(uint64_t id, uint16_t flags, uint32_t trigger) {
    struct kevent_qos_s input = note(id, flags, trigger), receipt;
    CHECK(kevent_qos(-1, &input, 1, &receipt, 1, NULL, NULL,
                     KEVENT_FLAG_WORKQ | KEVENT_FLAG_ERROR_EVENTS) == 0);
}

static void unexpected_worker(pthread_priority_t priority) {
    (void)priority;
    pthread_mutex_lock(&lock);
    ++errors;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
}

static void events(void **buffer, int *count) {
    struct kevent_qos_s *list = *buffer;
    bool again = false;
    pthread_mutex_lock(&lock);
    if (*count != 1 || list[0].flags & EV_ERROR) ++errors;
    uint64_t id = list[0].udata;
    if (id == 1) {
        blocker_started = true;
        pthread_cond_broadcast(&changed);
        while (!second_done) pthread_cond_wait(&changed, &lock);
    } else if (id == 2) {
        second_done = true;
    } else if (id == 3 || id == 4) {
        ++managers_started;
        if (++managers_running != 1) ++errors;
        pthread_cond_broadcast(&changed);
        while (!release_managers) pthread_cond_wait(&changed, &lock);
        --managers_running;
        ++managers_done;
    } else if (id == 5) {
        ordinary_seen = true;
    } else if (id == 6) {
        again = ++rearmed == 1;
    } else ++errors;
    ++callbacks;
    if (again) {
        list[0] = note(6, EV_ADD | EV_ONESHOT, NOTE_TRIGGER);
        *count = 1;
    } else *count = 0;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
}

int main(void) {
    alarm(12);
    CHECK(_pthread_workqueue_init_with_kevent(unexpected_worker, events, 0, 0) == 0);
    CHECK(kevent_qos(-1, NULL, 0, NULL, 0, NULL, NULL,
                     KEVENT_FLAG_WORKQ | KEVENT_FLAG_ERROR_EVENTS) == 0);
    for (uint64_t id = 1; id <= 5; ++id) submit(id, EV_ADD | EV_CLEAR, 0);

    // A callback waiting for another callback must not block the poller.
    submit(1, 0, NOTE_TRIGGER);
    struct timespec limit = deadline(2000);
    CHECK(pthread_mutex_lock(&lock) == 0);
    while (!blocker_started) CHECK(pthread_cond_timedwait(&changed, &lock, &limit) == 0);
    CHECK(pthread_mutex_unlock(&lock) == 0);
    submit(2, 0, NOTE_TRIGGER);
    limit = deadline(2000);
    CHECK(pthread_mutex_lock(&lock) == 0);
    while (callbacks < 2) CHECK(pthread_cond_timedwait(&changed, &lock, &limit) == 0);
    CHECK(errors == 0 && second_done);
    CHECK(pthread_mutex_unlock(&lock) == 0);

    // A second event-manager delivery waits, while ordinary delivery runs.
    submit(3, 0, NOTE_TRIGGER);
    limit = deadline(2000);
    CHECK(pthread_mutex_lock(&lock) == 0);
    while (!managers_started) CHECK(pthread_cond_timedwait(&changed, &lock, &limit) == 0);
    CHECK(pthread_mutex_unlock(&lock) == 0);
    submit(4, 0, NOTE_TRIGGER);
    submit(5, 0, NOTE_TRIGGER);
    limit = deadline(2000);
    CHECK(pthread_mutex_lock(&lock) == 0);
    while (!ordinary_seen) CHECK(pthread_cond_timedwait(&changed, &lock, &limit) == 0);
    limit = deadline(100);
    while (managers_started == 1) {
        int result = pthread_cond_timedwait(&changed, &lock, &limit);
        CHECK(result == 0 || result == ETIMEDOUT);
        if (result == ETIMEDOUT) break;
    }
    CHECK(managers_started == 1 && managers_running == 1 && managers_done == 0 && errors == 0);
    release_managers = true;
    CHECK(pthread_cond_broadcast(&changed) == 0);
    limit = deadline(2000);
    while (managers_done < 2) CHECK(pthread_cond_timedwait(&changed, &lock, &limit) == 0);
    CHECK(pthread_mutex_unlock(&lock) == 0);

    submit(6, EV_ADD | EV_ONESHOT, NOTE_TRIGGER);
    limit = deadline(2000);
    CHECK(pthread_mutex_lock(&lock) == 0);
    while (rearmed < 2) CHECK(pthread_cond_timedwait(&changed, &lock, &limit) == 0);
    CHECK(callbacks == 7 && errors == 0);
    CHECK(pthread_mutex_unlock(&lock) == 0);

    struct kevent_qos_s invalid = note(77, EV_ADD, 0), receipt[2];
    invalid.filter = -100;
    memset(receipt, 0xa5, sizeof(receipt));
    struct kevent_qos_s canary = receipt[1];
    CHECK(kevent_qos(-1, &invalid, 1, receipt, 1, NULL, NULL,
                     KEVENT_FLAG_WORKQ | KEVENT_FLAG_ERROR_EVENTS) == 1);
    CHECK(receipt[0].flags & EV_ERROR && receipt[0].data > 0);
    CHECK(memcmp(&canary, &receipt[1], sizeof(canary)) == 0);
    errno = 0;
    CHECK(kevent_qos(-1, NULL, 1, NULL, 0, NULL, NULL, KEVENT_FLAG_WORKQ) == -1 && errno == EINVAL);
    for (uint64_t id = 1; id <= 5; ++id) submit(id, EV_DELETE, 0);
    printf("DARWIN_KEVENT_WORKQUEUE_OK callbacks=%u managers=%u checks=%u\n", callbacks, managers_done, checks);
    return 0;
}
