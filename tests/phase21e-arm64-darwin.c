/* Compiler-generated SDK-free ARM64 Mach-O callsite fixture. This is not a
 * runnable iOS process and does not assert Darwin pthread ABI support. */
#include "ios27/libsystem21e_tls.h"

struct state {
    ios27_21e_runtime *runtime;
    ios27_21e_handle key;
    ios27_21e_handle token;
    uint64_t scope;
};
static void finalize(void *context) {
    (void)context;
}
int phase21e_darwin_compiled_client(struct state *s, void *owned) {
    int status = ios27_21e_attach(s->runtime);
    if (status) return status;
    status = ios27_21e_key_create(s->runtime, finalize, &s->key);
    if (status) return status;
    status = ios27_21e_setspecific(s->runtime, s->key, owned);
    if (status) return status;
    status = ios27_21e_cancel_register(s->runtime, &s->token);
    if (status) return status;
    status = ios27_21e_cleanup_push(s->runtime, finalize, owned, &s->scope);
    if (status) return status;
    status = ios27_21e_cancel_checkpoint(s->runtime);
    if (status) return status;
    return ios27_21e_cleanup_pop(s->runtime, s->scope, 0);
}
