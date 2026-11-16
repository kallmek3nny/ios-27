/* Native observable spawn-policy contract, including exec inheritance. */
#include <crt_externs.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s errno=%d\n", __LINE__, #c, errno); exit(1); } } while (0)

static void spawn(const char *path, posix_spawnattr_t *attributes, int expected) {
    char wanted[2] = {(char)('0' + expected), '\0'};
    char *arguments[] = {(char *)path, "--child", wanted, NULL};
    pid_t pid;
    CHECK(posix_spawn(&pid, path, NULL, attributes, arguments, *_NSGetEnviron()) == 0);
    int status;
    CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(int argc, char **argv) {
    alarm(12);
    if (argc == 3 && strcmp(argv[1], "--child") == 0) {
        CHECK(getiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_PROCESS) == argv[2][0] - '0');
        return 0;
    }
    int (*set_policy)(posix_spawnattr_t *, int);
    void *symbol = dlsym(RTLD_DEFAULT, "posix_spawnattr_setdataless_iopolicy_np");
    CHECK(symbol != NULL && sizeof(symbol) == sizeof(set_policy));
    memcpy(&set_policy, &symbol, sizeof(set_policy));
    posix_spawnattr_t attributes = NULL;
    CHECK(set_policy(NULL, 1) == EINVAL && set_policy(&attributes, 1) == EINVAL);
    CHECK(setiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_PROCESS, 2) == 0);
    char path[4096];
    uint32_t capacity = sizeof(path);
    CHECK(_NSGetExecutablePath(path, &capacity) == 0);
    // An inherited private environment key cannot override the actual policy.
    CHECK(setenv("__darling_dataless_policy", "2", 1) == 0);
    for (int policy = 0; policy <= 2; ++policy) {
        CHECK(posix_spawnattr_init(&attributes) == 0);
        CHECK(set_policy(&attributes, policy) == 0);
        CHECK(getiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_PROCESS) == 2);
        spawn(path, &attributes, policy == 2 ? 2 : 1);
        CHECK(posix_spawnattr_destroy(&attributes) == 0);
        CHECK(set_policy(&attributes, 1) == EINVAL);
    }
    CHECK(setiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_PROCESS, 1) == 0);
    spawn(path, NULL, 1);
    CHECK(posix_spawnattr_init(&attributes) == 0);
    CHECK(set_policy(&attributes, -1) == 0);
    pid_t pid = -1;
    char *arguments[] = {path, NULL};
    CHECK(posix_spawn(&pid, path, NULL, &attributes, arguments, *_NSGetEnviron()) == EINVAL && pid == -1);
    CHECK(set_policy(&attributes, 2) == 0);
    CHECK(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETEXEC) == 0);
    CHECK(posix_spawn(&pid, "/ios27-no-such-policy-executable", NULL, &attributes, arguments, *_NSGetEnviron()) == ENOENT);
    CHECK(getiopolicy_np(IOPOL_TYPE_VFS_MATERIALIZE_DATALESS_FILES, IOPOL_SCOPE_PROCESS) == 1);
    CHECK(posix_spawnattr_destroy(&attributes) == 0);
    printf("DARWIN_SPAWN_POLICY_OK children=4 checks=%u inherit=ok parent=unchanged failed_exec=restored\n", checks);
    return 0;
}
