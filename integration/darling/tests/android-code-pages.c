/* Original native ARM64 probe for Android file-backed versus anonymous code
 * transitions. Creates one private fixture and executes only its own code. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    const uint32_t code[] = {0x52800540u, 0xd65f03c0u}; // mov w0,#42; ret
    int fd = open(argv[1], O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0 || ftruncate(fd, (off_t)page) || pwrite(fd, code, sizeof(code), 0) != sizeof(code)) return 3;
    void* file = mmap(NULL, page, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0);
    if (file == MAP_FAILED) { perror("file mmap RX"); close(fd); unlink(argv[1]); return 4; }
    int write_result = mprotect(file, page, PROT_READ | PROT_WRITE);
    int write_error = write_result ? errno : 0;
    int file_exec_result = -1, file_exec_error = 0;
    if (write_result == 0) {
        ((uint32_t*)file)[0] = 0x52800560u; // mov w0,#43
        __builtin___clear_cache(file, (char*)file + sizeof(code));
        file_exec_result = mprotect(file, page, PROT_READ | PROT_EXEC);
        file_exec_error = file_exec_result ? errno : 0;
    }
    void* anonymous = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (anonymous == MAP_FAILED) { perror("anonymous mmap"); return 5; }
    memcpy(anonymous, code, sizeof(code));
    __builtin___clear_cache(anonymous, (char*)anonymous + sizeof(code));
    int anonymous_exec = mprotect(anonymous, page, PROT_READ | PROT_EXEC);
    int anonymous_error = anonymous_exec ? errno : 0;
    int anonymous_value = anonymous_exec == 0 ? ((int (*)(void))anonymous)() : -1;

    void* replacement = mmap(file, page, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int replaced_exec = -1, replaced_error = 0, replaced_value = -1;
    if (replacement == file) {
        memcpy(replacement, code, sizeof(code));
        ((uint32_t*)replacement)[0] = 0x52800560u;
        __builtin___clear_cache(replacement, (char*)replacement + sizeof(code));
        replaced_exec = mprotect(replacement, page, PROT_READ | PROT_EXEC);
        replaced_error = replaced_exec ? errno : 0;
        if (replaced_exec == 0) replaced_value = ((int (*)(void))replacement)();
    }
    uint32_t original[2];
    int intact = pread(fd, original, sizeof(original), 0) == sizeof(original) && memcmp(original, code, sizeof(code)) == 0;
    printf("{\"file_rw\":%d,\"file_rw_errno\":%d,\"file_rx_after_write\":%d,\"file_rx_errno\":%d,"
           "\"anonymous_rx\":%d,\"anonymous_errno\":%d,\"anonymous_return\":%d,"
           "\"replacement_rx\":%d,\"replacement_errno\":%d,\"replacement_return\":%d,\"file_intact\":%s}\n",
           write_result, write_error, file_exec_result, file_exec_error, anonymous_exec, anonymous_error, anonymous_value,
           replaced_exec, replaced_error, replaced_value, intact ? "true" : "false");
    munmap(file, page); munmap(anonymous, page); close(fd);
    int removed = unlink(argv[1]) == 0;
    return anonymous_value == 42 && replaced_value == 43 && intact && removed ? 0 : 6;
}
