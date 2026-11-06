#include "ios27/process.h"
#include "ios27/chained.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    FILE *file;
    long length;
    uint8_t *bytes;
    ios27_macho_info info;
    ios27_exec_layout layout;
    ios27_chained_plan rebases;
    ios27_process_result result;
    ios27_process_options options = {2000};
    ios27_startup_context context = {0, NULL, NULL, NULL};
    char error[256] = {0};
    if (argc != 3) return 2;
    file = fopen(argv[1], "rb");
    if (!file) { puts("FAIL: cannot open packaged fixture"); return 2; }
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 32 || length > 2097152 || fseek(file, 0, SEEK_SET)) { fclose(file); return 2; }
    bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) { free(bytes); fclose(file); return 2; }
    fclose(file);
    if (ios27_macho_parse(bytes, (size_t)length, &info, error, sizeof(error)) ||
        ios27_macho_executable_layout(bytes, (size_t)length, &info, (size_t)sysconf(_SC_PAGESIZE), &layout, error, sizeof(error))) {
        printf("FAIL: Mach-O validation: %s\n", error); free(bytes); return 3;
    }
    printf("Mach-O validated; host page size %ld bytes\n", sysconf(_SC_PAGESIZE));
    if (ios27_chained_static_plan(bytes, (size_t)length, &info, &rebases, error, sizeof(error))) {
        printf("FAIL: static rebase plan: %s\n", error); free(bytes); return 3;
    }
    printf("Static internal pointer rebases: %zu\n", rebases.count);
#if !defined(__aarch64__)
    puts("UNSUPPORTED: native guest execution requires ARM64. Metadata validation passed.");
    free(bytes); return 77;
#endif
    if (ios27_run_executable_exec(argv[2], bytes, (size_t)length, &info, &context, &options, &result, error, sizeof(error))) {
        printf("FAIL: supervised loader setup: %s\n", error); free(bytes); return 4;
    }
    free(bytes);
    if (result.outcome != IOS27_PROCESS_RETURNED || result.exit_code != 42) {
        printf("FAIL: outcome=%d return=%d signal=%d\n", result.outcome, result.exit_code, result.signal); return 5;
    }
    puts("PASS: native ARM64 Mach-O returned 42. Named file, mapping and heap checks passed under supervision.");
    return 0;
}
