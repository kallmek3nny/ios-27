#define _POSIX_C_SOURCE 200809L
#include "ios27/phase22e_ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void need(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "phase22e replies: %s\n", message); abort(); }
}
static phase22e_header request(uint32_t dest, uint32_t reply, int32_t id) {
    phase22e_header h = {0};
    h.size = sizeof(h); h.bits = PHASE22E_COPY_SEND | (PHASE22E_MAKE_SEND_ONCE << 8);
    h.remote_port = dest; h.local_port = reply; h.id = id;
    return h;
}
int main(void) {
    phase22e_space *s = phase22e_new();
    need(s != NULL, "new space");
    uint32_t service = 0, reply = 0;
    need(!phase22e_port_create(s, 2, &service), "service port");
    need(!phase22e_port_create(s, 1, &reply), "reply port");
    need(!phase22e_make_send(s, 2, service, 1), "grant service right");
    enum { BURST = PHASE22E_QUEUE };
    for (unsigned i = 0; i < BURST; ++i) {
        phase22e_header m = request(service, reply, (int32_t)i + 1000);
        need(!phase22e_send(s, 1, &m, sizeof(m), NULL, 0, 0), "enqueue request");
    }
    phase22e_header received[BURST];
    for (unsigned i = 0; i < BURST; ++i) {
        size_t len = sizeof(received[i]), ool = 0;
        need(!phase22e_receive(s, 2, service, &received[i], &len, NULL, &ool, 0),
             "service request");
        need(received[i].id == (int32_t)i + 1000 &&
             received[i].remote_port == reply &&
             (received[i].bits & 255u) == PHASE22E_MOVE_SEND_ONCE,
             "in-flight message correlation");
    }
    phase22e_rights rights;
    need(!phase22e_rights_get(s, 2, reply, &rights) && rights.send_once == BURST,
         "all reply rights delivered");
    for (unsigned i = BURST; i > 0; --i) {
        phase22e_header m = {0};
        m.bits = PHASE22E_MOVE_SEND_ONCE;
        m.size = sizeof(m); m.remote_port = reply;
        m.id = received[i - 1].id + 100;
        need(!phase22e_send(s, 2, &m, sizeof(m), NULL, 0, 0), "out-of-order reply");
    }
    for (unsigned i = BURST; i > 0; --i) {
        phase22e_header got;
        size_t len = sizeof(got), ool = 0;
        need(!phase22e_receive(s, 1, reply, &got, &len, NULL, &ool, 0),
             "receive out-of-order reply");
        need(got.id == (int32_t)(i - 1) + 1100, "reply id correlation");
    }
    need(!phase22e_rights_get(s, 2, reply, &rights) && rights.send_once == 0,
         "no recycled send-once right");
    need(!phase22e_destroy(s, 2, service), "service shutdown");
    need(!phase22e_destroy(s, 1, reply), "reply shutdown");
    phase22e_stats stats;
    need(!phase22e_stats_get(s, &stats) && stats.sends == BURST * 2 &&
         stats.receives == BURST * 2 && !stats.in_flight_transfers &&
         !stats.messages_pending, "accounting");
    printf("phase22e replies: %u deliberately out-of-order replies verified\n", BURST);
    phase22e_free(s);
    return 0;
}
