#include "ios27/phase22a_space.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define OK(x) assert((x) == IOS27_22A_OK)
#define ERR(x, code) assert((x) == (code))

int main(void) {
    ios27_22a_config cfg = {0x100000u, 0x10000u, 5u};
    ios27_22a_space *s = NULL;
    ios27_22a_stats stats;
    uint64_t a = 0, b = 0, guarded = 0, reused = 0, extra = 0;
    uint8_t output[32], readback[32];
    ios27_22a_view view, duplicate, stale;
    memset(output, 0xa5, sizeof(output));
    memset(readback, 0x7b, sizeof(readback));
    OK(ios27_22a_create(&cfg, &s));
    ERR(ios27_22a_map(s, 0u, 4096u, 3u, 0u, &extra), IOS27_22A_INVALID);
    ERR(ios27_22a_map(s, UINT64_MAX - 4095u, 4096u, 3u, 0u, &extra), IOS27_22A_INVALID);
    ERR(ios27_22a_map(s, 0x10000u, 4096u, 7u, 0u, &extra), IOS27_22A_INVALID);
    ERR(ios27_22a_map(s, 0xff000u, 4096u, 3u, IOS27_22A_GUARD_AFTER, &extra), IOS27_22A_LIMIT);
    OK(ios27_22a_map(s, 0x10000u, 4096u, 3u, 0u, &a));
    OK(ios27_22a_map(s, 0x11000u, 4096u, 3u, 0u, &b));
    OK(ios27_22a_copyout(s, 0x10ff0u, output, sizeof(output)));
    OK(ios27_22a_copyin(s, readback, 0x10ff0u, sizeof(readback)));
    assert(memcmp(output, readback, sizeof(output)) == 0);
    ERR(ios27_22a_probe(s, 0x10001u, 4u, 4u, IOS27_22A_READ), IOS27_22A_INVALID);
    ERR(ios27_22a_probe(s, UINT64_MAX - 8u, 32u, 1u, IOS27_22A_READ), IOS27_22A_INVALID);
    ERR(ios27_22a_probe(s, 0x12000u, 1u, 1u, IOS27_22A_READ), IOS27_22A_FAULT);
    OK(ios27_22a_protect(s, b, IOS27_22A_READ));
    memset(output, 0x56, sizeof(output));
    ERR(ios27_22a_copyout(s, 0x10ff0u, output, sizeof(output)), IOS27_22A_PERM);
    OK(ios27_22a_copyin(s, readback, 0x10ff0u, sizeof(readback)));
    assert(readback[0] == 0xa5);
    OK(ios27_22a_protect(s, b, IOS27_22A_READ | IOS27_22A_EXEC));
    ERR(ios27_22a_protect(s, b, 7u), IOS27_22A_INVALID);
    ERR(ios27_22a_copyout(s, 0x11000u, output, sizeof(output)), IOS27_22A_PERM);
    OK(ios27_22a_protect(s, b, 3u));
    OK(ios27_22a_map_stack(s, 0x20000u, 4096u, &guarded));
    ERR(ios27_22a_probe(s, 0x1f000u, 1u, 1u, 1u), IOS27_22A_FAULT);
    ERR(ios27_22a_map(s, 0x1f000u, 4096u, 3u, 0u, &extra), IOS27_22A_BUSY);
    ERR(ios27_22a_map(s, 0x21000u, 4096u, 3u, 0u, &extra), IOS27_22A_BUSY);
    OK(ios27_22a_fail_map_after(s, 0u));
    ERR(ios27_22a_map(s, 0x30000u, 4096u, 3u, 0u, &extra), IOS27_22A_LIMIT);
    OK(ios27_22a_snapshot(s, &stats));
    assert(stats.regions == 3u && stats.committed == 12288u && stats.pins == 0u);
    OK(ios27_22a_map(s, 0x30000u, 4096u, 3u, 0u, &extra));
    OK(ios27_22a_pin(s, 0x10010u, 16u, 8u, 3u, &view));
    duplicate = view;
    stale = view;
    assert(view.host && view.length == 16u && view.token != 0u);
    ERR(ios27_22a_unmap(s, a), IOS27_22A_BUSY);
    ERR(ios27_22a_protect(s, a, 1u), IOS27_22A_BUSY);
    ERR(ios27_22a_close(s), IOS27_22A_BUSY);
    ERR(ios27_22a_pin(s, 0x10ff0u, 32u, 1u, 1u, &duplicate), IOS27_22A_INVALID);
    OK(ios27_22a_unpin(s, &view));
    ERR(ios27_22a_unpin(s, &stale), IOS27_22A_STALE);
    ERR(ios27_22a_unpin(s, &duplicate), IOS27_22A_INVALID);
    OK(ios27_22a_unmap(s, a));
    ERR(ios27_22a_unmap(s, a), IOS27_22A_STALE);
    OK(ios27_22a_map(s, 0x10000u, 4096u, 3u, 0u, &reused));
    assert(reused != a);
    ERR(ios27_22a_protect(s, a, 1u), IOS27_22A_STALE);
    OK(ios27_22a_close(s));
    OK(ios27_22a_snapshot(s, &stats));
    assert(stats.closed == 1u && stats.regions == 0u && stats.committed == 0u);
    ERR(ios27_22a_copyin(s, readback, 0x10000u, 32u), IOS27_22A_CLOSED);
    ERR(ios27_22a_map(s, 0x10000u, 4096u, 3u, 0u, &extra), IOS27_22A_CLOSED);
    OK(ios27_22a_destroy(&s));
    assert(s == NULL);
    printf("phase22a native: guards, W^X, rollback, pins, stale handles, teardown ok\n");
    return 0;
}
