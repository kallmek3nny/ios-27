#include "ios27/phase22d_metadata.h"
#include "ios27/objc_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int reject(char *dst, size_t cap, const char *reason) {
    if (dst && cap) (void)snprintf(dst, cap, "%s", reason);
    return -1;
}
static int matches_signature(phase22d_signature s, const char *encoding) {
    /* Objective-C type strings are ABI declarations. We intentionally do not
     * guess the calling convention for C structs, vectors, long doubles,
     * unions, varargs or unknown encodings. Darwin LP64 only. */
    switch (s) {
    case PHASE22D_WORD0: return !strcmp(encoding, "Q16@0:8") ||
                               !strcmp(encoding, "q16@0:8") ||
                               !strcmp(encoding, "@16@0:8");
    case PHASE22D_WORD1: return !strcmp(encoding, "Q24@0:8Q16");
    case PHASE22D_WORD2: return !strcmp(encoding, "Q32@0:8Q16Q24");
    case PHASE22D_WORD3: return !strcmp(encoding, "Q40@0:8Q16Q24Q32");
    case PHASE22D_WORD4: return !strcmp(encoding, "Q48@0:8Q16Q24Q32Q40");
    case PHASE22D_DOUBLE1: return !strcmp(encoding, "d24@0:8d16");
    default: return 0;
    }
}
int phase22d_register_compiled(phase22d_runtime *r,
                              const uint8_t *bytes, size_t length,
                              const phase22d_class_spec *trusted,
                              size_t count, char *diagnostic, size_t cap) {
    ios27_objc_metadata *parsed = NULL;
    int ok = -1;
    if (diagnostic && cap) diagnostic[0] = 0;
    if (!r || !bytes || !length || !trusted ||
        !count || count > IOS27_OBJC_METADATA_CLASSES)
        return reject(diagnostic, cap, "invalid compiled ObjC2 registration input");
    parsed = calloc(1u, sizeof(*parsed));
    if (!parsed) return reject(diagnostic, cap, "metadata allocation failed");
    if (ios27_objc_metadata_inspect(bytes, length, parsed,
                                  diagnostic, cap)) goto done;
    if (parsed->class_count != count) {
        (void)reject(diagnostic, cap, "class count differs from compiled image");
        goto done;
    }
    for (size_t i = 0; i < count; ++i) {
        const phase22d_class_spec *binding = &trusted[i];
        const ios27_objc_metadata_class *found = NULL;
        if (!binding->name) {
            (void)reject(diagnostic, cap, "null class binding name"); goto done;
        }
        for (size_t j = 0; j < count; ++j)
            if (!strcmp(parsed->classes[j].name, binding->name)) {
                found = &parsed->classes[j]; break;
            }
        if (!found || found->instance_size != binding->instance_size ||
            (binding->superclass ?
             strcmp(found->superclass, binding->superclass) != 0 :
             found->superclass[0] != 0)) {
            (void)reject(diagnostic, cap, "class name/parent/size mismatch");
            goto done;
        }
        if (found->method_count != binding->instance_count ||
            (binding->instance_count && !binding->instance_methods)) {
            (void)reject(diagnostic, cap, "instance method count mismatch");
            goto done;
        }
        for (size_t k = 0; k < binding->instance_count; ++k) {
            const phase22d_method_spec *method = &binding->instance_methods[k];
            const ios27_objc_metadata_method *compiled = NULL;
            if (!method->selector) {
                (void)reject(diagnostic, cap, "null method selector"); goto done;
            }
            for (size_t j = 0; j < found->method_count; ++j)
                if (!strcmp(found->methods[j].selector, method->selector)) {
                    compiled = &found->methods[j]; break;
                }
            if (!compiled ||
                !matches_signature(method->signature, compiled->type_encoding)) {
                (void)reject(diagnostic, cap, "unsupported or mismatched method ABI");
                goto done;
            }
        }
    }
    if (phase22d_register(r, trusted, count)) {
        (void)reject(diagnostic, cap, "validated classes failed atomic registration");
        goto done;
    }
    ok = 0;
done:
    free(parsed);
    return ok;
}
