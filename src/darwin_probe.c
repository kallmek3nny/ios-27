#include "ios27/darwin_probe.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define IOS27_MAX_STEPS 4096u
#define IOS27_SVC_80 0xd4001001u
#define IOS27_RET_X30 0xd65f03c0u
#define IOS27_NOP 0xd503201fu

static int fail(char *error, size_t cap, const char *reason) {
    if (error != NULL && cap > 0u)
        (void)snprintf(error, cap, "%s", reason);
    return -1;
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void ios27_darwin_dispatch(uint64_t x16, uint64_t x0,
                           uint64_t *new_x0, unsigned *carry,
                           unsigned *exited) {
    uint64_t class_id = x16 & 0xff000000ull;
    uint64_t number = x16 & 0x00ffffffull;
    if (!new_x0 || !carry || !exited)
        return;
    *new_x0 = (uint64_t)ENOSYS;
    *carry = 1u;
    *exited = 0u;
    if (class_id != 0u && class_id != IOS27_SYSCALL_CLASS_BSD)
        return;
    switch (number) {
        case 1u: /* Virtual guest exit, never exits the host process. */
            *new_x0 = x0;
            *carry = 0u;
            *exited = 1u;
            break;
        case 20u:
            *new_x0 = (uint64_t)getpid();
            *carry = 0u;
            break;
        case 24u:
            *new_x0 = (uint64_t)getuid();
            *carry = 0u;
            break;
        case 25u:
            *new_x0 = (uint64_t)geteuid();
            *carry = 0u;
            break;
        case 39u:
            *new_x0 = (uint64_t)getppid();
            *carry = 0u;
            break;
        case 43u:
            *new_x0 = (uint64_t)getegid();
            *carry = 0u;
            break;
        case 47u:
            *new_x0 = (uint64_t)getgid();
            *carry = 0u;
            break;
        default:
            break;
    }
}

/* Read one instruction only from a file-backed executable segment. */
static int executable_instruction(const ios27_macho_info *info,
                                  uint64_t offset, size_t size) {
    for (size_t i = 0; i < info->segment_count; ++i) {
        const ios27_segment *s = &info->segments[i];
        if ((s->initprot & 4u) == 0u || s->fileoff > (uint64_t)size ||
            s->filesize > (uint64_t)size - s->fileoff)
            continue;
        if (offset >= s->fileoff && offset - s->fileoff <= s->filesize &&
            s->filesize - (offset - s->fileoff) >= 4u &&
            offset <= (uint64_t)size && (uint64_t)size - offset >= 4u)
            return 1;
    }
    return 0;
}

int ios27_darwin_probe(const uint8_t *data, size_t size,
                       const ios27_macho_info *info,
                       ios27_darwin_probe_result *result,
                       char *error, size_t error_size) {
    uint64_t regs[31] = {0};
    uint64_t pc;
    unsigned carry = 0u;
    size_t svc_count = 0u;
    if (!data || !info || !result)
        return fail(error, error_size, "null probe input");
    memset(result, 0, sizeof(*result));
    if (info->cputype != 0x0100000cu || info->filetype != IOS27_MH_EXECUTE ||
        !info->has_main || info->needs_dyld || info->has_dylibs ||
        info->has_fixups || info->encrypted || info->segment_count > IOS27_MAX_SEGMENTS)
        return fail(error, error_size, "requires a plain freestanding ARM64 Mach-O with LC_MAIN");
    if ((info->entryoff & 3u) != 0u)
        return fail(error, error_size, "unaligned entry point");
    pc = info->entryoff;
    for (size_t step = 0; step < IOS27_MAX_STEPS; ++step) {
        uint32_t op;
        uint32_t rd, hw, imm16;
        uint64_t value;
        int is64;
        if (!executable_instruction(info, pc, size))
            return fail(error, error_size, "instruction outside executable file-backed segment");
        op = get32(data + (size_t)pc);
        result->instructions = step + 1u;
        if (op == IOS27_RET_X30) {
            result->x0 = regs[0];
            result->x16 = regs[16];
            result->carry = carry;
            result->syscalls = svc_count;
            if (error && error_size) error[0] = '\0';
            return 0;
        }
        if (op == IOS27_NOP) {
            pc += 4u;
            continue;
        }
        if (op == IOS27_SVC_80) {
            unsigned stop = 0u;
            ios27_darwin_dispatch(regs[16], regs[0], &regs[0], &carry, &stop);
            ++svc_count;
            if (stop) {
                result->x0 = regs[0];
                result->x16 = regs[16];
                result->syscalls = svc_count;
                result->exited = 1u;
                if (error && error_size) error[0] = '\0';
                return 0;
            }
            pc += 4u;
            continue;
        }
        /* MOVZ/MOVK for Wn and Xn, no arbitrary guest code execution. */
        if ((op & 0x7f800000u) != 0x52800000u &&
            (op & 0x7f800000u) != 0x72800000u)
            return fail(error, error_size, "unsupported ARM64 instruction");
        rd = op & 31u;
        hw = (op >> 21) & 3u;
        imm16 = (op >> 5) & 0xffffu;
        is64 = (op & 0x80000000u) != 0u;
        if ((!is64 && hw >= 2u) || rd == 31u)
            return fail(error, error_size, "unsupported register/immediate encoding");
        value = (uint64_t)imm16 << (16u * hw);
        if ((op & 0x7f800000u) == 0x72800000u) {
            const uint64_t mask = (uint64_t)0xffffu << (16u * hw);
            value = (regs[rd] & ~mask) | value;
        }
        regs[rd] = is64 ? value : (uint32_t)value;
        if (pc > UINT64_MAX - 4u)
            return fail(error, error_size, "instruction address overflow");
        pc += 4u;
    }
    return fail(error, error_size, "probe exceeded instruction limit");
}
