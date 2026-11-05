#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21j_diagnostics.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "phase21j rule-races:%d %s\n", __LINE__, #x); abort(); \
} } while (0)
enum { RACES = 1024 };
typedef struct {
    ios27_21j_diagnostics *diag;
    pthread_barrier_t gate;
    int rule_rc, probe_rc;
} race;
static void *configure(void *value) {
    race *r = value;
    int rc = pthread_barrier_wait(&r->gate);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    r->rule_rc = ios27_21j_rule_add(r->diag, 17, 3, 99, 1);
    return NULL;
}
static void *probe(void *value) {
    race *r = value;
    int rc = pthread_barrier_wait(&r->gate);
    CHECK(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    r->probe_rc = ios27_21j_probe(r->diag, 17, 3, 99);
    return NULL;
}
static void race_script_freeze(void) {
    unsigned won_config = 0, won_probe = 0;
    for (unsigned i = 0; i < RACES; ++i) {
        ios27_21j_snapshot_data stats;
        race r = {0};
        pthread_t config_thread, probe_thread;
        CHECK(ios27_21j_create(&r.diag, 1, i + 1u, 0) == 0);
        CHECK(pthread_barrier_init(&r.gate, NULL, 2) == 0);
        CHECK(pthread_create(&config_thread, NULL, configure, &r) == 0);
        CHECK(pthread_create(&probe_thread, NULL, probe, &r) == 0);
        CHECK(pthread_join(config_thread, NULL) == 0);
        CHECK(pthread_join(probe_thread, NULL) == 0);
        CHECK((r.rule_rc == 0 && r.probe_rc == ENOMEM) ||
              (r.rule_rc == EBUSY && r.probe_rc == 0));
        if (!r.rule_rc) ++won_config;
        else ++won_probe;
        CHECK(ios27_21j_rule_add(r.diag, 17, 3, 99, 1) == EBUSY);
        CHECK(ios27_21j_rules_clear(r.diag) == EBUSY);
        /* Every future probe must retain whichever immutable choice won. */
        for (unsigned attempt = 0; attempt < 8; ++attempt)
            CHECK(ios27_21j_probe(r.diag, 17, 3, 99) == r.probe_rc);
        CHECK(ios27_21j_snapshot(r.diag, &stats) == 0);
        CHECK(stats.probes == 9);
        CHECK(stats.injections == (r.rule_rc == 0 ? 9u : 0u));
        CHECK(stats.active == 0 && stats.leases_created == 0);
        CHECK(pthread_barrier_destroy(&r.gate) == 0);
        CHECK(ios27_21j_destroy(r.diag) == 0);
    }
    CHECK(won_config + won_probe == RACES);
    printf("phase21j rule-races: %u concurrent script-freeze linearization races, %u pre-probe rules, %u frozen wins; PASS\n",
           RACES, won_config, won_probe);
}
int main(void) { race_script_freeze(); return 0; }
