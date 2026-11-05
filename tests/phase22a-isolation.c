#include "ios27/phase22a_space.h"
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) assert((x) == IOS27_22A_OK)

static void must_fault(volatile unsigned char *address) {
    pid_t pid = fork();
    int status = 0;
    assert(pid >= 0);
    if (pid == 0) {
        struct rlimit no_core = {0u, 0u};
        (void)setrlimit(RLIMIT_CORE, &no_core);
        *address = 0x55u; /* Child-only deliberate protected-page fault. */
        _exit(4);
    }
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
}

int main(void) {
    ios27_22a_config config = {65536u, 4096u, 2u};
    ios27_22a_space *space = NULL;
    ios27_22a_view view;
    uint64_t handle = 0u;
    CHECK(ios27_22a_create(&config, &space));
    CHECK(ios27_22a_map_stack(space, 0x4000u, 4096u, &handle));
    CHECK(ios27_22a_pin(space, 0x4000u, 4096u, 1u, IOS27_22A_READ, &view));
    must_fault((volatile unsigned char *)view.host - 1);
    must_fault((volatile unsigned char *)view.host + 4096u);
    CHECK(ios27_22a_unpin(space, &view));
    CHECK(ios27_22a_protect(space, handle, IOS27_22A_READ | IOS27_22A_EXEC));
    CHECK(ios27_22a_pin(space, 0x4000u, 1u, 1u, IOS27_22A_READ, &view));
    must_fault((volatile unsigned char *)view.host);
    CHECK(ios27_22a_unpin(space, &view));
    CHECK(ios27_22a_destroy(&space));
    puts("phase22a isolation: 3 child-only SIGSEGVs confirm guards and RX write denial");
    return 0;
}
