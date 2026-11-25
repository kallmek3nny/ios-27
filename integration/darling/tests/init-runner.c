/* Custom-init supervisor for headless Darwin runtime checks. Arguments after
 * argv[0] are supplied as newline-separated IOS27_INIT_ARGS, at most 14 entries.
 * The child uses the actual guest exec/exit path; the report records waitpid(). */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int parse_id(const char *text, unsigned int *value) {
    if (!text || text[0] < '0' || text[0] > '9') return 0;
    char *end;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || *end || parsed > UINT_MAX) return 0;
    *value = (unsigned int)parsed;
    return 1;
}

static void report(const char *path, pid_t child, int waited, int status, int error) {
    FILE *output = fopen(path, "w");
    if (!output) return;
    fprintf(output, "{\"child\":%ld,\"waited\":%s,\"exited\":%s,\"exit_code\":%d,\"signal\":%d,\"error\":%d}\n",
            (long)child, waited ? "true" : "false", waited && WIFEXITED(status) ? "true" : "false",
            waited && WIFEXITED(status) ? WEXITSTATUS(status) : -1,
            waited && WIFSIGNALED(status) ? WTERMSIG(status) : 0, error);
    fclose(output);
}

int main(void) {
    const char *program = getenv("IOS27_INIT_PROGRAM");
    const char *result = getenv("IOS27_INIT_REPORT");
    const char *output = getenv("IOS27_INIT_OUTPUT");
    const char *arguments = getenv("IOS27_INIT_ARGS");
    if (!program || program[0] != '/' || !result || !output) return 2;
    const char *uid_text = getenv("IOS27_INIT_UID"), *gid_text = getenv("IOS27_INIT_GID");
    unsigned int uid = 0, gid = 0;
    int change_ids = uid_text || gid_text;
    if (change_ids && (!parse_id(uid_text, &uid) || !parse_id(gid_text, &gid))) return 7;
    char buffer[8192];
    char *argv[16] = {(char *)program, NULL};
    if (arguments) {
        if (strlen(arguments) >= sizeof(buffer)) return 3;
        strcpy(buffer, arguments);
        char *save = NULL;
        size_t count = 1;
        for (char *arg = strtok_r(buffer, "\n", &save); arg; arg = strtok_r(NULL, "\n", &save)) {
            if (count == 15) return 4;
            argv[count++] = arg;
        }
        argv[count] = NULL;
    }
    int fd = open(output, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) { report(result, -1, 0, 0, errno); return 5; }
    pid_t child = fork();
    if (child < 0) { int error = errno; close(fd); report(result, -1, 0, 0, error); return 6; }
    if (child == 0) {
        if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) _exit(126);
        if (fd > STDERR_FILENO) close(fd);
        if (change_ids && (setgid((gid_t)gid) || setuid((uid_t)uid)
            || getegid() != (gid_t)gid || geteuid() != (uid_t)uid)) {
            fputs("guest credential switch failed\n", stderr);
            _exit(125);
        }
        execv(program, argv);
        perror("guest execv");
        _exit(127);
    }
    close(fd);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    report(result, child, waited == child, status, waited < 0 ? errno : 0);
    // The supervisor remains alive until the external test controller ends it.
    for (;;) pause();
}
