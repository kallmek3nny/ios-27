#ifndef IOS27_MACH_V2_H
#define IOS27_MACH_V2_H

#include "ios27/mach_ipc.h"

/*
 * Phase 10 synthetic task-local Mach namespace, separate from the phase 9
 * globally named compatibility interface. All task IDs and rights exist
 * ONLY in one caller-owned in-process fixture. NOT an XNU trap or sandbox.
 */
#define IOS27_MACH_V2_NAMES 64u
#define IOS27_MACH_MOVE_SEND_ONCE 18u
#define IOS27_MACH_MAKE_SEND_ONCE 21u

typedef struct ios27_mach_v2 ios27_mach_v2;

ios27_mach_v2 *ios27_mach_v2_new(void);
void ios27_mach_v2_free(ios27_mach_v2 *space);

/* Receive names are task-local and never recycled; no implicit grants. */
int ios27_mach_v2_allocate(ios27_mach_v2 *space, uint32_t owner,
                           uint32_t *local_name);
/* Return the recipient's own local name, which may differ or collide. */
int ios27_mach_v2_make_send(ios27_mach_v2 *space, uint32_t owner,
                            uint32_t receive_name, uint32_t recipient,
                            uint32_t *recipient_name);
int ios27_mach_v2_drop_send(ios27_mach_v2 *space, uint32_t task,
                            uint32_t local_name);
int ios27_mach_v2_drop_send_once(ios27_mach_v2 *space, uint32_t task,
                                 uint32_t local_name);
int ios27_mach_v2_rights(const ios27_mach_v2 *space, uint32_t task,
                         uint32_t local_name, uint32_t *send,
                         uint32_t *send_once, unsigned *receive);
int ios27_mach_v2_destroy(ios27_mach_v2 *space, uint32_t owner,
                          uint32_t local_receive_name);

/* Atomically retire a synthetic task's namespace and all its receive ports.
 * Fail closed (without changes) if any task-owned port or local alias has
 * an in-flight transfer. Names stay consumed to prevent stale-handle reuse.
 * This is not OS task termination or an authentication boundary. */
int ios27_mach_v2_task_teardown(ios27_mach_v2 *space, uint32_t task);

/*
 * Header bits: remote disposition MOVE_SEND(17), MOVE_SEND_ONCE(18),
 * or COPY_SEND(19). MOVE_SEND consumes one local send uref only after
 * successful enqueue; a combined destination+descriptor move needs two.
 * Optional local disposition MAKE_SEND_ONCE(21) in bits 8..15 with
 * a sender-owned receive name in header.local_port; receiver obtains a
 * task-local reply send-once name in received header.remote_port, with
 * MOVE_SEND_ONCE in the received remote disposition bits.
 * Optionally one port descriptor carries COPY_SEND or MOVE_SEND.
 *
 * No OOL, trailers, port sets, vouchers, send-once descriptor, kernel RPC,
 * background threads or arbitrary host syscall forwarding. Timeout
 * arguments are deliberately poll-only: full/empty returns TIMEOUT.
 * Invalid/failed sends do not consume rights or alter any queue.
 */
int ios27_mach_v2_send(ios27_mach_v2 *space, uint32_t task,
                       const void *wire, size_t size, uint32_t timeout_ms);
int ios27_mach_v2_receive(ios27_mach_v2 *space, uint32_t task,
                          uint32_t local_receive_name, void *out,
                          size_t *inout_size, uint32_t timeout_ms);

#endif
