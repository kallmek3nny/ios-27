#define _POSIX_C_SOURCE 200809L
#include "ios27/phase22e_ipc.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(int success, const char *why) {
    if (!success) { fprintf(stderr, "phase22e negative: %s\n", why); abort(); }
}
static void write32(uint8_t *b, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) b[i] = (uint8_t)(n >> (8 * i));
}
static void wire_header(uint8_t *b, size_t n, uint32_t bits, uint32_t dest) {
    memset(b, 0, n); write32(b, bits); write32(b + 4, (uint32_t)n);
    write32(b + 8, dest); write32(b + 20, 4321);
}
static int receive(phase22e_space *s, uint32_t t, uint32_t p,
                   void *wire, size_t *n, void *ool, size_t *oo) {
    return phase22e_receive(s, t, p, wire, n, ool, oo, 0);
}
static void test_ool(void) {
    phase22e_space *s = phase22e_new();
    require(s != NULL, "alloc");
    uint32_t p;
    require(!phase22e_port_create(s, 2, &p), "port");
    require(!phase22e_make_send(s, 2, p, 1), "send right");
    uint8_t b[44], arena[128], got[64], payload[64];
    memset(arena, 0xa5, sizeof(arena));
    wire_header(b, sizeof(b), PHASE22E_COMPLEX | PHASE22E_COPY_SEND, p);
    write32(b + 24, 1);
    write32(b + 28, 8); /* OOL guest offset 8, high 32 bits remain 0. */
    b[39] = PHASE22E_DESC_OOL;
    write32(b + 40, 40);
    require(!phase22e_send(s, 1, b, sizeof(b), arena, sizeof(arena), 0), "OOL send");
    phase22e_stats before = {0};
    require(!phase22e_stats_get(s, &before) && before.ool_bytes_pending == 40, "accounting");
    phase22e_port_diag diag;
    require(!phase22e_port_diagnostics(s, 2, p, &diag) &&
            diag.queued == 1 && diag.next_message_id == 4321, "queue diagnostics");
    size_t n = 1, o = sizeof(payload);
    require(receive(s, 2, p, got, &n, payload, &o) == PHASE22E_SIZE,
            "short receive does not consume");
    n = sizeof(got); o = 39;
    require(receive(s, 2, p, got, &n, payload, &o) == PHASE22E_SIZE,
            "short OOL does not consume");
    n = sizeof(got); o = sizeof(payload);
    require(!receive(s, 2, p, got, &n, payload, &o) && n == 44 && o == 40,
            "OOL receive");
    require(!memcmp(payload, arena + 8, 40), "OOL snapshot");
    require(!memcmp(got + 28, "\0\0\0\0\0\0\0\0", 8), "no leaked guest address");
    require(!phase22e_destroy(s, 2, p), "port teardown");
    phase22e_stats after;
    require(!phase22e_stats_get(s, &after) && !after.messages_pending &&
            !after.ool_bytes_pending && !after.live_ports, "zero leaks");
    phase22e_free(s);
}
static void test_rollback_and_dead_names(void) {
    phase22e_space *s = phase22e_new();
    require(s != NULL, "alloc rollback");
    uint32_t dest, granted;
    require(!phase22e_port_create(s, 2, &dest), "dest");
    require(!phase22e_port_create(s, 3, &granted), "grant");
    require(!phase22e_make_send(s, 2, dest, 1), "send");
    require(!phase22e_make_send(s, 3, granted, 1), "grant send");
    uint8_t msg[40], result[64];
    wire_header(msg, sizeof(msg), PHASE22E_COMPLEX | PHASE22E_COPY_SEND, dest);
    write32(msg + 24, 1); write32(msg + 28, granted);
    msg[38] = PHASE22E_MOVE_SEND; msg[39] = PHASE22E_DESC_PORT;
    require(!phase22e_send(s, 1, msg, sizeof(msg), NULL, 0, 0), "queued transfer");
    phase22e_rights ref;
    require(!phase22e_rights_get(s, 1, granted, &ref) && !ref.send, "sender debited");
    require(phase22e_destroy(s, 3, granted) == PHASE22E_QUOTA,
            "transfer prevents port destruction");
    size_t n = sizeof(result), ool = 0;
    require(!receive(s, 2, dest, result, &n, NULL, &ool), "receive transfer");
    require(!phase22e_rights_get(s, 2, granted, &ref) && ref.send == 1,
            "receiver credited");
    require(!phase22e_destroy(s, 3, granted), "destroy granted");
    require(!phase22e_rights_get(s, 2, granted, &ref) && ref.dead && !ref.send,
            "dead names observed");
    require(!phase22e_destroy(s, 2, dest), "destroy dest");
    phase22e_stats st;
    require(!phase22e_stats_get(s, &st) && !st.in_flight_transfers, "transfer fully released");
    phase22e_free(s);
}
static void test_send_once_and_repeat_descriptor(void) {
    phase22e_space *s = phase22e_new();
    require(s != NULL, "alloc send-once");
    uint32_t dest, reply, grant;
    require(!phase22e_port_create(s, 2, &dest), "service port");
    require(!phase22e_port_create(s, 1, &reply), "reply port");
    require(!phase22e_port_create(s, 3, &grant), "descriptor port");
    require(!phase22e_make_send(s, 2, dest, 1), "service send");
    require(!phase22e_make_send(s, 3, grant, 1), "descriptor send");
    uint8_t msg[52];
    wire_header(msg, sizeof(msg), PHASE22E_COMPLEX | PHASE22E_COPY_SEND, dest);
    write32(msg + 24, 2);
    write32(msg + 28, grant); msg[38] = PHASE22E_MOVE_SEND;
    write32(msg + 40, grant); msg[50] = PHASE22E_MOVE_SEND;
    require(phase22e_send(s, 1, msg, sizeof(msg), NULL, 0, 0) == PHASE22E_RIGHT,
            "repeated descriptor consumes two refs, not one");
    phase22e_rights r;
    require(!phase22e_rights_get(s, 1, grant, &r) && r.send == 1,
            "failed transfer preserves right");
    phase22e_stats st;
    require(!phase22e_stats_get(s, &st) && !st.messages_pending,
            "rejected transfer does not enqueue");
    uint8_t request[24];
    wire_header(request, sizeof(request), PHASE22E_COPY_SEND |
                (PHASE22E_MAKE_SEND_ONCE << 8), dest);
    write32(request + 12, reply);
    require(!phase22e_send(s, 1, request, sizeof(request), NULL, 0, 0),
            "one-shot request");
    require(phase22e_destroy(s, 1, reply) == PHASE22E_QUOTA,
            "in-flight reply right pins port");
    uint8_t recv[24]; size_t n = sizeof(recv), o = 0;
    require(!receive(s, 2, dest, recv, &n, NULL, &o), "receive one-shot");
    require(!phase22e_rights_get(s, 2, reply, &r) && r.send_once == 1,
            "send-once right installed at receive");
    wire_header(request, sizeof(request), PHASE22E_MOVE_SEND_ONCE, reply);
    require(!phase22e_send(s, 2, request, sizeof(request), NULL, 0, 0),
            "send-once reply");
    require(phase22e_send(s, 2, request, sizeof(request), NULL, 0, 0) == PHASE22E_RIGHT,
            "send-once cannot be reused");
    require(!phase22e_rights_get(s, 2, reply, &r) && r.send_once == 0,
            "send-once consumed exactly once");
    n = sizeof(recv); o = 0;
    require(!receive(s, 1, reply, recv, &n, NULL, &o), "receive reply");
    require(phase22e_receive(s, 2, dest, recv, &n, NULL, &o, 1) == PHASE22E_TIMEOUT,
            "bounded timed receive");
    require(!phase22e_destroy(s, 1, reply), "retire reply");
    require(!phase22e_destroy(s, 2, dest), "retire dest");
    require(!phase22e_destroy(s, 3, grant), "retire grant");
    require(!phase22e_stats_get(s, &st) && !st.in_flight_transfers &&
            !st.messages_pending && !st.live_ports, "one-shot teardown exact");
    phase22e_free(s);
}
static void test_drop_queued_transfer(void) {
    phase22e_space *s = phase22e_new();
    require(s != NULL, "alloc drop");
    uint32_t dest, granted;
    require(!phase22e_port_create(s, 2, &dest), "dest");
    require(!phase22e_port_create(s, 3, &granted), "grant");
    require(!phase22e_make_send(s, 2, dest, 1), "send");
    require(!phase22e_make_send(s, 3, granted, 1), "grant send");
    uint8_t msg[40];
    wire_header(msg, sizeof(msg), PHASE22E_COMPLEX | PHASE22E_COPY_SEND, dest);
    write32(msg + 24, 1); write32(msg + 28, granted);
    msg[38] = PHASE22E_COPY_SEND; msg[39] = PHASE22E_DESC_PORT;
    require(!phase22e_send(s, 1, msg, sizeof(msg), NULL, 0, 0), "enqueue");
    require(!phase22e_destroy(s, 2, dest), "drop pending message");
    require(!phase22e_destroy(s, 3, granted), "rollback in-flight transfer");
    phase22e_stats st;
    require(!phase22e_stats_get(s, &st) && !st.messages_pending &&
            !st.in_flight_transfers && !st.live_ports, "rollback exact");
    phase22e_free(s);
}
typedef struct { phase22e_space *s; uint32_t p; atomic_int result; } waiter;
static void *waiting_receiver(void *ctx) {
    waiter *w = ctx;
    uint8_t b[32]; size_t n = sizeof(b), o = 0;
    int rc = phase22e_receive(w->s, 2, w->p, b, &n, NULL, &o, 5000);
    atomic_store(&w->result, rc);
    return NULL;
}
static void test_cancel(void) {
    phase22e_space *s = phase22e_new();
    require(s != NULL, "alloc cancel");
    uint32_t p;
    require(!phase22e_port_create(s, 2, &p), "port cancel");
    waiter w = {.s = s, .p = p};
    pthread_t thread;
    require(!pthread_create(&thread, NULL, waiting_receiver, &w), "start waiter");
    phase22e_stats st;
    int waiting = 0;
    for (unsigned i = 0; i < 1000000; ++i) {
        require(!phase22e_stats_get(s, &st), "check waiter");
        if (st.waiters == 1) { waiting = 1; break; }
    }
    require(waiting, "entered wait");
    require(!phase22e_cancel_waits(s), "cancel trigger");
    require(!pthread_join(thread, NULL), "cancel join");
    require(atomic_load(&w.result) == PHASE22E_CANCELLED, "cancel semantics");
    require(!phase22e_task_die(s, 2), "task death");
    require(!phase22e_stats_get(s, &st) && !st.waiters && !st.live_ports,
            "cancel teardown accounting");
    phase22e_free(s);
}
static void test_limits_and_fuzz(void) {
    phase22e_space *s = phase22e_new();
    require(s != NULL, "alloc fuzz");
    uint32_t p;
    require(!phase22e_port_create(s, 2, &p), "port fuzz");
    require(!phase22e_make_send(s, 2, p, 1), "right fuzz");
    uint8_t b[PHASE22E_WIRE_MAX];
    wire_header(b, 24, PHASE22E_COPY_SEND, p);
    for (unsigned i = 0; i < PHASE22E_QUEUE; ++i)
        require(!phase22e_send(s, 1, b, 24, NULL, 0, 0), "fill queue");
    require(phase22e_send(s, 1, b, 24, NULL, 0, 0) == PHASE22E_TIMEOUT,
            "bounded backpressure");
    size_t n = sizeof(b), ool = 0;
    require(!receive(s, 2, p, b, &n, NULL, &ool), "dequeue");
    wire_header(b, 24, PHASE22E_COPY_SEND, p);
    require(!phase22e_send(s, 1, b, 24, NULL, 0, 0), "recover after pressure");
    uint32_t state = UINT32_C(0x1830c0de);
    const char *seed_env = getenv("PHASE22E_SEED");
    if (seed_env && *seed_env) {
        char *end = NULL;
        unsigned long seed = strtoul(seed_env, &end, 0);
        require(end && *end == '\0' && seed && seed <= UINT32_MAX, "seed format");
        state = (uint32_t)seed;
    }
    uint32_t initial_seed = state;
    unsigned rejected = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        for (unsigned j = 0; j < sizeof(b); ++j) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            b[j] = (uint8_t)state;
        }
        size_t sz = (state % sizeof(b)) + 1;
        int rc = phase22e_send(s, 1, b, sz, NULL, 0, 0);
        require(rc != PHASE22E_OK, "fuzz must fail closed");
        ++rejected;
    }
    require(rejected == 100000, "fuzz iterations");
    require(!phase22e_destroy(s, 2, p), "drop queued");
    phase22e_stats st;
    require(!phase22e_stats_get(s, &st) && !st.messages_pending &&
            !st.in_flight_transfers && st.bad_messages >= 100000,
            "queue reclamation and malformed diagnostics");
    printf("malformed_fuzz=%u seed=0x%08x queue_capacity=%u rejected=%llu\n",
           rejected, initial_seed, PHASE22E_QUEUE, (unsigned long long)st.rejected);
    phase22e_free(s);
}
int main(void) {
    test_ool(); test_rollback_and_dead_names(); test_send_once_and_repeat_descriptor();
    test_drop_queued_transfer();
    test_cancel(); test_limits_and_fuzz();
    puts("phase22e negative: passed");
    return 0;
}
