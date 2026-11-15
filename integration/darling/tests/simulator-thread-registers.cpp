/* Exercise the actual dyld_sim mapping adapter on host-owned RX pages.
 * Mach-O declarations below follow the public loader.h byte layout. ARM64
 * words are test data; no Apple or translated guest instructions execute. */
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "tsd_rewrite.h"

struct macho_segment_command {
    uint32_t cmd, cmdsize;
    char segname[16];
    uint64_t vmaddr, vmsize, fileoff, filesize;
    int32_t maxprot, initprot;
    uint32_t nsects, flags;
};
struct section_64 {
    char sectname[16], segname[16];
    uint64_t addr, size;
    uint32_t offset, align, reloff, nreloc, flags, reserved1, reserved2, reserved3;
};
struct Metadata { macho_segment_command segment; section_64 sections[2]; };
static_assert(sizeof(macho_segment_command) == 72 && sizeof(section_64) == 80);
static_assert(offsetof(Metadata, sections) == 72);
static constexpr int VM_PROT_EXECUTE = PROT_EXEC;
static constexpr uint32_t S_ATTR_PURE_INSTRUCTIONS = 0x80000000u;
static constexpr uint32_t S_ATTR_SOME_INSTRUCTIONS = 0x00000400u;

static size_t vm_page_size;
static uintptr_t tracked, slot = 64;
static size_t tracked_size;
static unsigned checks, protections, invalidations, fail_protection;
static int observed_protection[8];
static bool tracking;
static uintptr_t invalidated_start;
static size_t invalidated_size;

#define CHECK(c) do { ++checks; if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

extern "C" int mprotect(void* address, size_t size, int protection) noexcept {
    if (tracking && (uintptr_t)address >= tracked && (uintptr_t)address - tracked < tracked_size) {
        CHECK(size <= tracked_size - ((uintptr_t)address - tracked));
        CHECK((protection & (PROT_WRITE | PROT_EXEC)) != (PROT_WRITE | PROT_EXEC));
        CHECK(protections < sizeof(observed_protection) / sizeof(observed_protection[0]));
        observed_protection[protections++] = protection;
        if (fail_protection == protections) { errno = EACCES; return -1; }
    }
    return (int)syscall(SYS_mprotect, address, size, protection);
}
extern "C" unsigned long sys_thread_get_native_tsd_slot_offset(void) { return slot; }
static void sys_icache_invalidate(void* address, size_t size) {
    CHECK((uintptr_t)address >= tracked && size <= tracked_size - ((uintptr_t)address - tracked));
    CHECK(protections > 0 && observed_protection[protections - 1] == (PROT_READ | PROT_WRITE));
    ++invalidations; invalidated_start = (uintptr_t)address; invalidated_size = size;
    __builtin___clear_cache((char*)address, (char*)address + size);
}

#define DARLING 1
#ifndef __arm64__
#define __arm64__ 1
#endif
static const char* adapt(macho_segment_command* seg, uintptr_t requestedLoadAddress) {
#include SIMULATOR_TSD_BLOCK
    return nullptr;
}

static bool is_rx(void* address) {
    FILE* maps = std::fopen("/proc/self/maps", "r");
    CHECK(maps != nullptr);
    char line[1024], permission[5];
    unsigned long start, end;
    bool found = false;
    while (std::fgets(line, sizeof(line), maps)) {
        if (std::sscanf(line, "%lx-%lx %4s", &start, &end, permission) == 3 &&
                (uintptr_t)address >= start && (uintptr_t)address < end) {
            found = permission[0] == 'r' && permission[1] == '-' && permission[2] == 'x';
            break;
        }
    }
    std::fclose(maps);
    return found;
}

static Metadata reset() {
    tracking = false;
    CHECK(mprotect((void*)tracked, tracked_size, PROT_READ | PROT_WRITE) == 0);
    std::memset((void*)tracked, 0xa5, tracked_size);
    uint32_t* instructions = (uint32_t*)(tracked + vm_page_size - 8);
    const uint32_t pairs[] = {0xd53bd063u, 0x927df063u, 0xd53bd068u, 0x927db108u};
    std::memcpy(instructions, pairs, sizeof(pairs));
    std::memcpy((void*)(tracked + 128), pairs, sizeof(pairs)); // data, outside the code section
    CHECK(mprotect((void*)tracked, tracked_size, PROT_READ | PROT_EXEC) == 0);
    protections = invalidations = fail_protection = 0;
    slot = 64; tracking = true;
    Metadata metadata{};
    auto& seg = metadata.segment;
    seg.cmd = 0x19; seg.cmdsize = 72 + 80; seg.vmaddr = 0x100000000ull;
    seg.vmsize = seg.filesize = tracked_size; seg.initprot = seg.maxprot = PROT_READ | PROT_EXEC; seg.nsects = 1;
    metadata.sections[0].addr = seg.vmaddr + vm_page_size - 8;
    metadata.sections[0].size = 16;
    metadata.sections[0].flags = S_ATTR_PURE_INSTRUCTIONS;
    return metadata;
}

