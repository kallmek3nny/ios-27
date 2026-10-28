#ifndef IOS27_LIBSYSTEM21H_LIFECYCLE_H
#define IOS27_LIBSYSTEM21H_LIFECYCLE_H
#include <stddef.h>
#include <stdint.h>
/* Host synchronization-object lifetime model, not a Darwin pthread ABI claim. */
typedef struct ios27_21h_runtime ios27_21h_runtime;
typedef struct { uint32_t slot; uint32_t generation; } ios27_21h_handle;
typedef struct {
    size_t live, closing, owned, waiting, retired;
} ios27_21h_stats;
/* Caller must externally serialize final runtime_destroy against fresh entry. */
int ios27_21h_runtime_create(ios27_21h_runtime **out, size_t capacity);
int ios27_21h_runtime_destroy(ios27_21h_runtime *rt);
int ios27_21h_object_create(ios27_21h_runtime *rt, ios27_21h_handle *out);
/* Close prevents new acquisitions, wakes all waiters, permits owner unlock. */
int ios27_21h_object_close(ios27_21h_runtime *rt, ios27_21h_handle h);
/* Destroy fails EBUSY while a waiter or owner references the slot. */
int ios27_21h_object_destroy(ios27_21h_runtime *rt, ios27_21h_handle h);
int ios27_21h_lock(ios27_21h_runtime *rt, ios27_21h_handle h);
int ios27_21h_trylock(ios27_21h_runtime *rt, ios27_21h_handle h);
int ios27_21h_unlock(ios27_21h_runtime *rt, ios27_21h_handle h);
int ios27_21h_get_stats(ios27_21h_runtime *rt, ios27_21h_stats *out);
#endif
