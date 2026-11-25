#include "ios27/platform_memory.h"
#include "ios27/images.h"
#include "images_internal.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sign_of(int value) { return (value > 0) - (value < 0); }

static void host_checks(void) {
    unsigned char source[256], expected[256], actual[256];
    size_t cases = 0u;
    for (size_t i = 0u; i < sizeof(source); ++i) source[i] = (unsigned char)(i * 79u + 13u);
    for (size_t size = 0u; size <= 128u; ++size) {
        for (size_t from = 0u; from < 8u; ++from) for (size_t to = 0u; to < 8u; ++to) {
            memset(expected, 0xa5, sizeof(expected)); memset(actual, 0xa5, sizeof(actual));
            memcpy(expected + to, source + from, size);
            assert(ios27_platform_memcpy(actual + to, source + from, size) == actual + to);
            assert(!memcmp(actual, expected, sizeof(actual)));
            memcpy(expected, source, sizeof(expected)); memcpy(actual, source, sizeof(actual));
            memmove(expected + to, expected + from, size);
            assert(ios27_platform_memmove(actual + to, actual + from, size) == actual + to);
            assert(!memcmp(actual, expected, sizeof(actual)));
            ++cases;
        }
        const int values[] = {INT_MIN, -1000, -1, 0, 1, 255, 256, INT_MAX};
        for (size_t i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
            memcpy(expected, source, sizeof(expected)); memcpy(actual, source, sizeof(actual));
            memset(expected + 7u, values[i], size);
            assert(ios27_platform_memset(actual + 7u, values[i], size) == actual + 7u);
            assert(!memcmp(actual, expected, sizeof(actual)));
            ios27_platform_bzero(actual + 7u, size); memset(expected + 7u, 0, size);
            assert(!memcmp(actual, expected, sizeof(actual)));
            ++cases;
        }
        memcpy(actual, source, sizeof(actual));
        assert(ios27_platform_memcmp(actual, source, size) == 0);
        actual[size / 2u] ^= 255u;
        assert(sign_of(ios27_platform_memcmp(actual, source, size)) == sign_of(memcmp(actual, source, size)));
    }
    const char *strings[] = {"", "a", "ab", "aba", "ababababac", "bc", "longer needle than text",
                             "\x80", "\xff", "a\x80", "a\xff", "a\x80\xff", "xxx"};
    for (size_t i = 0u; i < sizeof(strings) / sizeof(strings[0]); ++i) {
        const char *a = strings[i];
        assert(ios27_platform_strlen(a) == strlen(a));
        for (size_t limit = 0u; limit <= 64u; ++limit) {
            size_t expected_length = strlen(a) < limit ? strlen(a) : limit;
            assert(ios27_platform_strnlen(a, limit) == expected_length);
            memset(expected, 0xa5, sizeof(expected)); memset(actual, 0xa5, sizeof(actual));
            strncpy((char *)expected + 3u, a, limit);
            assert(ios27_platform_strncpy((char *)actual + 3u, a, limit) == (char *)actual + 3u);
            assert(!memcmp(actual, expected, sizeof(actual)));
            ++cases;
        }
        memset(expected, 0xa5, sizeof(expected)); memset(actual, 0xa5, sizeof(actual));
        strcpy((char *)expected + 3u, a);
        assert(ios27_platform_strcpy((char *)actual + 3u, a) == (char *)actual + 3u);
        assert(!memcmp(actual, expected, sizeof(actual)));
        for (size_t j = 0u; j < sizeof(strings) / sizeof(strings[0]); ++j) {
            const char *b = strings[j];
            assert(sign_of(ios27_platform_strcmp(a, b)) == sign_of(strcmp(a, b)));
            assert(ios27_platform_strstr(a, b) == strstr(a, b));
            for (size_t limit = 0u; limit < 40u; ++limit) {
                assert(sign_of(ios27_platform_strncmp(a, b, limit)) == sign_of(strncmp(a, b, limit)));
                ++cases;
            }
        }
    }
    /* Exactly sized objects make accidental reads past an explicit length or
     * terminator visible to ASan, even when their returned value looks right. */
    for (size_t size = 0u; size < 65u; ++size) {
        char *a = malloc(size ? size : 1u), *b = malloc(size ? size : 1u);
        char *terminated = malloc(size + 1u), *copied = malloc(size + 1u);
        assert(a && b && terminated && copied);
        memset(a, 'x', size); memset(b, 'x', size); memset(terminated, 'x', size);
        terminated[size] = '\0';
        assert(ios27_platform_strnlen(a, size) == size);
        assert(ios27_platform_strncmp(a, b, size) == 0);
        assert(ios27_platform_memcmp(a, b, size) == 0);
        assert(ios27_platform_memcpy(b, a, size) == b);
        assert(ios27_platform_strlen(terminated) == size);
        assert(ios27_platform_strcpy(copied, terminated) == copied);
        assert(ios27_platform_strcmp(copied, terminated) == 0);
        assert(ios27_platform_strstr(terminated, "xy") == NULL);
        free(a); free(b); free(terminated); free(copied);
        ++cases;
    }
    printf("platform memory/string contracts: %zu differential cases passed\n", cases);
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    assert(file && !fseek(file, 0, SEEK_END));
    length = ftell(file);
    assert(length >= 32 && length <= 1024 * 1024 && !fseek(file, 0, SEEK_SET));
    *size = (size_t)length;
    bytes = malloc(*size);
    assert(bytes && fread(bytes, 1u, *size, file) == *size && !fclose(file));
    return bytes;
}

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}
static void write32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static void stub_metadata_checks(const uint8_t *client, size_t size) {
    size_t cursor = 32u, stubs = 0u, entry = 0u;
    uint8_t *copy = malloc(size);
    ios27_initializer_plan plan;
    char error[160];
    assert(copy);
    for (uint32_t i = 0u; i < read32(client + 16u); ++i) {
        uint32_t command = read32(client + cursor);
        if (command == 0x19u) {
            for (uint32_t j = 0u; j < read32(client + cursor + 64u); ++j) {
                size_t section = cursor + 72u + 80u * j;
                if (!memcmp(client + section, "__stubs", 8u)) stubs = section;
            }
        }
        if (command == UINT32_C(0x80000028)) entry = cursor;
        cursor += read32(client + cursor + 4u);
    }
    assert(stubs && entry && read32(client + stubs + 72u) == 12u);
    assert(!ios27_initializers_inspect(client, size, &plan, error, sizeof(error)));
    memcpy(copy, client, size);
    write32(copy + stubs + 72u, 8u);
    assert(ios27_initializers_inspect(copy, size, &plan, error, sizeof(error)) == -1);
    assert(strstr(error, "stub size") && !plan.count && !plan.entry_vmaddr);
    memcpy(copy, client, size);
    write32(copy + entry + 8u, read32(copy + stubs + 48u));
    write32(copy + entry + 12u, 0u);
    assert(ios27_initializers_inspect(copy, size, &plan, error, sizeof(error)) == -1);
    assert(strstr(error, "LC_MAIN") && !plan.count && !plan.entry_vmaddr);
    free(copy);
}