int main() {
    vm_page_size = (size_t)sysconf(_SC_PAGESIZE);
    CHECK(vm_page_size >= 4096 && !(vm_page_size & (vm_page_size - 1)));
    tracked_size = 2 * vm_page_size;
    void* memory = mmap(nullptr, tracked_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(memory != MAP_FAILED);
    tracked = (uintptr_t)memory;
    Metadata metadata = reset();
    CHECK(adapt(&metadata.segment, tracked) == nullptr);
    CHECK(protections == 2 && observed_protection[0] == (PROT_READ | PROT_WRITE) &&
          observed_protection[1] == (PROT_READ | PROT_EXEC));
    CHECK(invalidations == 1 && invalidated_start == tracked + vm_page_size - 8 && invalidated_size == 16);
    const uint32_t expected[] = {0xd53bd043u, 0xf9402063u, 0xd53bd048u, 0xf9402108u};
    CHECK(std::memcmp((void*)(tracked + vm_page_size - 8), expected, sizeof(expected)) == 0);
    const uint32_t original[] = {0xd53bd063u, 0x927df063u, 0xd53bd068u, 0x927db108u};
    CHECK(std::memcmp((void*)(tracked + 128), original, sizeof(original)) == 0);
    CHECK(is_rx(memory) && is_rx((char*)memory + vm_page_size));
    protections = invalidations = 0;
    CHECK(adapt(&metadata.segment, tracked) == nullptr && protections == 0 && invalidations == 0);

    for (unsigned mutation = 0; mutation < 12; ++mutation) {
        metadata = reset();
        auto& seg = metadata.segment;
        auto& section = metadata.sections[0];
        switch (mutation) {
            case 0: seg.cmdsize = 71; break;
            case 1: seg.cmdsize = 72; break;
            case 2: seg.nsects = UINT32_MAX; break;
            case 3: section.addr = seg.vmaddr - 4; break;
            case 4: section.addr = seg.vmaddr + seg.filesize + 4; break;
            case 5: section.size = UINT64_MAX; break;
            case 6: ++section.addr; break;
            case 7: section.size = 3; break;
            case 8: section.addr = seg.vmaddr + seg.filesize; section.size = 4; break;
            case 9: section.size = seg.filesize; break;
            case 10: slot = 15; break;
            case 11: slot = 32768; break;
        }
        CHECK(adapt(&seg, tracked) != nullptr && protections == 0 && invalidations == 0);
        CHECK(std::memcmp((void*)(tracked + vm_page_size - 8), original, sizeof(original)) == 0);
        CHECK(is_rx(memory));
    }
    metadata = reset(); metadata.sections[0].flags = 0;
    CHECK(adapt(&metadata.segment, tracked) == nullptr && protections == 0);
    metadata = reset(); metadata.segment.initprot = PROT_READ;
    CHECK(adapt(&metadata.segment, tracked) == nullptr && protections == 0);
    metadata = reset(); metadata.sections[0].size = 0; metadata.sections[0].addr = UINT64_MAX;
    CHECK(adapt(&metadata.segment, tracked) == nullptr && protections == 0);

    metadata = reset(); fail_protection = 1;
    CHECK(adapt(&metadata.segment, tracked) != nullptr && invalidations == 0);
    CHECK(is_rx(memory) && std::memcmp((void*)(tracked + vm_page_size - 8), original, sizeof(original)) == 0);
    metadata = reset(); fail_protection = 2;
    CHECK(adapt(&metadata.segment, tracked) != nullptr && invalidations == 1);

    metadata = reset();
    metadata.segment.cmdsize += 80; metadata.segment.nsects = 2;
    metadata.sections[1] = metadata.sections[0];
    metadata.sections[0].size = metadata.sections[1].size = 8;
    metadata.sections[1].addr += 8;
    CHECK(adapt(&metadata.segment, tracked) == nullptr && protections == 4 && invalidations == 2);
    CHECK(is_rx(memory) && is_rx((char*)memory + vm_page_size));
    tracking = false;
    CHECK(munmap(memory, tracked_size) == 0);
    std::printf("SIMULATOR_THREAD_REGISTERS_OK checks=%u\n", checks);
}
