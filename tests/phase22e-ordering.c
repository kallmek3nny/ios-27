#define _POSIX_C_SOURCE 200809L
#include "ios27/phase22e_ipc.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "phase22e ordering failed: %s\n", message); abort(); }
}
static void make_msg(phase22e_header *h, uint32_t dest, int32_t id) {
    memset(h, 0, sizeof(*h));
    h->bits = PHASE22E_COPY_SEND; h->size = sizeof(*h);
    h->remote_port = dest; h->id = id;
}
typedef struct {
    phase22e_space *s;
    uint32_t port;
    int32_t id;
    atomic_int result;
} sender;
static void *send_one(void *context) {
    sender *w = context;
    phase22e_header h;
    make_msg(&h, w->port, w->id);
    atomic_store(&w->result, phase22e_send(w->s, 1, &h, sizeof(h), NULL, 0, 5000));
    return NULL;
}
static void wait_for_count(phase22e_space *s, uint64_t target) {
    phase22e_stats st;
    for (unsigned i = 0; i < 3000000u; ++i) {
        check(!phase22e_stats_get(s, &st), "waiter stats");
        if (st.waiters >= target) return;
    }
    check(0, "waiting threads never started");
}
static int dispatch_fail(void *ctx, const uint8_t *req, size_t n,
                         uint8_t *out, size_t *length) {
    (void)ctx; (void)req; (void)n; (void)out; (void)length;
    return PHASE22E_UNSUPPORTED;
}
typedef struct {
    phase22e_space *s;
    uint32_t port;
    uint32_t task;
    atomic_int rc;
} registration;
static void *register_one(void *ctx) {
    registration *r = ctx;
    phase22e_method m = {.request_id = 11, .request_size = 24,
                          .response_size = 24, .invoke = dispatch_fail};
    atomic_store(&r->rc, phase22e_service_register(r->s, r->task, r->port, &m, 1));
    return NULL;
}
static void test_fifo(void) {
    phase22e_space *s = phase22e_new();
    check(s != NULL, "allocate fifo");
    uint32_t p = 0;
    check(!phase22e_port_create(s, 2, &p), "allocate fifo port");
    check(!phase22e_make_send(s, 2, p, 1), "grant fifo send");
    phase22e_header h;
    for (unsigned i = 0; i < PHASE22E_QUEUE; ++i) {
        make_msg(&h, p, -1);
        check(!phase22e_send(s, 1, &h, sizeof(h), NULL, 0, 0), "fill fifo");
    }
    enum { N = 4 };
    pthread_t threads[N];
    sender workers[N];
    for (unsigned i = 0; i < N; ++i) {
        workers[i] = (sender){.s = s, .port = p, .id = (int32_t)i + 100};
        atomic_init(&workers[i].result, 99);
        check(!pthread_create(&threads[i], NULL, send_one, &workers[i]), "spawn send");
        wait_for_count(s, i + 1);
    }
    for (unsigned i = 0; i < PHASE22E_QUEUE + N; ++i) {
        phase22e_header reply;
        size_t len = sizeof(reply), ool_len = 0;
        int rc = phase22e_receive(s, 2, p, &reply, &len, NULL, &ool_len, 5000);
        check(rc == PHASE22E_OK && len == sizeof(reply), "fifo receive");
        if (i < PHASE22E_QUEUE) check(reply.id == -1, "initial fifo order");
        else check(reply.id == (int32_t)(i - PHASE22E_QUEUE + 100), "waiting fifo order");
    }
    for (unsigned i = 0; i < N; ++i) {
        check(!pthread_join(threads[i], NULL), "fifo join");
        check(atomic_load(&workers[i].result) == PHASE22E_OK, "fifo send succeeds");
    }
    check(!phase22e_destroy(s, 2, p), "fifo destroy");
    phase22e_free(s);
}
static void test_registration(void) {
    phase22e_space *s = phase22e_new();
    check(s != NULL, "allocate registration");
    enum { N = 8 };
    registration regs[N];
    pthread_t threads[N];
    for (unsigned i = 0; i < N; ++i) {
        regs[i] = (registration){.s = s, .task = (uint32_t)i + 1};
        check(!phase22e_port_create(s, regs[i].task, &regs[i].port), "port registry");
    }
    for (unsigned i = 0; i < N; ++i)
        check(!pthread_create(&threads[i], NULL, register_one, &regs[i]), "spawn registry");
    for (unsigned i = 0; i < N; ++i) {
        check(!pthread_join(threads[i], NULL), "join registry");
        check(atomic_load(&regs[i].rc) == PHASE22E_OK, "registered");
    }
    phase22e_method duplicate[2] = {
        {.request_id=11,.request_size=24,.response_size=24,.invoke=dispatch_fail},
        {.request_id=11,.request_size=24,.response_size=24,.invoke=dispatch_fail}
    };
    check(phase22e_service_register(s, 1, regs[0].port, duplicate, 2) == PHASE22E_BAD,
          "duplicate method rejected");
    check(phase22e_service_unregister(s, 2, regs[0].port) == PHASE22E_RIGHT,
          "foreign unregister rejected");
    for (unsigned i = 0; i < N; ++i) {
        check(!phase22e_service_unregister(s, regs[i].task, regs[i].port),
              "unregister all");
        check(!phase22e_destroy(s, regs[i].task, regs[i].port), "destroy reg ports");
    }
    phase22e_stats stats;
    check(!phase22e_stats_get(s, &stats) && !stats.live_ports, "no live registered ports");
    phase22e_free(s);
}
static void test_quota_and_repeated_teardown(void) {
    for (unsigned repeat = 0; repeat < 12; ++repeat) {
        phase22e_space *s = phase22e_new();
        check(s != NULL, "create repeated space");
        uint32_t p = 0;
        for (unsigned i = 0; i < PHASE22E_PORTS; ++i)
            check(!phase22e_port_create(s, 2, &p), "port within quota");
        check(phase22e_port_create(s, 2, &p) == PHASE22E_QUOTA, "port quota");
        check(!phase22e_task_die(s, 2), "task teardown");
        phase22e_stats stats;
        check(!phase22e_stats_get(s, &stats) && !stats.live_ports, "no live ports");
        phase22e_free(s);
    }
}
int main(void) {
    test_fifo(); test_registration(); test_quota_and_repeated_teardown();
    puts("phase22e ordering, concurrent registration, quotas and teardown: passed");
    return 0;
}
