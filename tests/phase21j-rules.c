#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j rules:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { ACTORS = 8, PROBES = 10000 };
static void forced_pass_and_failure(void) {
    ios27_21j_diagnostics *d = NULL;
    ios27_21j_snapshot_data stats;
    ios27_21j_lease a, rejected;
    CHECK(ios27_21j_create(&d, 1, 99, 10000) == 0);
    CHECK(ios27_21j_rule_add(NULL, 9, 7, 2, 0) == EINVAL);
    CHECK(ios27_21j_rule_add(d, 0, 7, 2, 0) == EINVAL);
    CHECK(ios27_21j_rule_add(d, 9, 7, 2, 2) == EINVAL);
    CHECK(ios27_21j_rule_add(d, 9, 7, 2, 0) == 0);
    CHECK(ios27_21j_rule_add(d, 9, 7, 3, 1) == 0);
    CHECK(ios27_21j_rule_add(d, 9, 7, 2, 1) == EEXIST);
    CHECK(ios27_21j_probe(d, 9, 7, 2) == 0);
    CHECK(ios27_21j_probe(d, 9, 7, 3) == ENOMEM);
    CHECK(ios27_21j_probe(d, 5, 7, 2) == ENOMEM);
    CHECK(ios27_21j_rule_add(d, 9, 7, 4, 0) == EBUSY);
    CHECK(ios27_21j_rules_clear(d) == EBUSY);
    CHECK(ios27_21j_acquire_injected(d, 9, 7, 2, &a) == 0);
    CHECK(ios27_21j_release(d, a) == 0);
    CHECK(ios27_21j_acquire_injected(d, 9, 7, 3, &rejected) == ENOMEM);
    CHECK(!rejected.slot && !rejected.owner && !rejected.generation);
    CHECK(ios27_21j_snapshot(d, &stats) == 0);
    CHECK(stats.probes == 5 && stats.injections == 3);
    CHECK(stats.active == 0 && stats.leases_created == 1);
    CHECK(stats.leases_created == stats.leases_released);
    CHECK(ios27_21j_destroy(d) == 0);
}
static void bounded_table_and_reset(void) {
    ios27_21j_diagnostics *d = NULL;
    ios27_21j_snapshot_data stats;
    CHECK(ios27_21j_create(&d, 1, 100, 0) == 0);
    CHECK(ios27_21j_rules_clear(d) == 0);
    for (uint64_t i = 0; i < IOS27_21J_MAX_FAULT_RULES; ++i)
        CHECK(ios27_21j_rule_add(d, 1, 2, i, 1) == 0);
    CHECK(ios27_21j_rule_add(d, 1, 2, 5, 0) == EEXIST);
    CHECK(ios27_21j_rule_add(d, 1, 2, IOS27_21J_MAX_FAULT_RULES,
                             1) == ENOSPC);
    CHECK(ios27_21j_rules_clear(d) == 0);
    CHECK(ios27_21j_rule_add(d, 3, 2, 17, 1) == 0);
    CHECK(ios27_21j_rule_add(d, 3, 2, 18, 0) == 0);
    for (uint64_t i = 0; i < PROBES; ++i) {
        int rc = ios27_21j_probe(d, 3, 2, i);
        CHECK(rc == (i == 17 ? ENOMEM : 0));
    }
    CHECK(ios27_21j_snapshot(d, &stats) == 0);
    CHECK(stats.probes == PROBES && stats.injections == 1);
    CHECK(ios27_21j_destroy(d) == 0);
}
typedef struct { ios27_21j_diagnostics *d; uint32_t actor; } arg;
static void *worker(void *value) {
    arg *a = value;
    for (uint64_t i = 0; i < PROBES; ++i) {
        int rc = ios27_21j_probe(a->d, 99, a->actor, i);
        CHECK(rc == (i == 7 ? ENOMEM : 0));
    }
    return NULL;
}
static void concurrently_probed_immutable_rules(void) {
    ios27_21j_diagnostics *d = NULL;
    ios27_21j_snapshot_data stats;
    pthread_t ids[ACTORS];
    arg args[ACTORS];
    CHECK(ios27_21j_create(&d, 1, 0, 0) == 0);
    for (unsigned actor = 0; actor < ACTORS; ++actor)
        CHECK(ios27_21j_rule_add(d, 99, actor, 7, 1) == 0);
    for (unsigned actor = 0; actor < ACTORS; ++actor) {
        args[actor] = (arg){d, actor};
        CHECK(pthread_create(&ids[actor], NULL, worker, &args[actor]) == 0);
    }
    for (unsigned actor = 0; actor < ACTORS; ++actor)
        CHECK(pthread_join(ids[actor], NULL) == 0);
    CHECK(ios27_21j_snapshot(d, &stats) == 0);
    CHECK(stats.probes == ACTORS * PROBES && stats.injections == ACTORS);
    CHECK(stats.active == 0 && stats.leases_created == stats.leases_released);
    CHECK(ios27_21j_destroy(d) == 0);
}
int main(void) {
    forced_pass_and_failure();
    bounded_table_and_reset();
    concurrently_probed_immutable_rules();
    puts("phase21j rules: bounded exact override table, 90k deterministic concurrent probes; PASS");
    return 0;
}
