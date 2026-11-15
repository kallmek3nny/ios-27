#ifndef IOS27_ANDROID_RUNNER_MODES_H
#define IOS27_ANDROID_RUNNER_MODES_H

#include "ios27/process.h"
#include <signal.h>
#include <string.h>

/* Debug smoke modes. Each selects one existing path of the reviewed fixture
 * and accepts exactly one supervised outcome; anything else, including a
 * normal return from a failure mode, exits nonzero. Like every supervisor
 * deadline, the deadline mode's timeout includes child setup time. */
typedef struct {
    const char *name;
    const char *guest_argument; /* guest argv[0]; NULL passes no arguments */
    uint32_t timeout_ms;
    ios27_process_outcome outcome;
    int signal; /* checked unless the expected outcome is a return */
    const char *pass;
} runner_mode;

static const runner_mode modes[] = {
    {"success", NULL, 2000, IOS27_PROCESS_RETURNED, 0,
     "PASS: native ARM64 Mach-O returned 42. Named file, mapping and heap checks passed under supervision."},
    {"trap", "trap", 2000, IOS27_PROCESS_SIGNALED, SIGTRAP,
     "PASS: supervised trap mode: signaled by SIGTRAP."},
    {"deadline", "loop", 1000, IOS27_PROCESS_TIMED_OUT, SIGKILL,
     "PASS: supervised deadline mode: timed out and killed by SIGKILL."},
    {"raw-syscall", "raw-syscall", 2000, IOS27_PROCESS_SIGNALED, SIGSYS,
     "PASS: supervised raw-syscall mode: signaled by SIGSYS."},
};

static inline const runner_mode *runner_mode_find(const char *name) {
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
        if (!strcmp(name, modes[i].name)) return &modes[i];
    return NULL;
}

static inline int runner_mode_passed(const runner_mode *mode,
                                     const ios27_process_result *result) {
    return result->outcome == mode->outcome &&
        (mode->outcome == IOS27_PROCESS_RETURNED ? result->exit_code == 42 :
                                                  result->signal == mode->signal);
}

#endif
