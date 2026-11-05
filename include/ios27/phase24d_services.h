#ifndef IOS27_PHASE24D_SERVICES_H
#define IOS27_PHASE24D_SERVICES_H
#include <stddef.h>
#include <stdint.h>
/* Sandboxed modeled process/Mach service ABI, not an XNU trap or native runner. */
#define IOS27_24D_ABI UINT32_C(0x240d0001)
#define IOS27_24D_TASKS 8u
#define IOS27_24D_THREADS 12u
#define IOS27_24D_MEMORY 65536u
#define IOS27_24D_PAGE 4096u
#define IOS27_24D_FDS 24u
#define IOS27_24D_OFD 24u
#define IOS27_24D_FILES 8u
#define IOS27_24D_FILE_BYTES 4096u
#define IOS27_24D_PORTS 32u
#define IOS27_24D_QUEUE 8u
#define IOS27_24D_PAYLOAD 128u
#define IOS27_24D_READ 1u
#define IOS27_24D_WRITE 2u
#define IOS27_24D_OPEN_READ 1u
#define IOS27_24D_OPEN_WRITE 2u
#define IOS27_24D_CLOEXEC 1u
#define IOS27_24D_SYS_READ 3u
#define IOS27_24D_SYS_WRITE 4u
#define IOS27_24D_SYS_OPEN 5u
#define IOS27_24D_SYS_CLOSE 6u
#define IOS27_24D_SYS_GETPID 20u
#define IOS27_24D_SYS_DUP 41u
#define IOS27_24D_SYS_LSEEK 199u
#define IOS27_24D_SYS_DUP2 90u
#define IOS27_24D_SYS_FD_FLAGS UINT32_C(0x24d1)
#define IOS27_24D_SYS_IPC_SEND UINT32_C(0x24d2)
#define IOS27_24D_SYS_IPC_RECV UINT32_C(0x24d3)

typedef enum {
    IOS27_24D_OK=0, IOS27_24D_INVALID=-1, IOS27_24D_FAULT=-2,
    IOS27_24D_DENIED=-3, IOS27_24D_DEAD=-4, IOS27_24D_LIMIT=-5,
    IOS27_24D_TIMEOUT=-6, IOS27_24D_INTERRUPTED=-7,
    IOS27_24D_UNSUPPORTED=-8, IOS27_24D_BAD_ABI=-9,
    IOS27_24D_BUSY=-10
} ios27_24d_status;
typedef struct ios27_24d_runtime ios27_24d_runtime;
typedef struct { uint32_t abi, number; uint64_t x[6]; } ios27_24d_frame;
typedef struct { int32_t status; int64_t value; uint64_t correlation; } ios27_24d_result;
/* Stable 160-byte, little-endian wire; payload lengths explicitly bounded. */
typedef struct {
    uint64_t correlation, reply_port, transfer_port;
    uint32_t opcode, size;
    uint8_t payload[IOS27_24D_PAYLOAD];
} ios27_24d_message;
typedef struct {
    uint64_t calls, rejected, fd_ops, copies, sends, receives, waits,
             timed_out, cancelled, dead_notifications, cleaned_fds,
             cleaned_ports, active_tasks, active_threads, queued;
} ios27_24d_stats;

int ios27_24d_new(ios27_24d_runtime **out);
void ios27_24d_free(ios27_24d_runtime *rt);
int ios27_24d_process_create(ios27_24d_runtime *rt, uint64_t *pid);
int ios27_24d_process_destroy(ios27_24d_runtime *rt, uint64_t pid);
int ios27_24d_thread_create(ios27_24d_runtime *rt, uint64_t pid, uint64_t *tid);
int ios27_24d_thread_cancel(ios27_24d_runtime *rt, uint64_t pid, uint64_t tid);
int ios27_24d_thread_destroy(ios27_24d_runtime *rt, uint64_t pid, uint64_t tid);
int ios27_24d_memory_protect(ios27_24d_runtime *rt, uint64_t pid, uint32_t page, uint32_t permissions);
int ios27_24d_memory_write(ios27_24d_runtime *rt, uint64_t pid, uint64_t addr, const void *src, size_t len);
int ios27_24d_memory_read(ios27_24d_runtime *rt, uint64_t pid, uint64_t addr, void *dst, size_t len);
int ios27_24d_seed_file(ios27_24d_runtime *rt, uint64_t pid, const char *path, const void *bytes, size_t len);
int ios27_24d_port_allocate(ios27_24d_runtime *rt, uint64_t owner, uint64_t *name);
int ios27_24d_port_grant(ios27_24d_runtime *rt, uint64_t owner, uint64_t name, uint64_t target, int once);
int ios27_24d_port_destroy(ios27_24d_runtime *rt, uint64_t owner, uint64_t name);
int ios27_24d_poll_dead(ios27_24d_runtime *rt, uint64_t pid, uint64_t *name);
int ios27_24d_trap(ios27_24d_runtime *rt, uint64_t pid, uint64_t tid,
                   const ios27_24d_frame *frame, ios27_24d_result *result);
void ios27_24d_get_stats(ios27_24d_runtime *rt, ios27_24d_stats *out);
#endif
