#include "ios27/phase23d_bridge.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { int check_status_ = (x); if (check_status_ != IOS27_23D_OK) { \
    fprintf(stderr, "failure line %d: %d\n", __LINE__, check_status_); abort(); } } while (0)
#define EXPECT(x, err) do { int check_status_ = (x); if (check_status_ != (err)) { \
    fprintf(stderr, "unexpected status line %d: %d expected %d\n", __LINE__, check_status_, (err)); abort(); } } while (0)
static ios27_23d_result trap(ios27_23d_runtime *rt, uint64_t pid, uint64_t tid, uint32_t op,
                             uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    ios27_23d_frame frame = { IOS27_23D_ABI, op, {a,b,c,d,e,f} };
    ios27_23d_result result;
    int status = ios27_23d_trap(rt, pid, tid, &frame, &result);
    assert(status == result.status);
    return result;
}
static void rpc(ios27_23d_runtime *rt, uint64_t pid, uint64_t tid, uint64_t port,
                uint64_t in, uint64_t out, uint64_t correlation, uint32_t op,
                const void *payload, uint32_t bytes, uint64_t transfer, uint32_t flags,
                uint64_t budget, int expected) {
    assert(bytes <= IOS27_23D_PAYLOAD);
    ios27_23d_wire req = {0};
    req.opcode = op; req.correlation = correlation; req.size = bytes;
    req.transfer = transfer; req.flags = flags;
    if (bytes) memcpy(req.data, payload, bytes);
    CHECK(ios27_23d_memory_write(rt, pid, in, &req, 32u + bytes));
    ios27_23d_result result = trap(rt, pid, tid, IOS27_23D_SYS_RPC,
                                   port, in, 32u + bytes, out, 160, budget);
    EXPECT(result.status, expected);
    if (!expected) {
        ios27_23d_wire response;
        CHECK(ios27_23d_memory_read(rt, pid, out, &response, 32u + bytes));
        assert(response.correlation == correlation && response.size == bytes);
        if (payload && bytes && op != IOS27_23D_RPC_SUM)
            assert(memcmp(response.data, payload, bytes) == 0);
    }
}
static void test_fds(ios27_23d_runtime *rt, uint64_t pid, uint64_t tid) {
    static const char input[] = "/fixture/input";
    static const char output[] = "/fixture/output";
    static const uint8_t seed[] = "deterministic input";
    CHECK(ios27_23d_seed(rt, pid, seed, sizeof(seed)));
    CHECK(ios27_23d_memory_write(rt, pid, 0, input, sizeof(input)));
    CHECK(ios27_23d_memory_write(rt, pid, 48, output, sizeof(output)));
    ios27_23d_result r = trap(rt, pid, tid, IOS27_23D_SYS_OPEN, 0, 0, 0, 0, 0, 0);
    CHECK(r.status); uint64_t fd = (uint64_t)r.value;
    r = trap(rt, pid, tid, IOS27_23D_SYS_READ, fd, 128, sizeof(seed), 0, 0, 0);
    CHECK(r.status); assert(r.value == (int64_t)sizeof(seed));
    uint8_t readback[sizeof(seed)];
    CHECK(ios27_23d_memory_read(rt, pid, 128, readback, sizeof(readback)));
    assert(!memcmp(readback, seed, sizeof(seed)));
    r = trap(rt, pid, tid, IOS27_23D_SYS_CLOSE, fd, 0, 0, 0, 0, 0);
    CHECK(r.status);
    EXPECT(trap(rt, pid, tid, IOS27_23D_SYS_READ, fd, 128, 2, 0, 0, 0).status, IOS27_23D_DENIED);
    r = trap(rt, pid, tid, IOS27_23D_SYS_OPEN, 48, 2, 0, 0, 0, 0);
    CHECK(r.status); fd = (uint64_t)r.value;
    r = trap(rt, pid, tid, IOS27_23D_SYS_WRITE, fd, 128, 12, 0, 0, 0);
    CHECK(r.status); assert(r.value == 12);
    r = trap(rt, pid, tid, IOS27_23D_SYS_LSEEK, fd, 0, 0, 0, 0, 0);
    CHECK(r.status); assert(r.value == 0);
    r = trap(rt, pid, tid, IOS27_23D_SYS_READ, fd, 512, 12, 0, 0, 0);
    CHECK(r.status); assert(r.value == 12);
    uint8_t buf[12]; CHECK(ios27_23d_memory_read(rt, pid, 512, buf, 12));
    assert(!memcmp(buf, seed, 12));
    EXPECT(trap(rt, pid, tid, IOS27_23D_SYS_OPEN, 600, 1, 0, 0, 0, 0).status, IOS27_23D_DENIED);
    CHECK(ios27_23d_memory_protect(rt, pid, 1, IOS27_23D_READ));
    EXPECT(trap(rt, pid, tid, IOS27_23D_SYS_READ, fd, 4096, 4, 0, 0, 0).status, IOS27_23D_FAULT);
    CHECK(ios27_23d_memory_protect(rt, pid, 1, IOS27_23D_READ | IOS27_23D_WRITE));
}
static void test_ipc(ios27_23d_runtime *rt, uint64_t client, uint64_t tid,
                     uint64_t server, uint64_t echo, uint64_t sum, uint64_t relay) {
    const uint8_t bytes[] = {1,2,3,4,5,6,7,8};
    rpc(rt, client, tid, echo, 1024, 2048, 1, IOS27_23D_RPC_ECHO, bytes, 8, 0, 0, 1, IOS27_23D_DENIED);
    CHECK(ios27_23d_service_grant(rt, server, echo, client, 0));
    CHECK(ios27_23d_service_grant(rt, server, sum, client, 0));
    CHECK(ios27_23d_service_grant(rt, server, relay, client, 0));
    rpc(rt, client, tid, echo, 1024, 2048, 1, IOS27_23D_RPC_ECHO, bytes, 8, 0, 0, 1, IOS27_23D_OK);
    rpc(rt, client, tid, relay, 1024, 2048, 2, IOS27_23D_RPC_RELAY, bytes, 8, echo, 0, 1, IOS27_23D_TIMEOUT);
    rpc(rt, client, tid, relay, 1024, 2048, 2, IOS27_23D_RPC_RELAY, bytes, 8, echo, 0, 2, IOS27_23D_OK);
    uint32_t nums[3] = {1, 10, 100};
    ios27_23d_wire req = {0};
    req.opcode = IOS27_23D_RPC_SUM; req.size = 12; req.correlation = 3;
    memcpy(req.data, nums, 12);
    CHECK(ios27_23d_memory_write(rt, client, 1024, &req, 44));
    ios27_23d_result result = trap(rt, client, tid, IOS27_23D_SYS_RPC, sum, 1024, 44, 2048, 160, 1);
    CHECK(result.status);
    ios27_23d_wire response;
    CHECK(ios27_23d_memory_read(rt, client, 2048, &response, 36));
    uint32_t total; memcpy(&total, response.data, 4); assert(total == 111);
    EXPECT(trap(rt, client, tid, IOS27_23D_SYS_RPC, sum, 1024, 44, 2048, 160, 1).status, IOS27_23D_INVALID);
    rpc(rt, client, tid, echo, 1024, 2048, 4, IOS27_23D_RPC_ECHO, bytes, 8, 0, 0, 0, IOS27_23D_TIMEOUT);
    uint8_t sentinel[160]; memset(sentinel, 0xA7, sizeof(sentinel));
    CHECK(ios27_23d_memory_write(rt, client, 2048, sentinel, sizeof(sentinel)));
    rpc(rt, client, tid, echo, 1024, 2048, 4, IOS27_23D_RPC_ECHO, bytes, 8, 0, 0, 1, IOS27_23D_OK);
    CHECK(ios27_23d_memory_write(rt, client, 2048, sentinel, sizeof(sentinel)));
    req.opcode = IOS27_23D_RPC_ECHO; req.size = 8; req.correlation = 5; req.transfer = 0;
    memcpy(req.data, bytes, 8);
    CHECK(ios27_23d_memory_write(rt, client, 1024, &req, 40));
    EXPECT(trap(rt, client, tid, IOS27_23D_SYS_RPC, echo, 1024, 40, 2048, 34, 1).status, IOS27_23D_LIMIT);
    uint8_t check[160]; CHECK(ios27_23d_memory_read(rt, client, 2048, check, sizeof(check)));
    assert(!memcmp(check, sentinel, sizeof(check)));
    CHECK(ios27_23d_service_destroy(rt, server, sum));
    EXPECT(trap(rt, client, tid, IOS27_23D_SYS_RPC, sum, 1024, 40, 2048, 160, 1).status, IOS27_23D_DEAD);
    EXPECT(ios27_23d_service_grant(rt, server, sum, client, 0), IOS27_23D_DEAD);
    /* Cross-task capability transfer is committed only after a valid reply. */
    uint64_t third, third_port, server_tid;
    CHECK(ios27_23d_process_create(rt, &third));
    CHECK(ios27_23d_thread_create(rt, server, &server_tid));
    CHECK(ios27_23d_service_register(rt, third, 99, IOS27_23D_SERVICE_ECHO, &third_port));
    CHECK(ios27_23d_service_grant(rt, third, third_port, client, 0));
    rpc(rt, server, server_tid, third_port, 256, 1024, 1,
        IOS27_23D_RPC_ECHO, bytes, 8, 0, 0, 1, IOS27_23D_DENIED);
    rpc(rt, client, tid, echo, 1024, 2048, 6, IOS27_23D_RPC_ECHO,
        bytes, 8, third_port, IOS27_23D_RPC_TRANSFER_SEND, 1, IOS27_23D_OK);
    rpc(rt, server, server_tid, third_port, 256, 1024, 1,
        IOS27_23D_RPC_ECHO, bytes, 8, 0, 0, 1, IOS27_23D_OK);
    CHECK(ios27_23d_process_destroy(rt, third));
    rpc(rt, server, server_tid, third_port, 256, 1024, 2,
        IOS27_23D_RPC_ECHO, bytes, 8, 0, 0, 1, IOS27_23D_DEAD);
    CHECK(ios27_23d_thread_destroy(rt, server, server_tid));
}
typedef struct {
    ios27_23d_runtime *rt; uint64_t pid, tid, port, reqoff, outoff;
    uint32_t seed; unsigned iterations; int failures;
} worker_args;
static uint32_t next_random(uint32_t *seed) {
    uint32_t x = *seed;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *seed = x; return x;
}
static void *worker(void *arg) {
    worker_args *w = arg;
    for (unsigned i = 1; i <= w->iterations; ++i) {
        ios27_23d_wire req = {0};
        uint32_t number = next_random(&w->seed);
        req.opcode = IOS27_23D_RPC_ECHO; req.correlation = i;
        req.size = 8; memcpy(req.data, &number, sizeof(number));
        memcpy(req.data + 4, &i, sizeof(i));
        if (ios27_23d_memory_write(w->rt, w->pid, w->reqoff, &req, 40) != 0) { ++w->failures; break; }
        ios27_23d_result r = trap(w->rt, w->pid, w->tid, IOS27_23D_SYS_RPC,
                                  w->port, w->reqoff, 40, w->outoff, 160, 1);
        if (r.status != 0 || r.value != 40) { ++w->failures; break; }
        ios27_23d_wire reply = {0};
        if (ios27_23d_memory_read(w->rt, w->pid, w->outoff, &reply, 40) ||
            reply.correlation != i || memcmp(req.data, reply.data, 8)) { ++w->failures; break; }
    }
    return NULL;
}
static void test_races(ios27_23d_runtime *rt, uint64_t client, uint64_t server, uint64_t echo) {
    pthread_t workers[4]; worker_args args[4];
    for (unsigned i = 0; i < 4; ++i) {
        uint64_t thread;
        CHECK(ios27_23d_thread_create(rt, client, &thread));
        args[i] = (worker_args){rt, client, thread, echo, 4096 + 256 * i,
                                8192 + 256 * i, UINT32_C(0x12345678) + i, 30000, 0};
        assert(pthread_create(&workers[i], NULL, worker, &args[i]) == 0);
    }
    for (unsigned i = 0; i < 4; ++i) {
        assert(pthread_join(workers[i], NULL) == 0);
        assert(args[i].failures == 0);
        CHECK(ios27_23d_thread_destroy(rt, client, args[i].tid));
    }
    /* A consumed send-once right cannot be used twice. */
    uint64_t third, once_tid;
    CHECK(ios27_23d_process_create(rt, &third));
    CHECK(ios27_23d_thread_create(rt, third, &once_tid));
    CHECK(ios27_23d_service_grant(rt, server, echo, third, 1));
    const char value[] = "once";
    rpc(rt, third, once_tid, echo, 512, 1024, 1, IOS27_23D_RPC_ECHO,
        value, sizeof(value), 0, 0, 1, IOS27_23D_OK);
    rpc(rt, third, once_tid, echo, 512, 1024, 2, IOS27_23D_RPC_ECHO,
        value, sizeof(value), 0, 0, 1, IOS27_23D_DENIED);
    CHECK(ios27_23d_process_destroy(rt, third));
    EXPECT(ios27_23d_thread_cancel(rt, third, once_tid), IOS27_23D_DEAD);
}
static void test_malformed(ios27_23d_runtime *rt, uint64_t pid, uint64_t tid, uint64_t port) {
    uint32_t seed = UINT32_C(0x9e3779b9);
    ios27_23d_wire req = {0};
    req.opcode = IOS27_23D_RPC_ECHO; req.size = 4; req.correlation = 900000;
    uint8_t snapshot[160]; memset(snapshot, 0x44, sizeof(snapshot));
    CHECK(ios27_23d_memory_write(rt, pid, 13000, &req, 36));
    CHECK(ios27_23d_memory_write(rt, pid, 14000, snapshot, sizeof(snapshot)));
    for (unsigned i = 0; i < 12000; ++i) {
        uint32_t r = next_random(&seed);
        uint64_t offset = (r & 1u) ? UINT64_MAX - (r & 63u) : 16300u + (r & 1023u);
        ios27_23d_result result = trap(rt, pid, tid, IOS27_23D_SYS_RPC,
                                       port, offset, 36, 14000, 160, 1);
        assert(result.status != IOS27_23D_OK);
    }
    /* Malformed headers and unsupported dispositions must never commit. */
    for (unsigned i = 0; i < 256; ++i) {
        req.flags = (i & 1u) ? UINT32_MAX : 0;
        req.reserved = (i & 1u) ? 0 : i + 1;
        CHECK(ios27_23d_memory_write(rt, pid, 13000, &req, 36));
        EXPECT(trap(rt, pid, tid, IOS27_23D_SYS_RPC,
                    port, 13000, 36, 14000, 160, 1).status, IOS27_23D_INVALID);
    }
    uint8_t after[160];
    CHECK(ios27_23d_memory_read(rt, pid, 14000, after, sizeof(after)));
    assert(memcmp(after, snapshot, 160) == 0);
    EXPECT(trap(rt, pid, tid, UINT32_MAX, 0, 0, 0, 0, 0, 0).status, IOS27_23D_UNSUPPORTED);
    ios27_23d_frame bad = {0}; ios27_23d_result result;
    EXPECT(ios27_23d_trap(rt, pid, tid, &bad, &result), IOS27_23D_BAD_ABI);
    CHECK(ios27_23d_thread_cancel(rt, pid, tid));
    EXPECT(trap(rt, pid, tid, IOS27_23D_SYS_GETPID, 0, 0, 0, 0, 0, 0).status, IOS27_23D_INTERRUPTED);
}
int main(void) {
    ios27_23d_runtime *rt;
    CHECK(ios27_23d_new(&rt));
    uint64_t server, client, tid, echo, sum, relay;
    CHECK(ios27_23d_process_create(rt, &server));
    CHECK(ios27_23d_process_create(rt, &client));
    CHECK(ios27_23d_thread_create(rt, client, &tid));
    CHECK(ios27_23d_service_register(rt, server, 10, IOS27_23D_SERVICE_ECHO, &echo));
    CHECK(ios27_23d_service_register(rt, server, 11, IOS27_23D_SERVICE_SUM, &sum));
    CHECK(ios27_23d_service_register(rt, server, 12, IOS27_23D_SERVICE_RELAY, &relay));
    EXPECT(ios27_23d_service_register(rt, server, 12, IOS27_23D_SERVICE_ECHO, &sum), IOS27_23D_BUSY);
    test_fds(rt, client, tid);
    test_ipc(rt, client, tid, server, echo, sum, relay);
    test_races(rt, client, server, echo);
    test_malformed(rt, client, tid, echo);
    CHECK(ios27_23d_process_destroy(rt, server));
    EXPECT(ios27_23d_service_grant(rt, server, echo, client, 0), IOS27_23D_DEAD);
    CHECK(ios27_23d_process_destroy(rt, client));
    for (unsigned i = 0; i < 200; ++i) {
        uint64_t p, thread, port;
        CHECK(ios27_23d_process_create(rt, &p));
        CHECK(ios27_23d_thread_create(rt, p, &thread));
        CHECK(ios27_23d_service_register(rt, p, 1, IOS27_23D_SERVICE_ECHO, &port));
        CHECK(ios27_23d_process_destroy(rt, p));
        EXPECT(ios27_23d_service_grant(rt, p, port, p, 0), IOS27_23D_DEAD);
    }
    ios27_23d_stats stats;
    ios27_23d_get_stats(rt, &stats);
    assert(stats.rpc_calls >= 120000 && stats.calls >= 130000 && stats.port_deaths >= 200);
    printf("phase23d: calls=%llu rpc=%llu fd=%llu rejects=%llu copies=%llu nested=%llu deaths=%llu\n",
           (unsigned long long)stats.calls, (unsigned long long)stats.rpc_calls,
           (unsigned long long)stats.fd_operations, (unsigned long long)stats.rejects,
           (unsigned long long)stats.copies, (unsigned long long)stats.nested_calls,
           (unsigned long long)stats.port_deaths);
    ios27_23d_free(rt);
    return 0;
}
