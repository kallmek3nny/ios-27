#include "ios27/phase22a_space.h"
#include <assert.h>
#include <stdio.h>

int ios27_22a_restricted_probe(void);

int main(void) {
    int result = ios27_22a_restricted_probe();
    if (result == IOS27_22A_LIMIT) {
        puts("phase22a sandbox: unsupported (not evidence of filesystem/network isolation)");
        return 0;
    }
    assert(result == IOS27_22A_OK);
    puts("phase22a sandbox: openat, socket, stdout denied; private pipe allowed");
    return 0;
}
