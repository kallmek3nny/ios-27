#include "ios27/phase22a_space.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define OK(x) assert((x) == IOS27_22A_OK)
#define ERR(x, e) assert((x) == (e))

int main(void) {
    ios27_22a_config cfg = {0x40000u, 4096u * 3u, 2u};
    ios27_22a_space *s = NULL;
    ios27_22a_stats before, after;
    ios27_22a_view a, b;
    uint64_t first = 0u, second = 0u, unused = 0u;
    OK(ios27_22a_create(&cfg, &s));
    OK(ios27_22a_fail_map_after(s, 1u));
    OK(ios27_22a_map(s, 0x4000u, 4096u, 3u, 0u, &first));
    OK(ios27_22a_snapshot(s, &before));
    ERR(ios27_22a_map(s, 0x8000u, 4096u, 3u, 0u, &unused), IOS27_22A_LIMIT);
    assert(unused == 0u);
    OK(ios27_22a_snapshot(s, &after));
    assert(after.committed == before.committed && after.regions == before.regions);
    OK(ios27_22a_map(s, 0x8000u, 4096u, 3u, 0u, &second));
    ERR(ios27_22a_map(s, 0xc000u, 4096u, 3u, 0u, &unused), IOS27_22A_LIMIT);
    OK(ios27_22a_pin(s, 0x4000u, 16u, 1u, 3u, &a));
    OK(ios27_22a_pin(s, 0x4000u, 16u, 1u, 3u, &b));
    ERR(ios27_22a_destroy(&s), IOS27_22A_BUSY);
    assert(s != NULL);
    OK(ios27_22a_unpin(s, &a));
    ERR(ios27_22a_unmap(s, first), IOS27_22A_BUSY);
    OK(ios27_22a_unpin(s, &b));
    OK(ios27_22a_unmap(s, first));
    OK(ios27_22a_unmap(s, second));
    OK(ios27_22a_snapshot(s, &after));
    assert(after.regions == 0u && after.pins == 0u && after.committed == 0u);
    OK(ios27_22a_destroy(&s));
    puts("phase22a faults: injected rollback, region quota, pin exhaustion, destroy retry ok");
    return 0;
}
