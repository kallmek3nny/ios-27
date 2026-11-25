/* A real fiber migrates between two native workers. Inactive-worker TLS is
 * cleared, reproducing the stale pointer behind the Android server fault. */
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <ucontext.h>
#include <darlingserver/fiber-tls.hpp>

static ucontext_t fiber, roots[2];
static std::atomic<unsigned> turn{0};
static unsigned errors, visits;
static constexpr unsigned rounds = 100;

static void entry() {
    for (unsigned i = 0; i < rounds; ++i) {
        if (DarlingServer::fiberWorkerLocal<unsigned, 0>() != i % 2 + 1) ++errors;
        if (DarlingServer::fiberWorkerLocal<unsigned, 1>() != i % 2 + 11) ++errors;
        ++visits;
        if (swapcontext(&fiber, &roots[i % 2])) std::abort();
    }
    std::abort();
}

static void worker(unsigned id) {
    for (unsigned i = id; i < rounds; i += 2) {
        while (turn.load(std::memory_order_acquire) != i) std::this_thread::yield();
        DarlingServer::fiberWorkerLocal<unsigned, 0>() = id + 1;
        DarlingServer::fiberWorkerLocal<unsigned, 1>() = id + 11;
        if (swapcontext(&roots[id], &fiber)) std::abort();
        DarlingServer::fiberWorkerLocal<unsigned, 0>() = 0;
        DarlingServer::fiberWorkerLocal<unsigned, 1>() = 0;
        turn.store(i + 1, std::memory_order_release);
    }
    while (turn.load(std::memory_order_acquire) != rounds) std::this_thread::yield();
}

int main() {
    void* stack = std::malloc(65536);
    if (!stack || getcontext(&fiber)) return 1;
    fiber.uc_stack.ss_sp = stack; fiber.uc_stack.ss_size = 65536;
    fiber.uc_link = nullptr;
    makecontext(&fiber, entry, 0);
    std::thread first(worker, 0), second(worker, 1);
    first.join(); second.join();
    std::free(stack);
    std::printf("FIBER_TLS_%s migrations=%u errors=%u\n", errors ? "FAIL" : "OK", visits, errors);
    return errors || visits != rounds;
}
