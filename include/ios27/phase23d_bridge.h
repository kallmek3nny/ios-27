#ifndef IOS27_PHASE23D_BRIDGE_H
#define IOS27_PHASE23D_BRIDGE_H
#include <stddef.h>
#include <stdint.h>

/* Original-source deterministic Mach/BSD compatibility model. This API does
 * not perform XNU traps, guest code execution, host I/O or host network I/O. */
#define IOS27_23D_ABI UINT32_C(0x230d0001)
#define IOS27_23D_MEMORY 16384u
#define IOS27_23D_PAGE 4096u
#define IOS27_23D_TASKS 8u
#define IOS27_23D_THREADS 8u
#define IOS27_23D_PORTS 32u
#define IOS27_23D_FDS 12u
#define IOS27_23D_FILE_MAX 2048u
#define IOS27_23D_PAYLOAD 128u
#define IOS27_23D_MAX_DEPTH 8u
#define IOS27_23D_READ 1u
#define IOS27_23D_WRITE 2u
#define IOS27_23D_SERVICE_ECHO 1u
#define IOS27_23D_SERVICE_SUM 2u
#define IOS27_23D_SERVICE_RELAY 3u
#define IOS27_23D_RPC_ECHO 1u
#define IOS27_23D_RPC_SUM 2u
#define IOS27_23D_RPC_RELAY 3u
#define IOS27_23D_RPC_TRANSFER_SEND 1u
#define IOS27_23D_SYS_READ 3u
#define IOS27_23D_SYS_WRITE 4u
#define IOS27_23D_SYS_OPEN 5u
#define IOS27_23D_SYS_CLOSE 6u
#define IOS27_23D_SYS_GETPID 20u
#define IOS27_23D_SYS_LSEEK 199u
#define IOS27_23D_SYS_RPC 0x230du

/* Every guest address is an offset into a process-owned 16 KiB arena. */
typedef enum {
    IOS27_23D_OK = 0, IOS27_23D_INVALID = -1, IOS27_23D_FAULT = -2,
    IOS27_23D_DENIED = -3, IOS27_23D_DEAD = -4,
    IOS27_23D_LIMIT = -5, IOS27_23D_TIMEOUT = -6,
    IOS27_23D_INTERRUPTED = -7, IOS27_23D_UNSUPPORTED = -8,
    IOS27_23D_BUSY = -9, IOS27_23D_NOMEM = -10,
    IOS27_23D_BAD_ABI = -11
} ios27_23d_status;

typedef struct ios27_23d_runtime ios27_23d_runtime;
typedef struct { uint32_t abi, number; uint64_t x[6]; } ios27_23d_frame;
typedef struct { int32_t status; int64_t value; uint64_t correlation; } ios27_23d_result;
/* Serialized as a 32-byte little-endian ABI structure, no C padding assumed. */
typedef struct {
    uint32_t opcode, size;
    uint64_t correlation, transfer;
    uint32_t flags, reserved;
    uint8_t data[IOS27_23D_PAYLOAD];
} ios27_23d_wire;
typedef struct {
    uint64_t calls, rejects, rpc_calls, copies, fd_operations, nested_calls;
    uint64_t cancelled, port_deaths;
} ios27_23d_stats;

int ios27_23d_new(ios27_23d_runtime **out);
void ios27_23d_free(ios27_23d_runtime *rt);
int ios27_23d_process_create(ios27_23d_runtime *rt, uint64_t *pid);
int ios27_23d_process_destroy(ios27_23d_runtime *rt, uint64_t pid);
int ios27_23d_thread_create(ios27_23d_runtime *rt, uint64_t pid, uint64_t *tid);
int ios27_23d_thread_cancel(ios27_23d_runtime *rt, uint64_t pid, uint64_t tid);
int ios27_23d_thread_destroy(ios27_23d_runtime *rt, uint64_t pid, uint64_t tid);
int ios27_23d_memory_protect(ios27_23d_runtime *rt, uint64_t pid, uint32_t page, uint32_t permissions);
int ios27_23d_memory_read(ios27_23d_runtime *rt, uint64_t pid, uint64_t offset, void *destination, size_t size);
int ios27_23d_memory_write(ios27_23d_runtime *rt, uint64_t pid, uint64_t offset, const void *source, size_t size);
int ios27_23d_seed(ios27_23d_runtime *rt, uint64_t pid, const uint8_t *data, size_t size);
int ios27_23d_service_register(ios27_23d_runtime *rt, uint64_t owner, uint32_t name, uint32_t kind, uint64_t *port);
int ios27_23d_service_grant(ios27_23d_runtime *rt, uint64_t owner, uint64_t port, uint64_t recipient, int send_once);
int ios27_23d_service_destroy(ios27_23d_runtime *rt, uint64_t owner, uint64_t port);
int ios27_23d_trap(ios27_23d_runtime *rt, uint64_t pid, uint64_t tid, const ios27_23d_frame *frame, ios27_23d_result *out);
void ios27_23d_get_stats(ios27_23d_runtime *rt, ios27_23d_stats *out);
#endif
