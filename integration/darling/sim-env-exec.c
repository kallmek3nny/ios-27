/* Load a bounded simulator environment after this host-side Mach-O has
 * initialized, then exec the simulator program with its own dyld root. */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int valid_key(const char *key) {
    if (!key[0] || !(isalpha((unsigned char)key[0]) || key[0] == '_')) return 0;
    for (const char *p = key + 1; *p; ++p)
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
    return strcmp(key, "LD_PRELOAD") && strcmp(key, "LD_LIBRARY_PATH") && strncmp(key, "__mldr_", 7);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fputs("usage: sim-env-exec <environment-file> <program> [args...]\n", stderr);
        return 2;
    }
    FILE *input = fopen(argv[1], "r");
    if (!input) { perror("open simulator environment"); return 3; }
    struct stat info;
    if (fstat(fileno(input), &info) || !S_ISREG(info.st_mode) || info.st_size < 0 || info.st_size > 65536) {
        fputs("invalid simulator environment file\n", stderr);
        fclose(input);
        return 3;
    }
    char *contents = malloc(65537);
    if (!contents) { fclose(input); return 3; }
    size_t size = fread(contents, 1, 65537, input);
    int failed = ferror(input);
    fclose(input);
    if (failed || size > 65536 || memchr(contents, 0, size)) { free(contents); return 3; }
    contents[size] = 0;
    unsigned count = 0;
    char *line = contents;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        else next = line + strlen(line);
        size_t length = strlen(line);
        if (++count > 256 || length >= 8192) {
            fputs("simulator environment limit exceeded\n", stderr);
            free(contents);
            return 3;
        }
        if (length && line[length - 1] == '\r') line[--length] = 0;
        if (!length) { line = next; continue; }
        char *value = strchr(line, '=');
        if (!value) { free(contents); return 3; }
        *value++ = 0;
        if (!valid_key(line)) { free(contents); return 3; }
        if (setenv(line, value, 1)) { perror("set simulator environment"); free(contents); return 4; }
        line = next;
    }
    free(contents);
    execv(argv[2], argv + 2);
    perror("exec simulator program");
    return 127;
}
