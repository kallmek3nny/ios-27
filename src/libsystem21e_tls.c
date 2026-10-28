#define _POSIX_C_SOURCE 200809L
#include "ios27/libsystem21e_tls.h"

#include <errno.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct slot_key {
    uint32_t generation;
    int used;
    uint32_t inflight;
    ios27_21e_callback destructor;
};
struct context;
struct slot_cancel {
    uint32_t generation;
    int used;
    int pending;
    struct context *owner;
};
struct cleanup {
    uint64_t scope;
    ios27_21e_callback handler;
    void *argument;
};
struct context {
    ios27_21e_runtime *runtime;
    struct context *next;
    void *values[IOS27_21E_MAX_KEYS];
    uint32_t generations[IOS27_21E_MAX_KEYS];
    struct cleanup stack[IOS27_21E_MAX_CLEANUPS];
    size_t depth;
    uint32_t token_slot;
    uint32_t token_generation;
    enum ios27_21e_cancel_state state;
    int tearing_down;
    int draining_cleanup;
    unsigned active_cleanup_callbacks;
};
struct ios27_21e_runtime {
    uint64_t runtime_id;
    pthread_mutex_t mutex;
    pthread_key_t native_key;
    struct context *contexts;
    struct slot_key keys[IOS27_21E_MAX_KEYS];
    struct slot_cancel tokens[IOS27_21E_MAX_CANCEL];
    size_t attached;
    uint64_t completed_cleanup;
    uint64_t completed_destructors;
};

/* Never recycle a runtime identity during this process lifetime. */
static atomic_uint_fast64_t runtime_serial = ATOMIC_VAR_INIT(0);
/* Cleanup scope IDs also survive context retirement and runtime recreation.
 * A stale scope can never accidentally pop a new handler. */
static atomic_uint_fast64_t cleanup_scope_serial = ATOMIC_VAR_INIT(0);
static int mint_scope_id(uint64_t *out) {
    uint_fast64_t old = atomic_load(&cleanup_scope_serial);
    for (;;) {
        if (old == UINT64_MAX) return EOVERFLOW;
        if (atomic_compare_exchange_weak(&cleanup_scope_serial, &old, old + 1u)) {
            *out = (uint64_t)(old + 1u);
            return 0;
        }
    }
}
static int assign_identity(ios27_21e_runtime *runtime) {
    uint_fast64_t old = atomic_load(&runtime_serial);
    for (;;) {
        if (old == UINT64_MAX) return EOVERFLOW;
        if (atomic_compare_exchange_weak(&runtime_serial, &old, old + 1u)) {
            runtime->runtime_id = (uint64_t)(old + 1u);
            return 0;
        }
    }
}
static int lock_runtime(ios27_21e_runtime *runtime) {
    return pthread_mutex_lock(&runtime->mutex);
}
static void unlock_runtime(ios27_21e_runtime *runtime) {
    (void)pthread_mutex_unlock(&runtime->mutex);
}
static uint32_t next_generation(uint32_t old) {
    return old == UINT32_MAX ? 0u : old + 1u;
}
static int key_valid(ios27_21e_runtime *runtime, ios27_21e_handle key) {
    return key.runtime_id == runtime->runtime_id &&
           key.slot < IOS27_21E_MAX_KEYS && key.generation != 0 &&
           runtime->keys[key.slot].used &&
           runtime->keys[key.slot].generation == key.generation;
}
static int token_valid(ios27_21e_runtime *runtime, ios27_21e_handle token) {
    return token.runtime_id == runtime->runtime_id &&
           token.slot < IOS27_21E_MAX_CANCEL && token.generation != 0 &&
           runtime->tokens[token.slot].used &&
           runtime->tokens[token.slot].generation == token.generation;
}
static struct context *current(ios27_21e_runtime *runtime) {
    return pthread_getspecific(runtime->native_key);
}

