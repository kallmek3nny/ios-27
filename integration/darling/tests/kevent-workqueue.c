/* Compile the real asynchronous adapter with deterministic syscall/worker
 * fixtures. Native tests separately exercise pthread and kqueue concurrency. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#define EV_ADD 1
#define EV_DELETE 2
#define EV_ENABLE 4
#define EV_DISABLE 8
#define EV_CLEAR 0x20
#define EV_ONESHOT 0x10
#define EV_RECEIPT 0x40
#define EV_UDATA_SPECIFIC 0x100
#define EV_ERROR 0x4000
#define EVFILT_TIMER (-7)
#define EVFILT_USER (-10)
struct kevent64_s {
    uint64_t ident; int16_t filter; uint16_t flags; uint32_t fflags;
    int64_t data; uint64_t udata, ext[2];
};
struct kevent_qos_s {
    uint64_t ident; int16_t filter; uint16_t flags; int32_t qos;
    uint64_t udata; uint32_t fflags, xflags; int64_t data; uint64_t ext[4];
};
_Static_assert(sizeof(struct kevent64_s) == 48, "kevent64 layout");
_Static_assert(sizeof(struct kevent_qos_s) == 72, "kevent_qos layout");
#include WORKQUEUE_HEADER
typedef int libsimple_lock_t;
#define LIBSIMPLE_LOCK_INITIALIZER 0
static unsigned checks, kernel_changes, creates, closes, logs;
static int create_error, kernel_error, request_error;
static struct wq_kevent_data *pending;
static int pending_priority;
static unsigned requests;
static struct wq_kevent_data *delivery_log[2048];
static struct kevent64_s last_change;
static void *(*start_manager)(void *);
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static void libsimple_lock_init(int *value) { *value = 0; }
static void libsimple_lock_lock(int *value) { CHECK(*value == 0); *value = 1; }
static void libsimple_lock_unlock(int *value) { CHECK(*value == 1); *value = 0; }
static void __simple_printf(const char *format, ...) { (void)format; ++logs; }
static _Noreturn void __simple_abort(void) { abort(); }
static long sys_kqueue(void) { return 42; }
static long sys_fcntl_nocancel(int fd, int operation, long flags) {
    CHECK(fd == 42 && operation == F_SETFD && flags == FD_CLOEXEC); return 0;
}
static long sys_close_nocancel(int fd) { CHECK(fd == 42); ++closes; return 0; }
static long sys_kevent64(int fd, const struct kevent64_s *input, int ninput,
        struct kevent64_s *output, int noutput, unsigned flags, const struct timespec *timeout) {
    CHECK(fd == 42 && flags == 0);
    if (!ninput) { CHECK(noutput == 16 && timeout == NULL); return -EIO; }
    CHECK(ninput == 1 && noutput == 1 && input && output && timeout);
    CHECK((input->flags & EV_RECEIPT) && timeout->tv_sec == 0 && timeout->tv_nsec == 0);
    ++kernel_changes;
    last_change = *input;
    *output = *input; output->flags = EV_ERROR; output->data = kernel_error;
    return 1;
}
long sys_workq_kernreturn(int operation, void *item, int count, int priority) {
    CHECK(operation == WQOPS_QUEUE_REQTHREAD_FOR_KEVENT && count == 1 && item);
    pending = item; pending_priority = priority;
    CHECK(requests < sizeof(delivery_log) / sizeof(delivery_log[0]));
    delivery_log[requests++] = item;
    return request_error;
}
int sem_down(int *value, int timeout) { CHECK(*value > 0 && timeout == -1); --*value; return 1; }
void sem_up(int *value) { ++*value; }
void sigexc_setup2(void) { }
static int create(void **thread, const void *attr, void *(*start)(void *), void *arg) {
    CHECK(attr == NULL && arg == NULL && thread && start); ++creates;
    start_manager = start; *thread = (void *)(uintptr_t)7;
    return create_error;
}
static int detach(void *thread) { CHECK(thread == (void *)(uintptr_t)7); return 0; }
static void *lookup(void *handle, const char *name) {
    CHECK(handle == (void *)-2);
    if (strcmp(name, "pthread_create") == 0) return (void *)create;
    if (strcmp(name, "pthread_detach") == 0) return (void *)detach;
    CHECK(0); return NULL;
}
void *(*_dlsym)(void *, const char *);
long darling_workloop_post(uint64_t id, const struct kevent_qos_s *event, uint64_t serial);
#ifndef WORKLOOPS_TEST
long darling_workloop_post(uint64_t id, const struct kevent_qos_s *event, uint64_t serial) {
    (void)id; (void)event; (void)serial;
    CHECK(0); return -ENOTSUP;
}
#endif
#include KEVENT_WORKQUEUE_BLOCK

#ifndef WORKLOOPS_TEST
int main(void) {
    CHECK(darling_kevent_workq(NULL, 1, NULL, 0, 0) == -EINVAL);
    CHECK(darling_kevent_workq(NULL, -1, NULL, 0, 0) == -EINVAL);
    CHECK(darling_kevent_workq(NULL, 0, NULL, 1, 0) == -EINVAL);
    CHECK(darling_kevent_workq(NULL, 0, NULL, 0, 0) == -ENOSYS && creates == 0);
    _dlsym = lookup; create_error = EAGAIN;
    CHECK(darling_kevent_workq(NULL, 0, NULL, 0, 0) == -EAGAIN && closes == 1 && queue == -1);
    create_error = 0;
    CHECK(darling_kevent_workq(NULL, 0, NULL, 0, 0) == 0 && creates == 2 && start_manager);

    struct kevent_qos_s change = {.filter = -10, .flags = EV_ADD, .qos = 0x1ff};
    for (unsigned i = 0; i < WORKQ_NOTES; ++i) {
        change.ident = change.udata = i;
        CHECK(darling_kevent_workq(&change, 1, NULL, 0, 0) == 0);
    }
    unsigned before = kernel_changes;
    struct kevent_qos_s receipt[2], canary;
    memset(receipt, 0xa5, sizeof(receipt)); canary = receipt[1];
    change.ident = change.udata = WORKQ_NOTES;
    CHECK(darling_kevent_workq(&change, 1, receipt, 1, 0) == 1);
    CHECK(receipt[0].data == ENOMEM && kernel_changes == before);
    CHECK(memcmp(&receipt[1], &canary, sizeof(canary)) == 0);
    change.ident = change.udata = 0; change.flags = EV_DELETE;
    CHECK(darling_kevent_workq(&change, 1, NULL, 0, 0) == 0);
    change.ident = change.udata = WORKQ_NOTES; change.flags = EV_ADD | EV_RECEIPT;
    kernel_error = EACCES;
    CHECK(darling_kevent_workq(&change, 1, receipt, 1, 0) == 1 && receipt[0].data == EACCES);
    kernel_error = 0;
    CHECK(darling_kevent_workq(&change, 1, receipt, 1, 0) == 1 && receipt[0].data == 0);
    CHECK(memcmp(&receipt[1], &canary, sizeof(canary)) == 0);

    // Worker delivery owns a copy, survives caller mutation, and releases its
    // slot only after applying the changes returned by that worker.
    change.ident = change.udata = 1; change.flags = EV_DELETE;
    deliver(&change);
    CHECK(free_slots == WORKQ_SLOTS - 1 && pending_priority == 0x1ff);
    CHECK(pending->events != &change && pending->events[0].ident == 1);
    change.ident = 9999;
    CHECK(pending->events[0].ident == 1 && pending->capacity == WORKQ_RETURNED);
    pending->returned(pending);
    CHECK(free_slots == WORKQ_SLOTS);
    change.ident = change.udata = WORKQ_NOTES + 1; change.flags = EV_ADD;
    CHECK(darling_kevent_workq(&change, 1, NULL, 0, 0) == 0);

    request_error = -EAGAIN;
    change.qos = 0;
    deliver(&change);
    CHECK(free_slots == WORKQ_SLOTS && pending_priority == WORKQ_DEFAULT_PRIORITY);
    CHECK(darling_kevent_workq(NULL, 0, NULL, 0, 0) == -EAGAIN);
    failed = 0; request_error = 0;
    CHECK(start_manager(NULL) == NULL);
    CHECK(darling_kevent_workq(NULL, 0, NULL, 0, 0) == -EIO);
    CHECK(logs == 0);
    init_lock = note_lock = slot_lock = 1;
    darling_kevent_workq_fork_child();
    CHECK(init_lock == 0 && note_lock == 0 && slot_lock == 0);
    CHECK(queue == -1 && failed == 0 && free_slots == WORKQ_SLOTS);
    for (unsigned i = 0; i < WORKQ_NOTES; ++i) CHECK(!notes[i].used);
    for (unsigned i = 0; i < WORKQ_SLOTS; ++i) CHECK(!slots[i].used);
    printf("KEVENT_WORKQUEUE_OK checks=%u\n", checks);
    return 0;
}
#endif
