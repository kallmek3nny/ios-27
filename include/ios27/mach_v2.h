#ifndef IOS27_MACH_V2_H
#define IOS27_MACH_V2_H

#include "ios27/mach_ipc.h"

/*
 * Phase 10 synthetic task-local Mach namespace, separate from the phase 9
 * globally named compatibility interface. All task IDs and rights exist
 * ONLY in one caller-owned in-process fixture. NOT an XNU trap or sandbox.
 */
#define IOS27_MACH_V2_NAMES 64u
#define IOS27_MACH_V2_DESCRIPTORS 4u
#define IOS27_MACH_V2_PORT_SETS 16u
/* mach_msg_seqno_trailer_t: type, size, sequence number (little-endian). */
#define IOS27_MACH_SEQNO_TRAILER_SIZE 12u
#define IOS27_MACH_MOVE_SEND_ONCE 18u
#define IOS27_MACH_MAKE_SEND_ONCE 21u

typedef struct ios27_mach_v2 ios27_mach_v2;

ios27_mach_v2 *ios27_mach_v2_new(void);
void ios27_mach_v2_free(ios27_mach_v2 *space);

/* Fail-closed host-boundary preflight: a caller-controlled pointer/span
 * cannot alias private IPC state. Also rejects overflowing host spans. */
int ios27_mach_v2_validate_external(const ios27_mach_v2 *space,
                                     const void *ptr, size_t length);

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

/* Optional synthetic dead-name observation. Subscribes an existing local
 * send/send-once alias. Destruction or task teardown queues the retired
 * name for surviving watchers, with bounded FIFO capacity. Saturated
 * event queues reject retirement atomically with LIMIT; polling an empty
 * queue returns TIMEOUT without modifying the output. Not XNU notify RPC. */
int ios27_mach_v2_watch_dead_name(ios27_mach_v2 *space, uint32_t task,
                                  uint32_t local_name);
int ios27_mach_v2_poll_dead_name(ios27_mach_v2 *space, uint32_t task,
                                 uint32_t *local_name);

/*
 * Bounded synthetic task-local port sets. A receive right may belong to
 * exactly one set; only its owner can attach/detach it. Set names use the
 * same monotonic per-task namespace as port names, and are never reused.
 * Set receive deterministically scans member ports in round-robin order;
 * empty sets return TIMEOUT without sleeping. An undersized buffer leaves
 * the selected queued message, membership cursor and rights unchanged.
 */
int ios27_mach_v2_set_allocate(ios27_mach_v2 *space, uint32_t owner,
                               uint32_t *set_name);
int ios27_mach_v2_set_add(ios27_mach_v2 *space, uint32_t owner,
                          uint32_t set_name, uint32_t receive_name);
int ios27_mach_v2_set_remove(ios27_mach_v2 *space, uint32_t owner,
                             uint32_t set_name, uint32_t receive_name);
int ios27_mach_v2_set_destroy(ios27_mach_v2 *space, uint32_t owner,
                              uint32_t set_name);
int ios27_mach_v2_receive_set(ios27_mach_v2 *space, uint32_t task,
                              uint32_t set_name, void *out,
                              size_t *inout_size, uint32_t timeout_ms);

/* Optional Darwin-compatible *sequence-number* trailer for a direct
 * port receive. The header's msgh_size remains the message wire size;
 * the 12-byte trailer follows it and counts toward the receive capacity.
 * The trailer has no fabricated audit token or host process identity.
 * Too-small capacity does not dequeue the message or advance seqno.
 */
int ios27_mach_v2_receive_trailer(ios27_mach_v2 *space, uint32_t task,
                                  uint32_t receive_name, void *out,
                                  size_t *inout_size, uint32_t timeout_ms);

/*
 * Header bits: remote disposition MOVE_SEND(17), MOVE_SEND_ONCE(18),
 * or COPY_SEND(19). MOVE_SEND consumes one local send uref only after
 * successful enqueue; a combined destination+descriptor move needs two.
 * Optional local disposition MAKE_SEND_ONCE(21) in bits 8..15 with
 * a sender-owned receive name in header.local_port, or MOVE_SEND_ONCE(18)
 * with an existing send-once name. Both deliver a task-local reply
 * send-once name in received header.remote_port, with MOVE_SEND_ONCE
 * in the received remote disposition bits. MOVE_SEND_ONCE transfers its
 * existing right only after successful enqueue, including when it is
 * also the destination right (which requires two send-once urefs).
 * Up to four port descriptors carry COPY_SEND or MOVE_SEND; they may
 * repeat the same task-local name. All source debits, receiver credits,
 * fresh aliases, and queued grants are reserved as one transaction.
 * On receive, both are normalized to MOVE_SEND, describing the delivered
 * send right rather than the sender's transfer instruction.
 * Caller-owned receive buffers must not overlap the length result or this
 * fixture's internal state; rejected host aliases do not consume messages.
 *
 * No vouchers, send-once descriptor, kernel RPC,
 * background threads or arbitrary host syscall forwarding. Timeout
 * arguments are deliberately poll-only: full/empty returns TIMEOUT.
 * Invalid/failed sends do not consume rights or alter any queue.
 */
int ios27_mach_v2_send(ios27_mach_v2 *space, uint32_t task,
                       const void *wire, size_t size, uint32_t timeout_ms);
int ios27_mach_v2_receive(ios27_mach_v2 *space, uint32_t task,
                          uint32_t local_receive_name, void *out,
                          size_t *inout_size, uint32_t timeout_ms);

/* LP64 mach_msg_ool_descriptor_t: guest-virtual address at +0 (u64),
 * deallocate/copy/pad/type at +8..+11, payload length at +12 (u32).
 * Only one OOL memory descriptor with <= IOS27_MACH_OOL_MAX bytes is
 * supported, potentially mixed with port descriptors. The fixture accepts
 * deallocate=0 and physical/virtual COPY modes (0/1); other modes fail
 * closed. Send snapshots the caller's bounded guest arena, never dereferences
 * a raw guest address. Regular host receive rejects queued OOL messages.
 * Receive_guest_ool writes payload immediately after msgh_size within the
 * explicit output span, and rewrites the OOL address to that guest-virtual
 * offset. *inout_size counts wire + payload; msgh_size remains wire-only.
 * This is an explicit bounded fixture contract, NOT Darwin VM allocation,
 * pager semantics, or full mach_msg OOL ownership.
 */
int ios27_mach_v2_send_guest_ool(ios27_mach_v2 *space, uint32_t task,
                                 const void *wire, size_t size,
                                 uint32_t timeout_ms,
                                 const uint8_t *arena, size_t arena_size);
int ios27_mach_v2_receive_guest_ool(ios27_mach_v2 *space, uint32_t task,
                                    uint32_t receive_name, void *out,
                                    size_t *inout_size, uint32_t timeout_ms,
                                    uint64_t guest_offset);

#endif
