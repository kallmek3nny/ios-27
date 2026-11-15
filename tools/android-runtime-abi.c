/* Query the target libc's ABI constants needed by a cross-built runtime.
 * No settings, files, namespaces or persistent state are changed. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
#if !defined(__ANDROID__) || !defined(__aarch64__)
    fputs("requires native ARM64 Android\n", stderr);
    return 2;
#else
    printf("{\"architecture\":\"aarch64\",\"pointer_bits\":%zu,"
           "\"page_size\":%ld,\"sigrtmin\":%d,\"sigrtmax\":%d,"
           "\"uid\":%lu,\"named_semaphore_symbol\":%s}\n",
           sizeof(void *) * 8u, sysconf(_SC_PAGESIZE), SIGRTMIN, SIGRTMAX,
           (unsigned long)getuid(), dlsym(RTLD_DEFAULT, "sem_open") ? "true" : "false");
    return 0;
#endif
}
