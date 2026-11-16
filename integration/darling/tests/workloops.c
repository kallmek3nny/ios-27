#define WORKLOOPS_TEST 1
#include "kevent-workqueue.c"

#define EVFILT_WORKLOOP (-17)
#define NOTE_WL_THREAD_REQUEST 1
#define NOTE_WL_SYNC_WAIT 4
#define NOTE_WL_SYNC_WAKE 8
#define NOTE_WL_SYNC_IPC 0x80000000u
#define NOTE_WL_COMMANDS_MASK 0x8000000fu
#define NOTE_WL_UPDATE_QOS 0x10
#define NOTE_WL_END_OWNERSHIP 0x20
#define NOTE_WL_DISCOVER_OWNER 0x80
#define NOTE_WL_IGNORE_ESTALE 0x100
#define EV_EXTIDX_WL_ADDR 1
#define EV_EXTIDX_WL_MASK 2
#define EV_EXTIDX_WL_VALUE 3
#define KEVENT_FLAG_IMMEDIATE 1
#define KEVENT_FLAG_ERROR_EVENTS 2
#define KEVENT_FLAG_STACK_DATA 8
#define KEVENT_FLAG_WORKLOOP 0x400
#define KEVENT_FLAG_PARKING 0x800
#define KEVENT_FLAG_DYNAMIC_KQ_MUST_EXIST 0x20000
#define KEVENT_FLAG_DYNAMIC_KQ_MUST_NOT_EXIST 0x40000
#define _PTHREAD_TSD_SLOT_MACH_THREAD_SELF 3
static uintptr_t _pthread_getspecific_direct(unsigned slot) { CHECK(slot == 3); return 0x405; }
#include WORKLOOP_BLOCK

static long change_loop(uint64_t id, struct kevent_qos_s *change, struct kevent_qos_s *error) {
    return sys_kevent_id(id, change, 1, error, error ? 1 : 0, NULL, NULL,
                         KEVENT_FLAG_WORKLOOP | KEVENT_FLAG_ERROR_EVENTS);
}
static void finish(unsigned index) {
    struct wq_kevent_data *delivery = delivery_log[index];
    CHECK(delivery->workloop == 1 && delivery->capacity == WL_PENDING + 1);
    delivery->nevents = 0;
    delivery->returned(delivery);
}

