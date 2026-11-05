#define _POSIX_C_SOURCE 200809L
#include "ios27/phase22e_ipc.h"
#include "phase22e-generated.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLIENTS 4u
#define ITERATIONS 25000u
static phase22e_space *space;
static uint32_t add_port, multiply_port;
static atomic_uint_fast64_t completed, nested, failures;
static atomic_int shutdown_servers;
static void ensure(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "phase22e stress failed: %s\n", what); abort(); }
}
static int calc(void *context, const uint8_t *req, size_t n,
                uint8_t *out, size_t *out_n) {
    uint32_t op = *(const uint32_t *)context;
    phase22e_calc_request in;
    phase22e_calc_reply reply = {0};
    if (n != sizeof(in) || *out_n != sizeof(reply)) return PHASE22E_SIZE;
    memcpy(&in, req, n);
    reply.sequence = in.sequence;
    reply.value = op == 1 ? in.lhs + in.rhs : in.lhs * in.rhs;
    memcpy(out + sizeof(reply.header), (uint8_t *)&reply + sizeof(reply.header),
           sizeof(reply) - sizeof(reply.header));
    return PHASE22E_OK;
}
static int rpc(uint32_t task, uint32_t dest, uint32_t reply_port,
               uint32_t id, uint32_t seq, uint32_t lhs, uint32_t rhs,
               uint32_t *value) {
    phase22e_calc_request request = {0};
    request.header.bits = PHASE22E_COPY_SEND | (PHASE22E_MAKE_SEND_ONCE << 8);
    request.header.size = sizeof(request);
    request.header.remote_port = dest;
    request.header.local_port = reply_port;
    request.header.id = (int32_t)id;
    request.sequence = seq; request.lhs = lhs; request.rhs = rhs;
    int rc = phase22e_send(space, task, &request, sizeof(request), NULL, 0, 3000);
    if (rc) return rc;
    phase22e_calc_reply result = {0};
    size_t n = sizeof(result), ool = 0;
    rc = phase22e_receive(space, task, reply_port, &result, &n, NULL, &ool, 3000);
    if (rc) return rc;
    if (n != sizeof(result) || result.header.id != (int32_t)(id + 100) ||
        result.sequence != seq || result.header.size != n) return PHASE22E_BAD;
    *value = result.value;
    return PHASE22E_OK;
}
typedef struct { uint32_t task, dest, reply, id; unsigned iterations; } client_args;
static void *client(void *v) {
    client_args *a = v;
    uint32_t seed = a->task * UINT32_C(0x9e3779b9);
    for (unsigned i = 0; i < a->iterations; ++i) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        uint32_t lhs = seed & 255u;
        uint32_t result = 0;
        if (rpc(a->task, a->dest, a->reply, a->id, i, lhs, 3, &result) != 0 ||
            result != lhs + 3) {
            ++failures; break;
        }
        ++completed;
    }
    return NULL;
}
typedef struct { uint32_t task, port; uint32_t nested_reply; int has_nested; } server_args;
static void *server(void *v) {
    server_args *a = v;
    uint64_t seen = 0;
    while (!atomic_load(&shutdown_servers)) {
        uint8_t wire[PHASE22E_WIRE_MAX];
        size_t n = sizeof(wire), ool = 0;
        int rc = phase22e_receive(space, a->task, a->port, wire, &n, NULL, &ool, 200);
        if (rc == PHASE22E_TIMEOUT || rc == PHASE22E_CANCELLED) continue;
        if (rc) { ++failures; break; }
        ++seen;
        if (a->has_nested && (seen % 128u) == 0) {
            uint32_t out = 0;
            rc = rpc(a->task, multiply_port, a->nested_reply,
                     PHASE22E_ID_MULTIPLY, (uint32_t)seen, 17, 5, &out);
            if (rc || out != 85u) { ++failures; break; }
            ++nested;
        }
        uint8_t reply[sizeof(phase22e_calc_reply)] = {0};
        size_t reply_n = sizeof(reply);
        rc = phase22e_service_dispatch(space, a->task, a->port, wire, n, reply, &reply_n);
        if (!rc) rc = phase22e_send(space, a->task, reply, reply_n, NULL, 0, 3000);
        if (rc) { ++failures; break; }
    }
    return NULL;
}
int main(void) {
    space = phase22e_new(); ensure(space != NULL, "allocate");
    ensure(phase22e_port_create(space, 2, &add_port) == 0, "add port");
    ensure(phase22e_port_create(space, 3, &multiply_port) == 0, "multiply port");
    ensure(phase22e_make_send(space, 2, add_port, 1) == 0, "grant client 1");
    ensure(phase22e_make_send(space, 2, add_port, 4) == 0, "grant client 4");
    ensure(phase22e_make_send(space, 2, add_port, 5) == 0, "grant client 5");
    ensure(phase22e_make_send(space, 2, add_port, 6) == 0, "grant client 6");
    ensure(phase22e_make_send(space, 3, multiply_port, 2) == 0, "nested grant");
    client_args ca[CLIENTS] = {0};
    uint32_t ids[CLIENTS] = {1, 4, 5, 6};
    for (unsigned i = 0; i < CLIENTS; ++i) {
        ca[i].task = ids[i]; ca[i].dest = add_port;
        ca[i].id = PHASE22E_ID_ADD; ca[i].iterations = ITERATIONS;
        ensure(phase22e_port_create(space, ca[i].task, &ca[i].reply) == 0, "client reply");
    }
    server_args srv1 = {.task = 2, .port = add_port, .has_nested = 1};
    server_args srv2 = {.task = 3, .port = multiply_port, .has_nested = 0};
    ensure(phase22e_port_create(space, 2, &srv1.nested_reply) == 0, "nested reply");
    uint32_t add_op = 1, multiply_op = 2;
    phase22e_method add_method = {.request_id = PHASE22E_ID_ADD,
        .request_size = sizeof(phase22e_calc_request),
        .response_size = sizeof(phase22e_calc_reply), .invoke = calc, .ctx = &add_op};
    phase22e_method multiply_method = {.request_id = PHASE22E_ID_MULTIPLY,
        .request_size = sizeof(phase22e_calc_request),
        .response_size = sizeof(phase22e_calc_reply), .invoke = calc, .ctx = &multiply_op};
    ensure(!phase22e_service_register(space, 2, add_port, &add_method, 1), "register add");
    ensure(!phase22e_service_register(space, 3, multiply_port, &multiply_method, 1), "register multiply");
    pthread_t services[2], workers[CLIENTS];
    ensure(pthread_create(&services[1], NULL, server, &srv2) == 0, "server multiply");
    ensure(pthread_create(&services[0], NULL, server, &srv1) == 0, "server add");
    for (unsigned i = 0; i < CLIENTS; ++i)
        ensure(pthread_create(&workers[i], NULL, client, &ca[i]) == 0, "client");
    for (unsigned i = 0; i < CLIENTS; ++i)
        ensure(pthread_join(workers[i], NULL) == 0, "client join");
    atomic_store(&shutdown_servers, 1);
    ensure(phase22e_cancel_waits(space) == 0, "cancel");
    for (unsigned i = 0; i < 2; ++i)
        ensure(pthread_join(services[i], NULL) == 0, "service join");
    ensure(!phase22e_service_unregister(space, 2, add_port), "unregister add");
    ensure(!phase22e_service_unregister(space, 3, multiply_port), "unregister multiply");
    phase22e_stats stats = {0};
    ensure(phase22e_stats_get(space, &stats) == 0, "stats");
    fprintf(stdout, "client_rpcs=%llu nested_rpcs=%llu sends=%llu receives=%llu "
                    "pending=%llu in_flight=%llu wait_peak=%llu failures=%llu\n",
            (unsigned long long)atomic_load(&completed),
            (unsigned long long)atomic_load(&nested),
            (unsigned long long)stats.sends, (unsigned long long)stats.receives,
            (unsigned long long)stats.messages_pending,
            (unsigned long long)stats.in_flight_transfers,
            (unsigned long long)stats.peak_waiters,
            (unsigned long long)atomic_load(&failures));
    ensure(atomic_load(&completed) == (uint64_t)CLIENTS * ITERATIONS, "all requests");
    ensure(atomic_load(&nested) > 100, "nested rpc");
    ensure(stats.sends == stats.receives && stats.messages_pending == 0 &&
           stats.in_flight_transfers == 0, "leak accounting");
    ensure(atomic_load(&failures) == 0, "no failures");
    phase22e_free(space);
    return 0;
}
