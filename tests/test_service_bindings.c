#define _GNU_SOURCE
#include "ios27/service_bindings.h"
#include "ios27/process.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static uint8_t *load(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    assert(file && !fseek(file, 0, SEEK_END));
    length = ftell(file);
    assert(length >= 32 && length <= 2 * 1024 * 1024);
    assert(!fseek(file, 0, SEEK_SET));
    bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(!fclose(file));
    *size = (size_t)length;
    return bytes;
}

static size_t section_record(const uint8_t *bytes, size_t *segment) {
    static const char name[16] = "__ios27_api";
    size_t cursor = 32u;
    for (uint32_t i = 0; i < read32(bytes + 16u); ++i) {
        if (read32(bytes + cursor) == 0x19u) {
            for (uint32_t j = 0; j < read32(bytes + cursor + 64u); ++j) {
                size_t section = cursor + 72u + (size_t)j * 80u;
                if (!memcmp(bytes + section, name, sizeof(name))) {
                    *segment = cursor;
                    return section;
                }
            }
        }
        cursor += read32(bytes + cursor + 4u);
    }
    assert(!"compiler-produced service binding section missing");
    return 0;
}

static void rejected(const uint8_t *bytes, size_t size) {
    ios27_service_binding_plan plan;
    char error[160];
    memset(&plan, 0xff, sizeof(plan));
    assert(ios27_service_binding_inspect(bytes, size, &plan, error, sizeof(error)) == -1);
    assert(!plan.present && !plan.vmaddr && !plan.file_offset && error[0]);
}

int main(int argc, char **argv) {
    ios27_service_binding_plan plan;
    size_t size, segment, section, offset, old_size;
    char error[160];
    uint8_t *bytes, *copy, *old;
    if (argc == 2 && !strcmp(argv[1], "--no-linker")) {
        puts("service binding metadata fixture requires LLVM ld64.lld");
        return 77;
    }
    assert(argc == 4);
    bytes = load(argv[1], &size);
    copy = malloc(size);
    assert(copy);
    assert(!ios27_service_binding_inspect(bytes, size, &plan, error, sizeof(error)));
    assert(plan.present && plan.file_offset <= size - IOS27_SERVICE_BINDING_BYTES);
    offset = (size_t)plan.file_offset;
    section = section_record(bytes, &segment);
    memcpy(copy, bytes, size);
    assert(!ios27_service_binding_inspect(bytes, size, &plan, NULL, 0));
    assert(!memcmp(copy, bytes, size));

    /* Correct compiler metadata with an unsupported header must fail before
     * a pointer can be written or any guest code can become executable. */
    put32(copy + offset, 2u);
    rejected(copy, size);
#if defined(__aarch64__)
    {
        ios27_macho_info info;
        ios27_process_options options = {1000u};
        ios27_startup_context startup = {0, NULL, NULL, NULL};
        ios27_process_result result;
        int status;
        assert(!ios27_macho_parse(copy, size, &info, error, sizeof(error)));
        memset(&result, 0xff, sizeof(result));
        assert(ios27_run_executable_exec(argv[3], copy, size, &info, &startup,
            &options, &result, error, sizeof(error)) == -1);
        assert(!result.outcome && !result.exit_code && !result.signal && error[0]);
        assert(!ios27_run_executable_exec(argv[3], bytes, size, &info, &startup,
            &options, &result, error, sizeof(error)));
        assert(result.outcome == IOS27_PROCESS_RETURNED && result.exit_code == 42);
        errno = 0;
        assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    }
#endif
    memcpy(copy, bytes, size);
    put32(copy + offset + 4u, 8u);
    rejected(copy, size);
    memcpy(copy, bytes, size);
    copy[offset + 8u] = 1u;
    rejected(copy, size);
    memcpy(copy, bytes, size);
    memset(copy + offset + 8u, 0xff, 8u);
    rejected(copy, size);
    memcpy(copy, bytes, size);
    put32(copy + section + 40u, 8u);
    rejected(copy, size);
    memcpy(copy, bytes, size);
    put32(copy + section + 52u, 2u);
    rejected(copy, size);
    memcpy(copy, bytes, size);
    put32(copy + section + 64u, UINT32_C(0x80000000));
    rejected(copy, size);
    memcpy(copy, bytes, size);
    put32(copy + segment + 56u, 7u);
    rejected(copy, size);
    rejected(bytes, 0);
    rejected(bytes, 31u);
    assert(ios27_service_binding_inspect(bytes, size, NULL, error, sizeof(error)) == -1);
    old = load(argv[2], &old_size);
    assert(!ios27_service_binding_inspect(old, old_size, &plan, error, sizeof(error)));
    assert(!plan.present && !plan.vmaddr && !plan.file_offset);
    free(old);
    free(copy);
    free(bytes);
    puts("service binding: real linked record, read-only inspection, unsupported/prebound slots, bad geometry and absent binding passed");
    return 0;
}