int main(void) {
    _dlsym = lookup;
    CHECK(sys_kevent_id(0, NULL, 0, NULL, 0, NULL, NULL, KEVENT_FLAG_WORKLOOP) == -EINVAL);
    CHECK(sys_kevent_id(1, NULL, 1, NULL, 0, NULL, NULL, KEVENT_FLAG_WORKLOOP) == -EINVAL);
    CHECK(sys_kevent_id(1, NULL, -1, NULL, 0, NULL, NULL, KEVENT_FLAG_WORKLOOP) == -EINVAL);
    CHECK(sys_kevent_id(1, NULL, 0, NULL, 0, NULL, NULL, 0) == -EINVAL);
    CHECK(sys_kevent_id(1, NULL, 0, NULL, 0, NULL, NULL,
                        KEVENT_FLAG_WORKLOOP | KEVENT_FLAG_DYNAMIC_KQ_MUST_EXIST) == -ENOENT);
    struct kevent_qos_s request = {.ident = 10, .filter = EVFILT_WORKLOOP,
        .flags = EV_ADD | EV_CLEAR, .qos = 0x8ff, .fflags = NOTE_WL_THREAD_REQUEST, .udata = 55};
    CHECK(change_loop(10, &request, NULL) == 0 && requests == 1);
    CHECK(*((uint64_t *)pending->events - 1) == 10 && pending->events[0].udata == 55);
    CHECK((pending->events[0].flags & (EV_ADD | EV_ERROR)) == 0);
    CHECK(sys_kevent_id(10, NULL, 0, NULL, 0, NULL, NULL,
                        KEVENT_FLAG_WORKLOOP | KEVENT_FLAG_DYNAMIC_KQ_MUST_NOT_EXIST) == -EEXIST);
    finish(0);
    CHECK(requests == 1); // an inactive EV_CLEAR request cannot spin
    request.flags = EV_ENABLE;
    CHECK(change_loop(10, &request, NULL) == 0 && requests == 2);
    finish(1);
    request.flags = EV_DELETE;
    CHECK(change_loop(10, &request, NULL) == 0);

    uint64_t state = 0x405;
    request.flags = EV_ADD | EV_CLEAR;
    request.fflags = NOTE_WL_THREAD_REQUEST | NOTE_WL_DISCOVER_OWNER;
    request.ext[EV_EXTIDX_WL_ADDR] = (uintptr_t)&state;
    request.ext[EV_EXTIDX_WL_MASK] = UINT64_MAX;
    request.ext[EV_EXTIDX_WL_VALUE] = 0;
    struct kevent_qos_s error;
    CHECK(change_loop(20, &request, &error) == 1 && error.data == ESTALE);
    CHECK(error.ext[EV_EXTIDX_WL_VALUE] == state && requests == 2);
    request.fflags |= NOTE_WL_IGNORE_ESTALE;
    CHECK(change_loop(20, &request, &error) == 0 && requests == 2);
    request.fflags &= ~NOTE_WL_IGNORE_ESTALE;
    request.ext[EV_EXTIDX_WL_VALUE] = state;
    CHECK(change_loop(20, &request, NULL) == 0 && requests == 2); // owned by this thread
    request.flags = EV_ENABLE;
    request.fflags = NOTE_WL_THREAD_REQUEST | NOTE_WL_END_OWNERSHIP;
    CHECK(change_loop(20, &request, NULL) == 0 && requests == 3);
    finish(2);
    request.flags = EV_DELETE;
    CHECK(change_loop(20, &request, NULL) == 0);
    request.ext[EV_EXTIDX_WL_ADDR] = (uintptr_t)&state + 1;
    CHECK(change_loop(20, &request, &error) == 1 && error.data == EFAULT);

    struct kevent_qos_s sync = {.ident = 99, .filter = EVFILT_WORKLOOP,
        .flags = EV_ADD, .fflags = NOTE_WL_SYNC_WAKE};
    CHECK(change_loop(30, &sync, NULL) == 0);
    sync.flags = 0; sync.fflags = NOTE_WL_SYNC_WAIT;
    CHECK(change_loop(30, &sync, NULL) == 0); // wake-before-wait is persistent
    sync.flags = EV_DELETE;
    CHECK(change_loop(30, &sync, NULL) == 0);

    struct kevent_qos_s timer = {.ident = 1, .udata = 7, .filter = EVFILT_TIMER, .flags = EV_ADD};
    CHECK(change_loop(40, &timer, NULL) == 0);
    uint64_t first_ident = last_change.ident;
    CHECK(change_loop(41, &timer, NULL) == 0 && first_ident != last_change.ident);
    CHECK(first_ident != timer.ident && last_change.ident != timer.ident);
    int index = find_note(40, timer.ident, timer.filter, timer.udata, true);
    CHECK(index >= 0);
    uint64_t serial = notes[index].serial;
    unsigned before = requests;
    CHECK(darling_workloop_post(40, &timer, serial + 1) == 0 && requests == before);
    CHECK(darling_workloop_post(40, &timer, serial) == 0 && requests == before + 1);
    CHECK(pending->events[0].ident == 1 && *((uint64_t *)pending->events - 1) == 40);
    finish(before);
    timer.flags = EV_DISABLE;
    CHECK(change_loop(40, &timer, NULL) == 0);
    CHECK(darling_workloop_post(40, &timer, serial) == 0 && requests == before + 1);
    timer.flags = EV_DELETE;
    CHECK(change_loop(40, &timer, NULL) == 0 && change_loop(41, &timer, NULL) == 0);

    // More IDs than table entries over time must reuse quiescent entries.
    request = (struct kevent_qos_s){.filter = EVFILT_WORKLOOP, .qos = 0x8ff, .fflags = NOTE_WL_THREAD_REQUEST};
    for (unsigned i = 0; i < WL_COUNT + 20; ++i) {
        request.flags = EV_ADD | EV_CLEAR;
        unsigned at = requests;
        CHECK(change_loop(1000 + i, &request, NULL) == 0 && requests == at + 1);
        finish(at);
        request.flags = EV_DELETE;
        CHECK(change_loop(1000 + i, &request, NULL) == 0);
    }

    // Errors from returned changes are delivered to the next servicer.
    request.flags = EV_ADD;
    unsigned at = requests;
    CHECK(change_loop(5000, &request, NULL) == 0);
    pending->events[0] = (struct kevent_qos_s){.filter = EVFILT_WORKLOOP, .fflags = 2};
    pending->nevents = 1;
    pending->returned(pending);
    CHECK(requests == at + 2 && pending->events[0].data == ENOTSUP && (pending->events[0].flags & EV_ERROR));
    finish(at + 1);
    request.flags = EV_DELETE;
    CHECK(change_loop(5000, &request, NULL) == 0);
    printf("WORKLOOPS_OK checks=%u deliveries=%u\n", checks, requests);
    return 0;
}
