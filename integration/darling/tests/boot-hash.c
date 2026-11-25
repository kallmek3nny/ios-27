/* Original contracts for stable per-boot executable allocator identity. */
#define _GNU_SOURCE
#include "boot_hash.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "line %d: %s (errno=%d)\n", __LINE__, #c, errno); exit(1); } } while (0)
static const char boot[] = "01234567-89ab-4cde-8f01-23456789abcd";
static uint8_t identity[20];

static void key_path(char path[4096], const char* directory) {
    CHECK((size_t)snprintf(path, 4096, "%s/.darling-boothash-key", directory) < 4096);
}

int main(int argc, char** argv) {
    CHECK(argc == 2 || (argc == 3 && strcmp(argv[2], "--allow-denied-hardlink") == 0));
    char path[4096], a[41], b[41], c[41], race[4096];
    key_path(path, argv[1]);
    uint8_t key[32];
    for (unsigned i = 0; i < sizeof(key); ++i) key[i] = (uint8_t)i;
    for (unsigned i = 0; i < sizeof(identity); ++i) identity[i] = (uint8_t)i;
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    CHECK(fd >= 0 && write(fd, key, sizeof(key)) == sizeof(key));
    CHECK(close(fd) == 0);
    CHECK(mldr_executable_boothash_for_boot(argv[1], boot, identity, a) == 0);
    // Independent Python hmac/sha256 vector, truncated to 20 bytes.
    CHECK(strcmp(a, "4a108d05cde238c61c7e4b55d5bece4e815892bd") == 0);
    CHECK(mldr_executable_boothash_for_boot(argv[1], boot, identity, b) == 0 && strcmp(a, b) == 0);
    CHECK(mldr_executable_boothash_for_boot(argv[1], "01234567-89AB-4CDE-8F01-23456789ABCD", identity, b) == 0 && strcmp(a, b) == 0);
    CHECK(mldr_executable_boothash_for_boot(argv[1], "11234567-89ab-4cde-8f01-23456789abcd", identity, b) == 0 && strcmp(a, b) != 0);
    identity[0] ^= 1;
    CHECK(mldr_executable_boothash_for_boot(argv[1], boot, identity, b) == 0 && strcmp(a, b) != 0);
    identity[0] ^= 1;
    CHECK(mldr_executable_boothash_for_boot(argv[1], "not-a-boot-uuid", identity, b) == -EINVAL);
    CHECK(mldr_executable_boothash_for_boot(argv[1], "01234567-89ab-4cde-8f01-23456789abcg", identity, b) == -EINVAL);
    CHECK(mldr_executable_boothash(argv[1], identity, b) == 0);
    CHECK(mldr_executable_boothash(argv[1], identity, c) == 0 && strcmp(b, c) == 0);

    CHECK(chmod(path, 0644) == 0);
    CHECK(mldr_executable_boothash_for_boot(argv[1], boot, identity, b) == -EPERM);
    CHECK(chmod(path, 0600) == 0);
    CHECK(truncate(path, 17) == 0);
    CHECK(mldr_executable_boothash_for_boot(argv[1], boot, identity, b) == -EINVAL);
    CHECK(unlink(path) == 0);
    CHECK(symlink("/dev/zero", path) == 0);
    CHECK(mldr_executable_boothash_for_boot(argv[1], boot, identity, b) < 0);
    CHECK(unlink(path) == 0);
    CHECK(mkdir(path, 0700) == 0);
    CHECK(mldr_executable_boothash_for_boot(argv[1], boot, identity, b) < 0);
    CHECK(rmdir(path) == 0);

    CHECK((size_t)snprintf(race, sizeof(race), "%s/race", argv[1]) < sizeof(race));
    CHECK(mkdir(race, 0700) == 0);
    int pipefd[2]; CHECK(pipe(pipefd) == 0);
    enum { CHILDREN = 8 };
    pid_t children[CHILDREN];
    for (unsigned i = 0; i < CHILDREN; ++i) {
        children[i] = fork(); CHECK(children[i] >= 0);
        if (!children[i]) {
            close(pipefd[0]);
            char result[41];
            int ok = mldr_executable_boothash_for_boot(race, boot, identity, result) == 0;
            if (ok) ok = write(pipefd[1], result, sizeof(result)) == sizeof(result);
            _exit(ok ? 0 : 2);
        }
    }
    close(pipefd[1]);
    char results[CHILDREN][41];
    size_t have = 0;
    while (have < sizeof(results)) {
        ssize_t n = read(pipefd[0], (char*)results + have, sizeof(results) - have);
        if (n < 0 && errno == EINTR) continue;
        CHECK(n > 0); have += (size_t)n;
    }
    CHECK(close(pipefd[0]) == 0);
    for (unsigned i = 0; i < CHILDREN; ++i) {
        int status;
        CHECK(waitpid(children[i], &status, 0) == children[i] && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        CHECK(results[i][40] == 0 && strcmp(results[0], results[i]) == 0);
    }
    CHECK(mldr_executable_boothash_for_boot(race, boot, identity, b) == 0 && strcmp(results[0], b) == 0);
    CHECK(strcmp(a, b) != 0);
    key_path(path, race);
    struct stat st; CHECK(stat(path, &st) == 0 && st.st_size == 32 && (st.st_mode & 077) == 0);
    char extra[4096]; CHECK((size_t)snprintf(extra, sizeof(extra), "%s/linked", argv[1]) < sizeof(extra));
    unsigned skipped_hardlink = 0;
    if (link(path, extra) < 0) {
        CHECK(argc == 3 && (errno == EACCES || errno == EPERM));
        skipped_hardlink = 1;
        puts("SKIP: host denied creation of the hardlink rejection fixture");
    } else {
        CHECK(mldr_executable_boothash_for_boot(race, boot, identity, c) == -EPERM);
        CHECK(unlink(extra) == 0);
    }
    CHECK(unlink(path) == 0 && rmdir(race) == 0);
    printf("EXECUTABLE_BOOTHASH_OK checks=%u skipped_hardlink=%u\n", checks, skipped_hardlink);
}
