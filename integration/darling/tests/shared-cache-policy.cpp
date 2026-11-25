/* Original contracts for Darling's real signature-policy path. Only the
 * synthetic fixture is modified; optional real caches are opened read-only. */
#include <darlingserver/code-signing.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

using namespace DarlingServer::CodeSigning;
static unsigned checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } \
} while (0)

int main(int argc, char **argv) {
    CHECK(argc >= 2);
    FILE *source = std::fopen(argv[1], "rb"); CHECK(source != nullptr);
    FILE *copy = std::tmpfile(); CHECK(copy != nullptr);
    uint8_t bytes[8192];
    size_t size = std::fread(bytes, 1, sizeof(bytes), source);
    CHECK(size > 4096 && size < sizeof(bytes) && !std::ferror(source));
    CHECK(std::fclose(source) == 0);
    CHECK(std::fwrite(bytes, 1, size, copy) == size && std::fflush(copy) == 0);
    int fd = fileno(copy);
    auto original = readImage(fd, 0, Checks::Enforce, {});
    CHECK(original.state.status == dserver_cs_status_valid && original.error.empty());
    CHECK(original.state.code_limit == 4096 && original.pageSizeLog2 == 12);
    CHECK(readImage(fd, 1, Checks::Signature, {}).state.status == dserver_cs_status_invalid);

    uint8_t changed = bytes[200] ^ 1;
    CHECK(pwrite(fd, &changed, 1, 200) == 1);
    auto metadata = readImage(fd, 0, Checks::Signature, {});
    CHECK(metadata.state.status == dserver_cs_status_valid && metadata.state.code_limit == 4096);
    auto warning = readImage(fd, 0, Checks::Report, {});
    CHECK(warning.state.status == dserver_cs_status_valid && !warning.error.empty());
    auto enforced = readImage(fd, 0, Checks::Enforce, {});
    CHECK(enforced.state.status == dserver_cs_status_invalid && !enforced.error.empty());
    CHECK(pwrite(fd, bytes + 200, 1, 200) == 1);

    changed = bytes[4096] ^ 1;
    CHECK(pwrite(fd, &changed, 1, 4096) == 1);
    CHECK(readImage(fd, 0, Checks::Enforce, {}).state.status == dserver_cs_status_invalid);
    metadata = readImage(fd, 0, Checks::Signature, {});
    /* Policy off retains its existing tolerance, but a malformed signature
     * must never invent coverage that would pass dyld's length check. */
    CHECK(metadata.state.code_limit == 0 && metadata.pageSizeLog2 == 0);
    CHECK(std::fclose(copy) == 0);

    for (int i = 2; i < argc; ++i) {
        int real = open(argv[i], O_RDONLY | O_CLOEXEC); CHECK(real >= 0);
        auto image = readImage(real, 0, Checks::Signature, {});
        std::printf("cache=%s status=%u coverage=%llu page_log2=%u error=%s\n", argv[i],
            image.state.status, (unsigned long long)image.state.code_limit,
            image.pageSizeLog2, image.error.c_str());
        CHECK(image.state.status == dserver_cs_status_valid && image.error.empty());
        CHECK(image.state.code_limit > 0 && image.pageSizeLog2 >= 12);
        if (image.state.code_limit > maxVerifiedSize) {
            /* The existing verification-size limit is not relaxed for caches. */
            auto bounded = readImage(real, 0, Checks::Enforce, {});
            CHECK(bounded.state.status == dserver_cs_status_invalid && !bounded.error.empty());
        }
        CHECK(close(real) == 0);
    }
    std::printf("SHARED_CACHE_POLICY_OK checks=%u\n", checks);
    return 0;
}
