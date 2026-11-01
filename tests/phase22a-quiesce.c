#include "ios27/phase22a_space.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define OK(x) assert((x) == IOS27_22A_OK)
#define ERR(x,code) assert((x) == (code))
int main(void) {
    ios27_22a_config cfg = {0x20000u, 8192u, 4u};
    ios27_22a_space *s = NULL;
    ios27_22a_view view, rejected;
    ios27_22a_stats stats;
    uint64_t handle = 0, unused = 0;
    uint8_t probe[4];
    OK(ios27_22a_create(&cfg, &s));
    OK(ios27_22a_map(s, 0x4000u, 4096u, 3u, 3u, &handle));
    OK(ios27_22a_pin(s, 0x4000u, 4u, 1u, 1u, &view));
    ERR(ios27_22a_quiesce(s), IOS27_22A_BUSY);
    OK(ios27_22a_snapshot(s, &stats));
    assert(stats.quiescing && !stats.closed && stats.pins == 1u);
    ERR(ios27_22a_copyin(s, probe, 0x4000u, sizeof(probe)), IOS27_22A_CLOSED);
    ERR(ios27_22a_map(s, 0x8000u, 4096u, 3u, 0u, &unused), IOS27_22A_CLOSED);
    ERR(ios27_22a_probe(s, 0x4000u, 1u, 1u, 1u), IOS27_22A_CLOSED);
    ERR(ios27_22a_pin(s, 0x4000u, 4u, 1u, 1u, &rejected), IOS27_22A_CLOSED);
    ERR(ios27_22a_close(s), IOS27_22A_BUSY);
    OK(ios27_22a_unpin(s, &view));
    OK(ios27_22a_quiesce(s));
    OK(ios27_22a_close(s));
    OK(ios27_22a_snapshot(s, &stats));
    assert(stats.closed && stats.quiescing && !stats.regions && !stats.committed);
    OK(ios27_22a_destroy(&s));
    puts("phase22a quiesce: new operations denied, pins drain, close completes");
    return 0;
}
