#ifndef IOS27_PHASE26B_BSD_H
#define IOS27_PHASE26B_BSD_H

#include <stddef.h>
#include <stdint.h>

/* Private, host-independent guest address space. Never a host pointer. */
#define IOS27_26B_BASE UINT64_C(0x100000000)
#define IOS27_26B_PAGE 4096u
#define IOS27_26B_PAGES 64u
#define IOS27_26B_MAX_IO 8192u
#define IOS27_26B_SVC 0xd4001001u
#define IOS27_26B_BSD_CLASS 0x02000000u
#define IOS27_26B_PROT_READ 1u
#define IOS27_26B_PROT_WRITE 2u

typedef struct ios27_phase26b_process ios27_phase26b_process;
typedef struct {
    uint64_t x[8];     /* x0-x7: inputs; x0: returned data or positive errno */
    uint64_t x16;      /* Darwin syscall selector */
    uint32_t instruction; /* must be SVC #0x80 */
    unsigned carry;    /* Darwin NZCV.C: 1 on failure */
} ios27_phase26b_regs;

/* Destroy only after all calling threads have joined. stop() prevents new
 * operations, but cannot make a concurrently freed process pointer safe. */
ios27_phase26b_process *ios27_phase26b_create(uint32_t guest_pid,
                                               uint64_t epoch_seconds);
void ios27_phase26b_stop(ios27_phase26b_process *p);
void ios27_phase26b_destroy(ios27_phase26b_process *p);

/* Test/loader boundary: pages 0..15 are pre-mapped RW; the remaining
 * pages require guest mmap. No implicit host memory access. */
int ios27_phase26b_copyin(ios27_phase26b_process *p, uint64_t address,
                          void *destination, size_t length);
int ios27_phase26b_copyout(ios27_phase26b_process *p, uint64_t address,
                           const void *source, size_t length);

/* An ABI register/trap dispatcher, not yet an ARM64 instruction executor.
 * It NEVER forwards syscalls to the host. */
void ios27_phase26b_trap(ios27_phase26b_process *p, ios27_phase26b_regs *r);
#endif
