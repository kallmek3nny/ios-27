/* Actual cached-image adapter on owned, file-backed RX pages. ARM64 words
 * are data; the host executes only this original harness and the adapter. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

struct mach_header_64 { uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved; };
struct load_command { uint32_t cmd, cmdsize; };
struct segment_command_64 {
    uint32_t cmd, cmdsize; char segname[16];
    uint64_t vmaddr, vmsize, fileoff, filesize;
    int32_t maxprot, initprot; uint32_t nsects, flags;
};
struct section_64 {
    char sectname[16], segname[16]; uint64_t addr, size;
    uint32_t offset, align, reloff, nreloc, flags, reserved1, reserved2, reserved3;
};
struct Metadata { mach_header_64 header; segment_command_64 segment; section_64 sections[2]; };
static_assert(sizeof(mach_header_64) == 32 && sizeof(segment_command_64) == 72 && sizeof(section_64) == 80);
#define MH_MAGIC_64 0xfeedfacfu
#define MH_DYLIB 6
#define MH_DYLIB_IN_CACHE 0x80000000u
#define CPU_TYPE_ARM64 0x100000c
#define LC_SEGMENT_64 0x19
#define S_ATTR_PURE_INSTRUCTIONS 0x80000000u
#define S_ATTR_SOME_INSTRUCTIONS 0x400u

static uintptr_t tracked, scratch, last_target;
static size_t page, bytes, scratch_bytes, last_bytes, rewritten;
static size_t allowed_first, allowed_last;
static unsigned checks, mappings, protections, invalidations, released, fail_mapping, fail_protection;
static bool tracking, backed[4];
static int fixture;
static const uint32_t original[] = {0xd53bd063u, 0x927df063u, 0xd53bd068u, 0x927db108u};
static const uint32_t expected[] = {0xd53bd043u, 0xf9402063u, 0xd53bd048u, 0xf9402108u};
#define CHECK(c) do { ++checks; if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

extern "C" void* mmap(void* address, size_t size, int prot, int flags, int fd, off_t offset) noexcept {
    if (tracking) {
        CHECK(prot == (PROT_READ | PROT_WRITE) && fd == -1 && offset == 0);
        CHECK((flags & (MAP_PRIVATE | MAP_ANONYMOUS)) == (MAP_PRIVATE | MAP_ANONYMOUS));
        ++mappings;
        if (fail_mapping == mappings) { errno = ENOMEM; return MAP_FAILED; }
        if (flags & MAP_FIXED) {
            CHECK((uintptr_t)address >= tracked + allowed_first && (uintptr_t)address + size <= tracked + allowed_last);
            CHECK(!((uintptr_t)address & (page - 1)) && size <= 2 * page);
        } else CHECK(address == nullptr && scratch == 0 && size <= 2 * page);
    }
    void* result = (void*)syscall(SYS_mmap, address, size, prot, flags, fd, offset);
    if (tracking && result != MAP_FAILED) {
        if (flags & MAP_FIXED) {
            CHECK(result == address);
            last_target = (uintptr_t)result; last_bytes = size;
            for (size_t p = (last_target - tracked) / page; p < (last_target - tracked + size) / page; ++p) backed[p] = false;
        } else { scratch = (uintptr_t)result; scratch_bytes = size; }
    }
    return result;
}
extern "C" int munmap(void* address, size_t size) noexcept {
    if (tracking && (uintptr_t)address == scratch) {
        CHECK(size == scratch_bytes); scratch = 0; ++released;
    }
    return (int)syscall(SYS_munmap, address, size);
}
extern "C" int mprotect(void* address, size_t size, int prot) noexcept {
    if (tracking) {
        CHECK((uintptr_t)address == last_target && size == last_bytes);
        CHECK(prot == (PROT_READ | PROT_EXEC));
        for (size_t p = (last_target - tracked) / page; p < (last_target - tracked + size) / page; ++p) CHECK(!backed[p]);
        ++protections;
        if (protections == fail_protection) { errno = EACCES; return -1; }
    }
    return (int)syscall(SYS_mprotect, address, size, prot);
}
static void sys_icache_invalidate(void* address, size_t size) {
    CHECK((uintptr_t)address == last_target && size == last_bytes);
    ++invalidations;
    __builtin___clear_cache((char*)address, (char*)address + size);
}
#include SIMULATOR_CACHE_TSD_HEADER

static bool is_rx(uintptr_t address) {
    FILE* maps = std::fopen("/proc/self/maps", "r");
    CHECK(maps != nullptr);
    char line[1024], protection[5]; unsigned long start, end; bool result = false;
    while (std::fgets(line, sizeof(line), maps)) {
        if (std::sscanf(line, "%lx-%lx %4s", &start, &end, protection) == 3 && address >= start && address < end) {
            result = std::strcmp(protection, "r-xp") == 0; break;
        }
    }
    std::fclose(maps); return result;
}

static Metadata* reset(bool shared_page = false) {
    CHECK(!scratch);
    CHECK(mmap((void*)tracked, bytes, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == (void*)tracked);
    std::memset((void*)tracked, 0, bytes);
    auto* metadata = (Metadata*)tracked;
    metadata->header = {MH_MAGIC_64, CPU_TYPE_ARM64, 0, MH_DYLIB, 1, 72 + 2 * 80, MH_DYLIB_IN_CACHE, 0};
    auto& segment = metadata->segment;
    segment.cmd = LC_SEGMENT_64; segment.cmdsize = 72 + 2 * 80;
    std::memcpy(segment.segname, "__TEXT", 6);
    segment.vmaddr = 0x100000000ull; segment.vmsize = segment.filesize = bytes;
    segment.initprot = segment.maxprot = PROT_READ | PROT_EXEC; segment.nsects = 2;
    auto& code = metadata->sections[0];
    code.addr = segment.vmaddr + (shared_page ? page + 128 : 2 * page - 4);
    code.size = sizeof(original); code.flags = S_ATTR_PURE_INSTRUCTIONS;
    metadata->sections[1].addr = segment.vmaddr + 3 * page + 128;
    metadata->sections[1].size = sizeof(original);
    std::memcpy((void*)(tracked + code.addr - segment.vmaddr), original, sizeof(original));
    std::memcpy((void*)(tracked + 3 * page + 128), original, sizeof(original));
    if (shared_page) {
        metadata->sections[1] = code;
        metadata->sections[0].size = metadata->sections[1].size = 8;
        metadata->sections[1].addr += 8;
    }
    CHECK(pwrite(fixture, (void*)tracked, bytes, 0) == (ssize_t)bytes);
    CHECK(mmap((void*)tracked, bytes, PROT_READ | PROT_EXEC, MAP_FIXED | MAP_PRIVATE, fixture, 0) == (void*)tracked);
    mappings = protections = invalidations = released = fail_mapping = fail_protection = 0;
    last_target = last_bytes = 0;
    allowed_first = page; allowed_last = 3 * page;
    for (bool& file : backed) file = true;
    return (Metadata*)tracked;
}

static const char* adapt(uintptr_t slot = 64) {
    tracking = true;
    const char* error = darling_simulator_cache_tsd((mach_header_64*)tracked, page, slot, &rewritten);
    tracking = false;
    CHECK(!scratch);
    return error;
}

int main() {
    page = (size_t)sysconf(_SC_PAGESIZE); bytes = 4 * page;
    fixture = (int)syscall(SYS_memfd_create, "ios27-cache-tsd", MFD_CLOEXEC);
    CHECK(fixture >= 0 && ftruncate(fixture, (off_t)bytes) == 0);
    void* memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(memory != MAP_FAILED); tracked = (uintptr_t)memory;
    reset();
    CHECK(adapt() == nullptr && rewritten == 2);
    CHECK(mappings == 4 && protections == 2 && invalidations == 2 && released == 2);
    CHECK(backed[0] && !backed[1] && !backed[2] && backed[3]);
    CHECK(std::memcmp((void*)(tracked + 2 * page - 4), expected, sizeof(expected)) == 0);
    CHECK(std::memcmp((void*)(tracked + 3 * page + 128), original, sizeof(original)) == 0);
    uint32_t file_words[4];
    CHECK(pread(fixture, file_words, sizeof(file_words), (off_t)(2 * page - 4)) == sizeof(file_words));
    CHECK(std::memcmp(file_words, original, sizeof(original)) == 0);
    CHECK(is_rx(tracked + page) && is_rx(tracked + 2 * page));
    mappings = protections = invalidations = 0;
    CHECK(adapt() == nullptr && rewritten == 0 && mappings == 0 && protections == 0 && invalidations == 0);

    for (unsigned kind = 0; kind < 3; ++kind) {
        auto* metadata = reset();
        CHECK(mprotect((void*)tracked, page, PROT_READ | PROT_WRITE) == 0);
        if (kind == 0) metadata->header.flags = 0;
        if (kind == 1) metadata->sections[0].flags = 0;
        if (kind == 2) { metadata->sections[0].size = 0; metadata->sections[0].addr = UINT64_MAX; }
        CHECK(mprotect((void*)tracked, page, PROT_READ | PROT_EXEC) == 0);
        CHECK(adapt(kind == 0 ? 0 : 64) == nullptr && rewritten == 0 && mappings == 0);
        CHECK(std::memcmp((void*)(tracked + 2 * page - 4), original, sizeof(original)) == 0);
    }
    for (unsigned mutation = 0; mutation < 22; ++mutation) {
        auto* m = reset();
        CHECK(mprotect((void*)tracked, page, PROT_READ | PROT_WRITE) == 0);
        switch (mutation) {
            case 0: m->header.magic = 0; break;
            case 1: m->header.cputype = 7; break;
            case 2: m->header.filetype = 2; break;
            case 3: m->header.ncmds = 0; break;
            case 4: m->header.sizeofcmds = 1024 * 1024 + 1; break;
            case 5: m->header.ncmds = UINT32_MAX; break;
            case 6: m->segment.cmdsize = 7; break;
            case 7: m->segment.cmdsize = 64; break;
            case 8: m->segment.cmdsize = 240; break;
            case 9: m->segment.nsects = UINT32_MAX; break;
            case 10: m->segment.segname[0] = 'X'; break;
            case 11: --m->segment.vmsize; break;
            case 12: ++m->segment.filesize; break;
            case 13: m->segment.vmaddr = UINT64_MAX - 16; break;
            case 14: m->segment.initprot |= PROT_WRITE; break;
            case 15: --m->sections[0].addr; break;
            case 16: m->sections[0].size = UINT64_MAX; break;
            case 17: m->sections[0].size = 3; break;
            case 18: m->header.sizeofcmds += 8; break;
            case 19: m->sections[1].flags = S_ATTR_PURE_INSTRUCTIONS; m->sections[1].size = bytes; break;
            case 20: m->sections[0].addr = m->segment.vmaddr - 4; break;
            case 21: m->sections[0].addr = m->segment.vmaddr + m->segment.filesize; break;
        }
        CHECK(mprotect((void*)tracked, page, PROT_READ | PROT_EXEC) == 0);
        CHECK(adapt() != nullptr && rewritten == 0 && mappings == 0 && protections == 0);
        CHECK(std::memcmp((void*)(tracked + 2 * page - 4), original, sizeof(original)) == 0);
        CHECK(is_rx(tracked + page) && is_rx(tracked + 2 * page));
    }
    reset(); CHECK(adapt(15) != nullptr && mappings == 0);
    for (unsigned failure = 1; failure <= 2; ++failure) {
        reset(); fail_mapping = failure;
        CHECK(adapt() != nullptr && rewritten == 0 && released == failure - 1 && protections == 0);
        CHECK(is_rx(tracked + page) && std::memcmp((void*)(tracked + 2 * page - 4), original, sizeof(original)) == 0);
    }
    reset(); fail_protection = 1;
    CHECK(adapt() != nullptr && rewritten == 0 && released == 1 && invalidations == 1);
    CHECK(!is_rx(tracked + page));
    reset(true);
    CHECK(adapt() == nullptr && rewritten == 2 && mappings == 4);
    CHECK(std::memcmp((void*)(tracked + page + 128), expected, sizeof(expected)) == 0);
    CHECK(backed[0] && !backed[1] && backed[2] && backed[3]);
    // Cache text sizes are often not page multiples. Its final partial page
    // is mapped by the cache, including padding beyond the section's end.
    auto* partial = reset();
    CHECK(mprotect((void*)tracked, bytes, PROT_READ | PROT_WRITE) == 0);
    partial->segment.vmsize = partial->segment.filesize = bytes - 17;
    partial->sections[0].addr = partial->segment.vmaddr + bytes - 64;
    std::memcpy((void*)(tracked + bytes - 64), original, sizeof(original));
    CHECK(pwrite(fixture, (void*)tracked, bytes, 0) == (ssize_t)bytes);
    CHECK(mmap((void*)tracked, bytes, PROT_READ | PROT_EXEC, MAP_FIXED | MAP_PRIVATE, fixture, 0) == (void*)tracked);
    allowed_first = 3 * page; allowed_last = bytes;
    CHECK(adapt() == nullptr && rewritten == 2);
    CHECK(std::memcmp((void*)(tracked + bytes - 64), expected, sizeof(expected)) == 0);
    CHECK(backed[0] && backed[1] && backed[2] && !backed[3] && is_rx(tracked + 3 * page));
    CHECK(munmap((void*)tracked, bytes) == 0 && close(fixture) == 0);
    std::printf("SIMULATOR_CACHE_TSD_OK checks=%u\n", checks);
}
