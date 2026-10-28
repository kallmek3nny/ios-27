#ifndef IOS27_MACH_IPC_H
#define IOS27_MACH_IPC_H

#include <stddef.h>
#include <stdint.h>

/*
 * Synthetic, in-process Darwin/Mach-style message subset. The wire sizes
 * and values below deliberately follow the public XNU message.h ABI, but
 * this is NOT a kernel trap, system-wide Mach namespace, or iOS IPC stack.
 */
#define IOS27_MACH_TASK_MAX 8u
#define IOS27_MACH_PORT_MAX 32u
#define IOS27_MACH_QUEUE_MAX 4u
#define IOS27_MACH_MSG_MAX 256u
#define IOS27_MACH_RIGHT_MAX 1024u

#define IOS27_MACH_COPY_SEND 19u
#define IOS27_MACH_MOVE_SEND 17u
#define IOS27_MACH_COMPLEX UINT32_C(0x80000000)
#define IOS27_MACH_PORT_DESCRIPTOR 0u
#define IOS27_MACH_OOL_DESCRIPTOR 1u
/* Bounded fixture copy budget, not an operating-system VM allocation. */
#define IOS27_MACH_OOL_MAX 128u

typedef struct {
    uint32_t bits;
    uint32_t size;
    uint32_t remote_port;
    uint32_t local_port;
    uint32_t voucher_port;
    int32_t id;
} ios27_mach_header;

typedef struct {
    uint32_t name;
    uint32_t pad1;
    uint16_t pad2;
    uint8_t disposition;
    uint8_t type;
} ios27_mach_port_descriptor;

typedef enum {
    IOS27_MACH_OK = 0,
    IOS27_MACH_BAD_ARGUMENT = -1,
    IOS27_MACH_BAD_RIGHT = -2,
    IOS27_MACH_LIMIT = -3,
    IOS27_MACH_TIMEOUT = -4,
    IOS27_MACH_TOO_LARGE = -5,
    IOS27_MACH_UNSUPPORTED = -6
} ios27_mach_result;

typedef struct ios27_mach_space ios27_mach_space;

/* Completely private, synchronous, bounded fixture namespace. */
ios27_mach_space *ios27_mach_space_new(void);
void ios27_mach_space_free(ios27_mach_space *space);

/* Task names are 1..IOS27_MACH_TASK_MAX. Names never recycle in a space. */
int ios27_mach_port_allocate(ios27_mach_space *space, uint32_t receive_owner,
                            uint32_t *port_name);
int ios27_mach_port_make_send(ios27_mach_space *space, uint32_t receive_owner,
                             uint32_t port_name, uint32_t recipient);
int ios27_mach_port_drop_send(ios27_mach_space *space, uint32_t task,
                             uint32_t port_name);
int ios27_mach_port_destroy(ios27_mach_space *space, uint32_t receive_owner,
                            uint32_t port_name);
int ios27_mach_send_refs(const ios27_mach_space *space, uint32_t task,
                         uint32_t port_name, uint32_t *count);

/*
 * mach_msg_header_t wire format (24 bytes) and, optionally, one
 * mach_msg_body_t (4 bytes) + mach_msg_port_descriptor_t (12 bytes).
 * Exactly one complex descriptor transfers a send right. COPY_SEND keeps
 * the sender's right, MOVE_SEND consumes one on successful enqueue.
 * The remote disposition must be COPY_SEND; local and voucher ports zero.
 *
 * No blocking: when the queue is full/empty, return TIMEOUT regardless
 * of timeout_ms. This is deterministic testing, not kernel timing.
 * On failure, both message queue and rights remain unchanged.
 */
int ios27_mach_send(ios27_mach_space *space, uint32_t sender,
                    const void *message, size_t length,
                    uint32_t timeout_ms);
int ios27_mach_receive(ios27_mach_space *space, uint32_t receiver,
                       uint32_t port_name, void *buffer,
                       size_t *inout_length, uint32_t timeout_ms);

#endif