#if defined(__SANITIZE_ADDRESS__)
#define TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TEST_ASAN 1
#endif
#endif

static int linked_checks(const char *library_path, const char *client_path, int native) {
    size_t library_size, client_size;
    uint8_t *library = read_file(library_path, &library_size);
    uint8_t *client = read_file(client_path, &client_size);
    stub_metadata_checks(client, client_size);
    const char *dependencies[] = {"/ios27/libplatform-memory.dylib"};
    const char *paths[] = {"/fixture/Main", dependencies[0]};
    ios27_image_spec specs[] = {{"main", client, client_size, dependencies, 1u},
                                {"platform", library, library_size, NULL, 0u}};
    ios27_images *images = NULL;
    ios27_image_startup plan;
    char error[192] = {0};
    int status = ios27_images_load_with_paths(specs, 2u, paths, paths[0], &images, error, sizeof(error));
    if (status) fprintf(stderr, "platform graph: %s\n", error);
    assert(!status && images);
    status = ios27_images_prepare_startup(images, "main", &plan, error, sizeof(error));
    if (status) fprintf(stderr, "platform startup metadata: %s\n", error);
    assert(!status);
    assert(plan.count == 0u && plan.entry && plan.main_header);
    const char *names[] = {"_bzero", "_memcmp", "_memcpy", "_memmove", "_memset", "_strcmp", "_strcpy",
                           "_strlen", "_strncmp", "_strncpy", "_strnlen", "_strstr"};
    for (size_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        uintptr_t address;
        assert(!ios27_images_symbol(images, "platform", names[i], &address) && address);
        assert(memcmp((const void *)address, "\0\0\0\0", 4u));
    }
    int result = 0;
    if (native) {
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__)) && !defined(TEST_ASAN)
        ios27_startup_context context = {0, NULL, NULL, NULL};
        ios27_process_options options = {1000u};
        ios27_process_result completed;
        for (size_t i = 0u; i < 3u; ++i) {
            status = ios27_images_run_contained(images, "main", &context, &options, &completed, error, sizeof(error));
            if (status) fprintf(stderr, "platform native startup: %s\n", error);
            assert(!status && completed.outcome == IOS27_PROCESS_RETURNED && completed.exit_code == 42);
        }
        puts("native ARM64 platform replacement: client called all twelve linked routines and returned 42");
#else
        puts("native platform replacement skipped: requires ARM64 without ASan/filter conflict");
        result = 77;
#endif
    } else puts("original compiler-linked platform replacement: twelve code exports and startup plan passed");
    ios27_images_unload(images);
    free(library); free(client);
    return result;
}

int main(int argc, char **argv) {
    if (argc == 1) { host_checks(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--no-linker")) return 77;
    assert(argc == 3 || (argc == 4 && !strcmp(argv[3], "--native")));
    return linked_checks(argv[1], argv[2], argc == 4);
}
