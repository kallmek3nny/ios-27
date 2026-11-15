/* Native Android test: an independent process waits through libc's Linux
 * syscall interface; the simulator shim's raw AArch64 ABI must wake it. */
#define _GNU_SOURCE
#include "../shims/linux-arm64.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int main(void) {
    const char *temporary = getenv("TMPDIR");
    if (!temporary) return 2;
    char path[512];
    int length = snprintf(path, sizeof(path), "%s/ios27-native-syscall-XXXXXX", temporary);
    if (length < 0 || (size_t)length >= sizeof(path)) return 2;
    long page = sysconf(_SC_PAGESIZE);
    int file = mkstemp(path);
    if (page <= 0 || file < 0) return 2;
    unlink(path);
    if (ftruncate(file, page)) { close(file); return 2; }
    _Atomic int *words = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
    close(file);
    if (words == MAP_FAILED) return 2;
    atomic_init(&words[0], 0);
    atomic_init(&words[1], 0);
    int ready[2];
    if (pipe(ready)) { munmap(words, (size_t)page); return 2; }
    pid_t child = fork();
    if (child == 0) {
        close(ready[0]);
        if (write(ready[1], ".", 1) != 1) _exit(3);
        close(ready[1]);
        struct timespec deadline = {3, 0};
        long result = syscall(SYS_futex, words, 0 /* FUTEX_WAIT */, 0, &deadline, NULL, 0);
        if (result != 0) _exit(4);
        atomic_store(&words[1], 27);
        _exit(0);
    }
    close(ready[1]);
    char byte;
    int failed = child < 0 || read(ready[0], &byte, 1) != 1;
    close(ready[0]);
    long woken = 0;
    struct timespec brief = {0, 1000000};
    if (!failed) {
        for (unsigned attempt = 0; attempt < 1000 && woken == 0; ++attempt) {
            woken = ios27_linux_futex_wake_shared(words, 1);
            if (woken == 0) nanosleep(&brief, NULL);
        }
        failed = woken != 1;
    }
    int status = 0;
    if (child > 0) {
        if (failed) kill(child, SIGKILL);
        pid_t waited;
        do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
        failed |= waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0;
    }
    failed |= atomic_load(&words[1]) != 27;
    memset(words, 0x5a, (size_t)page);
    long removed = ios27_linux_madvise_remove(words, (unsigned long)page);
    int zero_filled = removed == 0;
    if (removed == 0) {
        const unsigned char *bytes = (const unsigned char *)words;
        for (long offset = 0; offset < page; ++offset) zero_filled &= bytes[offset] == 0;
        failed |= !zero_filled;
    } else if (removed != -EOPNOTSUPP && removed != -ENOSYS) {
        failed = 1;
    }
    munmap(words, (size_t)page);
    printf("{\"passed\":%s,\"processes_woken\":%ld,\"madvise_result\":%ld,\"removed_range_zero_filled\":%s}\n",
           failed ? "false" : "true", woken, removed, zero_filled ? "true" : "false");
    return failed ? 1 : 0;
}
