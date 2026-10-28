#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_lockorder.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j modelcheck:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { LOCKS = 16, ROUNDS = 50000, CHOICES = 4 };
static uint64_t scramble(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static int reference_path(const unsigned char edges[LOCKS][LOCKS],
                          uint32_t src, uint32_t target) {
    unsigned char visited[LOCKS] = {0};
    uint32_t queue[LOCKS];
    unsigned head = 0, tail = 0;
    queue[tail++] = src;
    visited[src] = 1;
    while (head < tail) {
        uint32_t node = queue[head++];
        if (node == target) return 1;
        for (uint32_t neighbor = 0; neighbor < LOCKS; ++neighbor) {
            if (!edges[node][neighbor] || visited[neighbor]) continue;
            visited[neighbor] = 1;
            queue[tail++] = neighbor;
        }
    }
    return 0;
}
static void randomized_nesting_model(void) {
    ios27_21j_lockorder *graph = NULL;
    ios27_21j_lockorder_stats stats;
    unsigned char edges[LOCKS][LOCKS] = {{0}};
    uint64_t random_state = UINT64_C(0x21f04451aa);
    uint64_t expected_acquires = 0, expected_cycles = 0;
    size_t expected_edges = 0;
    CHECK(ios27_21j_lockorder_create(&graph, LOCKS, 1) == 0);
    for (unsigned round = 0; round < ROUNDS; ++round) {
        uint32_t held[CHOICES] = {0};
        unsigned nheld = 0;
        for (unsigned attempt = 0; attempt < CHOICES; ++attempt) {
            random_state = scramble(random_state);
            uint32_t candidate = (uint32_t)(random_state % LOCKS);
            int reject = 0;
            for (unsigned i = 0; i < nheld; ++i)
                if (held[i] == candidate ||
                    reference_path(edges, candidate, held[i])) reject = 1;
            int rc = ios27_21j_lockorder_acquire(graph, 0, candidate);
            CHECK(rc == (reject ? EDEADLK : 0));
            if (reject) {
                ++expected_cycles;
                continue;
            }
            for (unsigned i = 0; i < nheld; ++i) {
                uint32_t from = held[i];
                if (!edges[from][candidate]) {
                    edges[from][candidate] = 1;
                    ++expected_edges;
                }
            }
            held[nheld++] = candidate;
            ++expected_acquires;
        }
        for (unsigned i = nheld; i; --i)
            CHECK(ios27_21j_lockorder_release(graph, 0,
                                               held[i - 1u]) == 0);
        if ((round & 511u) == 0) {
            CHECK(ios27_21j_lockorder_snapshot(graph, &stats) == 0);
            CHECK(stats.active == 0 && stats.edges == expected_edges);
            CHECK(stats.acquisitions == expected_acquires);
            CHECK(stats.cycles_rejected == expected_cycles);
        }
    }
    CHECK(ios27_21j_lockorder_snapshot(graph, &stats) == 0);
    CHECK(stats.active == 0 && stats.waiting == 0);
    CHECK(stats.acquisitions == expected_acquires &&
          stats.releases == expected_acquires);
    CHECK(stats.cycles_rejected == expected_cycles);
    CHECK(stats.edges == expected_edges);
    CHECK(expected_acquires + expected_cycles == ROUNDS * CHOICES);
    for (uint32_t from = 0; from < LOCKS; ++from) {
        for (uint32_t to = 0; to < LOCKS; ++to) {
            int edge = -1;
            CHECK(ios27_21j_lockorder_edge(graph, from, to, &edge) == 0);
            CHECK(edge == edges[from][to]);
        }
    }
    CHECK(ios27_21j_lockorder_destroy(graph) == 0);
    printf("phase21j modelcheck: %u randomized nested requests, %llu rejected cycles, exact reference graph; PASS\n",
           ROUNDS * CHOICES, (unsigned long long)expected_cycles);
}
int main(void) {
    randomized_nesting_model();
    return 0;
}