static int run_cleanup(struct context *ctx) {
    ios27_21e_runtime *runtime = ctx->runtime;
    int err = lock_runtime(runtime);
    if (err) return err;
    if (ctx->draining_cleanup || ctx->active_cleanup_callbacks) {
        unlock_runtime(runtime);
        return EBUSY;
    }
    ctx->draining_cleanup = 1;
    for (;;) {
        if (ctx->depth == 0) break;
        /* Claim this item before calling user code. Reentrant draining cannot
         * consume the remaining stack while this callback is in flight. */
        struct cleanup item = ctx->stack[--ctx->depth];
        ++ctx->active_cleanup_callbacks;
        unlock_runtime(runtime);
        item.handler(item.argument);
        err = lock_runtime(runtime);
        if (err) return err;
        --ctx->active_cleanup_callbacks;
        ++runtime->completed_cleanup;
    }
    ctx->draining_cleanup = 0;
    unlock_runtime(runtime);
    return 0;
}
/* Run destructor on a cleared value, without runtime lock. The generation
 * guard prevents stale values being used with recycled key slots. */
static void run_destructors(struct context *ctx) {
    ios27_21e_runtime *runtime = ctx->runtime;
    for (unsigned pass = 0; pass < IOS27_21E_DESTRUCTOR_PASSES; ++pass) {
        unsigned called = 0;
        for (size_t i = 0; i < IOS27_21E_MAX_KEYS; ++i) {
            ios27_21e_callback callback = NULL;
            void *value = NULL;
            if (lock_runtime(runtime) != 0) return;
            struct slot_key *key = &runtime->keys[i];
            if (key->used && key->destructor && ctx->values[i] &&
                ctx->generations[i] == key->generation) {
                value = ctx->values[i];
                ctx->values[i] = NULL;
                callback = key->destructor;
                ++key->inflight;
            }
            unlock_runtime(runtime);
            if (callback) {
                ++called;
                callback(value);
                if (lock_runtime(runtime) != 0) return;
                --runtime->keys[i].inflight;
                ++runtime->completed_destructors;
                unlock_runtime(runtime);
            }
        }
        if (!called) break;
    }
}
static void teardown_context(struct context *ctx) {
    ios27_21e_runtime *runtime = ctx->runtime;
    if (lock_runtime(runtime) != 0) return;
    if (ctx->tearing_down) {
        unlock_runtime(runtime);
        return;
    }
    ctx->tearing_down = 1;
    unlock_runtime(runtime);
    /* pthread_key destructors receive an already-cleared native key; restore
     * it temporarily so application TLS callbacks can reenter this bridge. */
    (void)pthread_setspecific(runtime->native_key, ctx);
    (void)run_cleanup(ctx);
    run_destructors(ctx);
    (void)pthread_setspecific(runtime->native_key, NULL);
    if (lock_runtime(runtime) != 0) return;
    if (ctx->token_generation != 0) {
        struct slot_cancel *t = &runtime->tokens[ctx->token_slot];
        if (t->used && t->generation == ctx->token_generation && t->owner == ctx) {
            t->used = 0;
            t->pending = 0;
            t->owner = NULL;
        }
    }
    struct context **it = &runtime->contexts;
    while (*it && *it != ctx) it = &(*it)->next;
    if (*it == ctx) *it = ctx->next;
    --runtime->attached;
    unlock_runtime(runtime);
    free(ctx);
}
static void native_destructor(void *value) {
    if (value) teardown_context(value);
}

