#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    const char *name = "/ios27-contract-keen-panda-33";
    Dl_info provider;
    void *symbol = dlsym(RTLD_DEFAULT, "sem_open");
    if (!symbol || !dladdr(symbol, &provider)) return 2;
    printf("sem_open provider: %s\n", provider.dli_fname);
    sem_t *object = sem_open(name, O_CREAT | O_EXCL, 0600, 0);
    if (object == SEM_FAILED) { perror("sem_open"); return 3; }
    int result = 1;
    if (sem_trywait(object) != -1 || errno != EAGAIN) goto cleanup;
    pid_t child = fork();
    if (child < 0) goto cleanup;
    if (child == 0) {
        sem_t *other = sem_open(name, 0);
        if (other == SEM_FAILED) _exit(4);
        if (sem_post(other) || sem_close(other)) _exit(5);
        _exit(0);
    }
    int status;
    if (sem_wait(object) || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status)) goto cleanup;
    if (sem_unlink(name) || sem_post(object) || sem_wait(object)) goto cleanup;
    if (sem_open(name, 0) != SEM_FAILED || errno != ENOENT) goto cleanup;
    result = 0;
cleanup:
    sem_close(object);
    sem_unlink(name);
    if (!result) puts("Named semaphore cross-process and unlink contracts passed.");
    return result;
}
