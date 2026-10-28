#ifndef IOS27_LIBSYSTEM21E_TLS_H
#define IOS27_LIBSYSTEM21E_TLS_H

#include <stddef.h>
#include <stdint.h>

/* Portable native host bridge, NOT Apple's pthread ABI, pthread_key_t,
 * Mach thread port or guest pointer representation. */
#define IOS27_21E_MAX_KEYS 64u
#define IOS27_21E_MAX_CANCEL 128u
#define IOS27_21E_MAX_CLEANUPS 64u
#define IOS27_21E_DESTRUCTOR_PASSES 4u

typedef struct ios27_21e_runtime ios27_21e_runtime;
/* Host-only bridge handles encode owning runtime identity to reject
 * accidental cross-runtime aliasing when slot/generation numbers match. */
typedef struct { uint32_t slot; uint32_t generation; uint64_t runtime_id; } ios27_21e_handle;
typedef void (*ios27_21e_callback)(void *);
enum ios27_21e_cancel_state {
    IOS27_21E_CANCEL_ENABLE = 0,
    IOS27_21E_CANCEL_DISABLE = 1
};

int ios27_21e_create(ios27_21e_runtime **out);
/* Requires external lifetime coordination; EBUSY while keys, workers,
 * attached threads or cancellation tokens are outstanding. */
int ios27_21e_destroy(ios27_21e_runtime *runtime);
int ios27_21e_attach(ios27_21e_runtime *runtime);
/* Runs cleanup handlers and TLS destructors for the current thread. */
int ios27_21e_thread_detach(ios27_21e_runtime *runtime);
int ios27_21e_key_create(ios27_21e_runtime *runtime,
                         ios27_21e_callback destructor, ios27_21e_handle *out);
/* EBUSY while destructor callback for this key is active. */
int ios27_21e_key_delete(ios27_21e_runtime *runtime, ios27_21e_handle key);
int ios27_21e_setspecific(ios27_21e_runtime *runtime,
                           ios27_21e_handle key, void *value);
int ios27_21e_getspecific(ios27_21e_runtime *runtime,
                           ios27_21e_handle key, void **out);

/* Tokens are bound to the registering native thread. Cancellation is never
 * asynchronous: checkpoints report ECANCELED and do not unwind the stack. */
int ios27_21e_cancel_register(ios27_21e_runtime *runtime, ios27_21e_handle *out);
int ios27_21e_cancel_unregister(ios27_21e_runtime *runtime, ios27_21e_handle token);
int ios27_21e_cancel_request(ios27_21e_runtime *runtime, ios27_21e_handle token);
int ios27_21e_cancel_setstate(ios27_21e_runtime *runtime,
                              enum ios27_21e_cancel_state state,
                              enum ios27_21e_cancel_state *previous);
int ios27_21e_cancel_checkpoint(ios27_21e_runtime *runtime);

/* Strictly nested stack, process-unique nonzero increasing scope IDs. Pop
 * removes ownership BEFORE optional callback invocation. */
int ios27_21e_cleanup_push(ios27_21e_runtime *runtime,
                           ios27_21e_callback handler, void *argument,
                           uint64_t *scope);
int ios27_21e_cleanup_pop(ios27_21e_runtime *runtime, uint64_t scope,
                          int execute);
int ios27_21e_cleanup_unwind(ios27_21e_runtime *runtime);
/* Counts attached thread contexts, active TLS keys, cancellation tokens.
 * The final two counters are cumulative completed cleanup/destructor calls. */
int ios27_21e_stats(ios27_21e_runtime *runtime, size_t *attached,
                     size_t *keys, size_t *tokens, uint64_t *cleanups,
                     uint64_t *destructors);
#endif
