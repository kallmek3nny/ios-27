#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_diagnostics.h"
#include "ios27/libsystem21j_schedule.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j trace:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { MAX_ACTORS = 8, MAX_STEPS = 1024 };
typedef struct trace_state trace_state;
typedef struct { trace_state *state; uint32_t actor; } trace_actor;
struct trace_state {
    ios27_21j_diagnostics *diagnostics;
    ios27_21j_schedule *schedule;
    uint32_t plan[MAX_STEPS];
    size_t steps;
};
static uint64_t mix(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static int parse(const char *value, uint64_t *out) {
    char *end = NULL;
    unsigned long long n;
    if (!value || !*value || *value == '-') return EINVAL;
    errno = 0;
    n = strtoull(value, &end, 0);
    if (errno || !end || *end || end == value) return EINVAL;
    *out = (uint64_t)n;
    return 0;
}
static void *actor(void *value) {
    trace_actor *a = value;
    trace_state *s = a->state;
    for (size_t step = 0; step < s->steps; ++step) {
        if (s->plan[step] != a->actor) continue;
        CHECK(ios27_21j_turn_acquire(s->schedule, a->actor, step, 15000) == 0);
        int rc = ios27_21j_probe(s->diagnostics, 19, a->actor, step);
        CHECK(rc == 0 || rc == ENOMEM);
        CHECK(ios27_21j_record(s->diagnostics, IOS27_21J_CHECKPOINT,
                                a->actor, rc, (uint32_t)step) == 0);
        CHECK(ios27_21j_turn_release(s->schedule, a->actor, step) == 0);
    }
    return NULL;
}
static uint64_t digest(const ios27_21j_event *events, size_t count) {
    uint64_t h = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < count; ++i) {
        const ios27_21j_event *e = &events[i];
        uint64_t words[] = {
            e->sequence, ((uint64_t)e->actor << 32) | e->kind,
            ((uint64_t)(uint32_t)e->result << 32) | e->detail
        };
        for (size_t j = 0; j < 3; ++j) {
            h ^= words[j];
            h *= UINT64_C(1099511628211);
        }
    }
    return h;
}
int main(int argc, char **argv) {
    uint64_t seed, count_value, actor_value;
    trace_state state = {0};
    pthread_t workers[MAX_ACTORS];
    trace_actor args[MAX_ACTORS];
    ios27_21j_event journal[IOS27_21J_TRACE_CAPACITY];
    ios27_21j_snapshot_data stats;
    size_t count = 0;
    uint64_t first = 0, last = 0;
    int planned = argc == 6 || argc == 7;
    int jsonl = (argc == 5 && strcmp(argv[4], "--jsonl") == 0) ||
                (argc == 7 && strcmp(argv[6], "--jsonl") == 0);
    const char *literal_plan = planned ? argv[5] : NULL;
    if ((argc != 4 && argc != 5 && argc != 6 && argc != 7) ||
        (argc == 5 && !jsonl) ||
        (planned && (strcmp(argv[4], "--plan") != 0 ||
                     (argc == 7 && !jsonl))) ||
        parse(argv[1], &seed) || parse(argv[2], &actor_value) ||
        parse(argv[3], &count_value) ||
        actor_value < 1 || actor_value > MAX_ACTORS ||
        count_value < 1 || count_value > MAX_STEPS) {
        fprintf(stderr, "usage: phase21j-trace SEED ACTORS(1..8) STEPS(1..1024) [--jsonl | --plan ACTOR_DIGITS [--jsonl]]\n");
        return 2;
    }
    state.steps = (size_t)count_value;
    if (planned && strlen(literal_plan) != state.steps) {
        fprintf(stderr, "phase21j trace: plan length must match STEPS\n");
        return 2;
    }
    for (size_t i = 0; i < state.steps; ++i) {
        if (!planned) state.plan[i] = (uint32_t)(mix(seed ^ i) % actor_value);
        else {
            unsigned char ch = (unsigned char)literal_plan[i];
            if (ch < (unsigned char)'0' ||
                ch >= (unsigned char)('0' + actor_value)) {
                fprintf(stderr, "phase21j trace: invalid actor digit at %llu\n",
                        (unsigned long long)i);
                return 2;
            }
            state.plan[i] = (uint32_t)(ch - (unsigned char)'0');
        }
    }
    CHECK(ios27_21j_create(&state.diagnostics, (size_t)actor_value,
                           seed, 1750) == 0);
    CHECK(ios27_21j_schedule_create(&state.schedule, (uint32_t)actor_value,
                                    state.plan, state.steps) == 0);
    for (uint32_t i = 0; i < actor_value; ++i) {
        args[i] = (trace_actor){&state, i};
        CHECK(pthread_create(&workers[i], NULL, actor, &args[i]) == 0);
    }
    for (uint32_t i = 0; i < actor_value; ++i)
        CHECK(pthread_join(workers[i], NULL) == 0);
    CHECK(ios27_21j_snapshot(state.diagnostics, &stats) == 0);
    CHECK(stats.probes == state.steps && stats.next_sequence ==
          state.steps + stats.injections && stats.active == 0);
    CHECK(ios27_21j_export(state.diagnostics, journal,
                            IOS27_21J_TRACE_CAPACITY,
                            &count, &first, &last) == 0);
    CHECK(last == stats.next_sequence &&
          count == (last < IOS27_21J_TRACE_CAPACITY ?
                    last : IOS27_21J_TRACE_CAPACITY));
    for (size_t i = 0; i < count; ++i)
        CHECK(journal[i].sequence == first + i);
    printf("{\"seed\":\"0x%016llx\",\"actors\":%llu,\"steps\":%llu,\"plan\":",
           (unsigned long long)seed, (unsigned long long)actor_value,
           (unsigned long long)count_value);
    if (planned) printf("\"%s\"", literal_plan);
    else fputs("null", stdout);
    printf(",\"first\":%llu,\"last\":%llu,\"saved\":%llu,\"dropped\":%llu,"
           "\"injections\":%llu,\"digest\":\"0x%016llx\"}\n",
           (unsigned long long)first, (unsigned long long)last,
           (unsigned long long)count, (unsigned long long)stats.dropped,
           (unsigned long long)stats.injections,
           (unsigned long long)digest(journal, count));
    if (jsonl) {
        for (size_t i = 0; i < count; ++i) {
            const ios27_21j_event *e = &journal[i];
            printf("{\"seq\":%llu,\"actor\":%u,\"kind\":%u,\"result\":%d,\"detail\":%u}\n",
                   (unsigned long long)e->sequence, e->actor, e->kind,
                   e->result, e->detail);
        }
    }
    CHECK(ios27_21j_schedule_destroy(state.schedule) == 0);
    CHECK(ios27_21j_destroy(state.diagnostics) == 0);
    return 0;
}
