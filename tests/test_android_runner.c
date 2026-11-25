#include "../android/app/src/main/cpp/runner_modes.h"
#include <assert.h>
#include <stdio.h>

/* Host-only contract tests: no guest code, loader, fork or Android device. */
int main(void) {
    const char *names[] = {"success", "trap", "deadline", "raw-syscall"};
    const char *arguments[] = {NULL, "trap", "loop", "raw-syscall"};
    const uint32_t deadlines[] = {2000, 2000, 1000, 2000};
    const ios27_process_outcome outcomes[] = {
        IOS27_PROCESS_RETURNED, IOS27_PROCESS_SIGNALED,
        IOS27_PROCESS_TIMED_OUT, IOS27_PROCESS_SIGNALED
    };
    const int signals[] = {0, SIGTRAP, SIGKILL, SIGSYS};
    const char *passes[] = {
        "PASS: native ARM64 Mach-O returned 42. Named file, mapping and heap checks passed under supervision.",
        "PASS: supervised trap mode: signaled by SIGTRAP.",
        "PASS: supervised deadline mode: timed out and killed by SIGKILL.",
        "PASS: supervised raw-syscall mode: signaled by SIGSYS."
    };
    const char *invalid[] = {"", "loop", "TRAP", "success ", "trap\n", "--mode", "raw-syscall-extra"};
    const int candidate_signals[] = {0, SIGTRAP, SIGKILL, SIGSYS, SIGSEGV, SIGILL, SIGABRT};
    const int candidate_exits[] = {-1, 0, 42, 43, 255};
    size_t checked = 0;
    assert(sizeof(modes) / sizeof(modes[0]) == 4);
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!runner_mode_find(invalid[i]));
    for (size_t i = 0; i < 4; ++i) {
        const runner_mode *mode = runner_mode_find(names[i]);
        assert(mode && mode == &modes[i]);
        assert(mode->timeout_ms == deadlines[i]);
        assert(mode->outcome == outcomes[i] && mode->signal == signals[i]);
        assert(!strcmp(mode->pass, passes[i]));
        if (arguments[i]) assert(!strcmp(mode->guest_argument, arguments[i]));
        else assert(!mode->guest_argument);
        for (int outcome = 0; outcome <= 4; ++outcome) {
            for (size_t s = 0; s < sizeof(candidate_signals) / sizeof(candidate_signals[0]); ++s) {
                for (size_t e = 0; e < sizeof(candidate_exits) / sizeof(candidate_exits[0]); ++e) {
                    ios27_process_result result = {
                        (ios27_process_outcome)outcome, candidate_exits[e], candidate_signals[s]
                    };
                    int expected = result.outcome == outcomes[i] &&
                        (i == 0 ? result.exit_code == 42 : result.signal == signals[i]);
                    assert(runner_mode_passed(mode, &result) == expected);
                    ++checked;
                }
            }
        }
    }
    printf("Android runner modes: %zu outcome combinations and 7 invalid selectors passed\n", checked);
    return 0;
}