int ios27_21e_create(ios27_21e_runtime **out) {
    if (!out) return EINVAL;
    *out = NULL;
    ios27_21e_runtime *runtime = calloc(1, sizeof(*runtime));
    if (!runtime) return ENOMEM;
    int err = assign_identity(runtime);
    if (err) { free(runtime); return err; }
    err = pthread_mutex_init(&runtime->mutex, NULL);
    if (err) { free(runtime); return err; }
    err = pthread_key_create(&runtime->native_key, native_destructor);
    if (err) { (void)pthread_mutex_destroy(&runtime->mutex); free(runtime); return err; }
    *out = runtime;
    return 0;
}
int ios27_21e_destroy(ios27_21e_runtime *runtime) {
    if (!runtime) return EINVAL;
    int err = lock_runtime(runtime);
    if (err) return err;
    if (runtime->attached) { unlock_runtime(runtime); return EBUSY; }
    for (size_t i = 0; i < IOS27_21E_MAX_KEYS; ++i)
        if (runtime->keys[i].used || runtime->keys[i].inflight) {
            unlock_runtime(runtime); return EBUSY;
        }
    for (size_t i = 0; i < IOS27_21E_MAX_CANCEL; ++i)
        if (runtime->tokens[i].used) { unlock_runtime(runtime); return EBUSY; }
    unlock_runtime(runtime);
    err = pthread_key_delete(runtime->native_key);
    if (err) return err;
    err = pthread_mutex_destroy(&runtime->mutex);
    if (err) return err;
    free(runtime);
    return 0;
}
int ios27_21e_attach(ios27_21e_runtime *runtime) {
    if (!runtime) return EINVAL;
    if (current(runtime)) return 0;
    struct context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return ENOMEM;
    ctx->runtime = runtime;
    ctx->state = IOS27_21E_CANCEL_ENABLE;
    int err = lock_runtime(runtime);
    if (err) { free(ctx); return err; }
    err = pthread_setspecific(runtime->native_key, ctx);
    if (!err) {
        ctx->next = runtime->contexts;
        runtime->contexts = ctx;
        ++runtime->attached;
    }
    unlock_runtime(runtime);
    if (err) free(ctx);
    return err;
}
int ios27_21e_thread_detach(ios27_21e_runtime *runtime) {
    if (!runtime) return EINVAL;
    struct context *ctx = current(runtime);
    if (!ctx) return EINVAL;
    if (lock_runtime(runtime) != 0) return EBUSY;
    int cannot_detach = ctx->tearing_down || ctx->active_cleanup_callbacks || ctx->draining_cleanup;
    unlock_runtime(runtime);
    if (cannot_detach) return EBUSY;
    teardown_context(ctx);
    return 0;
}
int ios27_21e_key_create(ios27_21e_runtime *runtime,
                         ios27_21e_callback destructor, ios27_21e_handle *out) {
    if (!runtime || !out) return EINVAL;
    int err = lock_runtime(runtime);
    if (err) return err;
    int rc = EAGAIN;
    for (uint32_t i = 0; i < IOS27_21E_MAX_KEYS; ++i) {
        struct slot_key *key = &runtime->keys[i];
        if (!key->used && key->inflight == 0 && key->generation != UINT32_MAX) {
            key->generation = next_generation(key->generation);
            key->used = 1;
            key->destructor = destructor;
            out->slot = i;
            out->generation = key->generation;
            out->runtime_id = runtime->runtime_id;
            rc = 0;
            break;
        }
    }
    unlock_runtime(runtime);
    return rc;
}
int ios27_21e_key_delete(ios27_21e_runtime *runtime, ios27_21e_handle key) {
    if (!runtime) return EINVAL;
    int err = lock_runtime(runtime);
    if (err) return err;
    int rc = EINVAL;
    if (key_valid(runtime, key)) {
        struct slot_key *s = &runtime->keys[key.slot];
        if (s->inflight) rc = EBUSY;
        else { s->used = 0; s->destructor = NULL; rc = 0; }
    }
    unlock_runtime(runtime);
    return rc;
}
int ios27_21e_setspecific(ios27_21e_runtime *runtime,
                           ios27_21e_handle key, void *value) {
    if (!runtime) return EINVAL;
    /* Reject stale/invalid handles before allocating a thread context.
     * Recheck under the write lock because deletion may race attach. */
    int err = lock_runtime(runtime);
    if (err) return err;
    int valid = key_valid(runtime, key);
    unlock_runtime(runtime);
    if (!valid) return EINVAL;
    const int was_attached = current(runtime) != NULL;
    err = ios27_21e_attach(runtime);
    if (err) return err;
    err = lock_runtime(runtime);
    if (err) return err;
    struct context *ctx = current(runtime);
    int rc = EINVAL;
    if (key_valid(runtime, key)) {
        ctx->values[key.slot] = value;
        ctx->generations[key.slot] = key.generation;
        rc = 0;
    }
    unlock_runtime(runtime);
    /* A key may be deleted after the precheck while the current thread
     * attaches. Reclaim an otherwise-empty context created by this call. */
    if (rc == EINVAL && !was_attached)
        (void)ios27_21e_thread_detach(runtime);
    return rc;
}
int ios27_21e_getspecific(ios27_21e_runtime *runtime,
                           ios27_21e_handle key, void **out) {
    if (!runtime || !out) return EINVAL;
    *out = NULL;
    int err = lock_runtime(runtime);
    if (err) return err;
    struct context *ctx = current(runtime);
    int rc = EINVAL;
    if (key_valid(runtime, key)) {
        if (ctx && ctx->generations[key.slot] == key.generation)
            *out = ctx->values[key.slot];
        rc = 0;
    }
    unlock_runtime(runtime);
    return rc;
}
int ios27_21e_cancel_register(ios27_21e_runtime *runtime, ios27_21e_handle *out) {
    if (!runtime || !out) return EINVAL;
    const int was_attached = current(runtime) != NULL;
    int err = ios27_21e_attach(runtime);
    if (err) return err;
    err = lock_runtime(runtime);
    if (err) return err;
    struct context *ctx = current(runtime);
    int rc = ctx->tearing_down ? EBUSY : EALREADY;
    if (!ctx->token_generation && !ctx->tearing_down) {
        rc = EAGAIN;
        for (uint32_t i = 0; i < IOS27_21E_MAX_CANCEL; ++i) {
            struct slot_cancel *t = &runtime->tokens[i];
            if (!t->used && t->generation != UINT32_MAX) {
                t->generation = next_generation(t->generation);
                t->used = 1;
                t->owner = ctx;
                t->pending = 0;
                ctx->token_slot = i;
                ctx->token_generation = t->generation;
                ctx->state = IOS27_21E_CANCEL_ENABLE;
                out->slot = i;
                out->generation = t->generation;
                out->runtime_id = runtime->runtime_id;
                rc = 0;
                break;
            }
        }
    }
    unlock_runtime(runtime);
    /* Quota exhaustion must not strand a just-created native thread context. */
    if (rc == EAGAIN && !was_attached)
        (void)ios27_21e_thread_detach(runtime);
    return rc;
}
int ios27_21e_cancel_unregister(ios27_21e_runtime *runtime, ios27_21e_handle token) {
    if (!runtime) return EINVAL;
    int err = lock_runtime(runtime);
    if (err) return err;
    struct context *ctx = current(runtime);
    int rc = EINVAL;
    if (token_valid(runtime, token) && ctx &&
        runtime->tokens[token.slot].owner == ctx && !ctx->tearing_down) {
        rc = ctx->depth ? EBUSY : 0;
        if (!rc) {
            struct slot_cancel *t = &runtime->tokens[token.slot];
            t->used = 0;
            t->pending = 0;
            t->owner = NULL;
            ctx->token_generation = 0;
        }
    }
    unlock_runtime(runtime);
    return rc;
}
int ios27_21e_cancel_request(ios27_21e_runtime *runtime, ios27_21e_handle token) {
    if (!runtime) return EINVAL;
    int err = lock_runtime(runtime);
    if (err) return err;
    int rc = EINVAL;
    if (token_valid(runtime, token)) {
        runtime->tokens[token.slot].pending = 1;
        rc = 0;
    }
    unlock_runtime(runtime);
    return rc;
}
int ios27_21e_cancel_setstate(ios27_21e_runtime *runtime,
                              enum ios27_21e_cancel_state state,
                              enum ios27_21e_cancel_state *previous) {
    if (!runtime || (state != IOS27_21E_CANCEL_ENABLE &&
                     state != IOS27_21E_CANCEL_DISABLE)) return EINVAL;
    int err = lock_runtime(runtime);
    if (err) return err;
    struct context *ctx = current(runtime);
    int rc = EINVAL;
    if (ctx && ctx->token_generation && !ctx->tearing_down) {
        if (previous) *previous = ctx->state;
        ctx->state = state;
        rc = 0;
    }
    unlock_runtime(runtime);
    return rc;
}
int ios27_21e_cancel_checkpoint(ios27_21e_runtime *runtime) {
    if (!runtime) return EINVAL;
    int err = lock_runtime(runtime);
    if (err) return err;
    struct context *ctx = current(runtime);
    int rc = EINVAL;
    if (ctx && ctx->token_generation) {
        struct slot_cancel *t = &runtime->tokens[ctx->token_slot];
        rc = (t->used && t->owner == ctx && t->generation == ctx->token_generation &&
              t->pending && ctx->state == IOS27_21E_CANCEL_ENABLE) ? ECANCELED : 0;
    }
    unlock_runtime(runtime);
    return rc;
}
int ios27_21e_cleanup_push(ios27_21e_runtime *runtime,
                           ios27_21e_callback handler, void *argument,
                           uint64_t *scope) {
    if (!runtime || !handler || !scope) return EINVAL;
    int err = ios27_21e_attach(runtime);
    if (err) return err;
    err = lock_runtime(runtime);
    if (err) return err;
    struct context *ctx = current(runtime);
    int rc = EBUSY;
    if (!ctx->tearing_down && !ctx->draining_cleanup &&
        !ctx->active_cleanup_callbacks &&
        ctx->depth < IOS27_21E_MAX_CLEANUPS) {
        uint64_t id = 0;
        rc = mint_scope_id(&id);
        if (!rc) {
            ctx->stack[ctx->depth++] = (struct cleanup){id, handler, argument};
            *scope = id;
        }
    } else if (ctx->tearing_down || ctx->draining_cleanup ||
               ctx->active_cleanup_callbacks) {
        rc = EBUSY;
    } else if (ctx->depth >= IOS27_21E_MAX_CLEANUPS) {
        rc = EAGAIN;
    }
    unlock_runtime(runtime);
    return rc;
}
int ios27_21e_cleanup_pop(ios27_21e_runtime *runtime, uint64_t scope,
                          int execute) {
    if (!runtime || !scope) return EINVAL;
    int err = lock_runtime(runtime);
    if (err) return err;
    struct context *ctx = current(runtime);
    struct cleanup item;
    int rc = EINVAL;
    if (ctx && !ctx->tearing_down && ctx->depth &&
        ctx->stack[ctx->depth - 1].scope == scope) {
        item = ctx->stack[--ctx->depth];
        if (execute) ++ctx->active_cleanup_callbacks;
        rc = 0;
    }
    unlock_runtime(runtime);
    if (!rc && execute) {
        item.handler(item.argument);
        err = lock_runtime(runtime);
        if (err) return err;
        --ctx->active_cleanup_callbacks;
        ++runtime->completed_cleanup;
        unlock_runtime(runtime);
    }
    return rc;
}
int ios27_21e_cleanup_unwind(ios27_21e_runtime *runtime) {
    if (!runtime) return EINVAL;
    struct context *ctx = current(runtime);
    if (!ctx) return EINVAL;
    return run_cleanup(ctx);
}
int ios27_21e_stats(ios27_21e_runtime *runtime, size_t *attached,
                     size_t *keys, size_t *tokens, uint64_t *cleanups,
                     uint64_t *destructors) {
    if (!runtime) return EINVAL;
    int err = lock_runtime(runtime);
    if (err) return err;
    size_t key_count = 0, token_count = 0;
    for (size_t i = 0; i < IOS27_21E_MAX_KEYS; ++i) key_count += (size_t)runtime->keys[i].used;
    for (size_t i = 0; i < IOS27_21E_MAX_CANCEL; ++i) token_count += (size_t)runtime->tokens[i].used;
    if (attached) *attached = runtime->attached;
    if (keys) *keys = key_count;
    if (tokens) *tokens = token_count;
    if (cleanups) *cleanups = runtime->completed_cleanup;
    if (destructors) *destructors = runtime->completed_destructors;
    unlock_runtime(runtime);
    return 0;
}
