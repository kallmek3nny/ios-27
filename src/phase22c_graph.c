#include "ios27/phase22c_graph.h"
#include "ios27/dyld_paths.h"
#include "ios27/macho.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NO_INDEX SIZE_MAX
#define MH_DYLIB 6u

typedef struct {
    uint64_t id;
    size_t index;
} lease_slot;

struct ios27_22c_graph {
    size_t count, order_count, started;
    uint64_t nonce, serial;
    ios27_image_spec specs[IOS27_22C_MAX_IMAGES];
    char names[IOS27_22C_MAX_IMAGES][IOS27_IMAGE_NAME_MAX];
    char dep_names[IOS27_22C_MAX_IMAGES][IOS27_22C_MAX_IMAGES][IOS27_IMAGE_NAME_MAX];
    const char *dep_ptrs[IOS27_22C_MAX_IMAGES][IOS27_22C_MAX_IMAGES];
    size_t dependencies[IOS27_22C_MAX_IMAGES][IOS27_22C_MAX_IMAGES];
    size_t order[IOS27_22C_MAX_IMAGES], refs[IOS27_22C_MAX_IMAGES];
    ios27_macho_info machos[IOS27_22C_MAX_IMAGES];
    ios27_exports *exports[IOS27_22C_MAX_IMAGES];
    lease_slot leases[IOS27_22C_MAX_LEASES];
    ios27_images *loaded;
    ios27_22c_fini_fn fini;
    void *fini_context;
};
static atomic_uint_fast64_t graph_epoch = ATOMIC_VAR_INIT(1u);

static int report(ios27_22c_diagnostic *d, ios27_22c_reason r,
                  size_t i, size_t dep, const char *detail) {
    if (d) {
        memset(d, 0, sizeof(*d));
        d->reason = r;
        d->image = i;
        d->dependency = dep;
        (void)snprintf(d->message, sizeof(d->message), "%s", detail);
    }
    return -1;
}
static void clear_diag(ios27_22c_diagnostic *d) {
    if (d) memset(d, 0, sizeof(*d));
}
static int valid_name(const char *s) {
    if (!s) return 0;
    for (size_t i = 0; i < IOS27_IMAGE_NAME_MAX; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (!c) return i > 0u;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return 0;
}
static int valid_symbol(const char *s) {
    if (!s) return 0;
    for (size_t i = 0; i < IOS27_EXPORT_NAME_MAX; ++i) {
        if (s[i] == '\0') return i != 0u;
    }
    return 0;
}
static size_t find_name(const ios27_22c_graph *g, const char *name) {
    for (size_t i = 0; i < g->count; ++i)
        if (strcmp(g->names[i], name) == 0) return i;
    return NO_INDEX;
}
static int visit(ios27_22c_graph *g, size_t i, unsigned char *colors,
                 ios27_22c_diagnostic *diag) {
    if (colors[i] == 1u)
        return report(diag, IOS27_22C_CYCLE, i, i, "dependency cycle detected");
    if (colors[i] == 2u) return 0;
    colors[i] = 1u;
    for (size_t k = 0; k < g->specs[i].dependency_count; ++k) {
        size_t child = g->dependencies[i][k];
        if (visit(g, child, colors, diag)) return -1;
    }
    colors[i] = 2u;
    g->order[g->order_count++] = i;
    return 0;
}
static void release_tables(ios27_22c_graph *g) {
    for (size_t i = 0; i < g->count; ++i) {
        free(g->exports[i]);
        g->exports[i] = NULL;
    }
}
int ios27_22c_create(const ios27_image_spec *specs, size_t count,
                      ios27_22c_graph **out, ios27_22c_diagnostic *diag) {
    ios27_22c_graph *g;
    unsigned char colors[IOS27_22C_MAX_IMAGES] = {0};
    char error[IOS27_22C_DIAG_SIZE];
    if (out) *out = NULL;
    clear_diag(diag);
    if (!out || !specs || count == 0u || count > IOS27_22C_MAX_IMAGES)
        return report(diag, IOS27_22C_INPUT, NO_INDEX, NO_INDEX,
                      "null catalog or image count outside bounded limit");
    g = calloc(1u, sizeof(*g));
    if (!g) return report(diag, IOS27_22C_INPUT, NO_INDEX, NO_INDEX,
                          "graph allocation failed");
    g->count = count;
    g->nonce = atomic_fetch_add_explicit(&graph_epoch, 1u, memory_order_relaxed);
    if (g->nonce == 0u || g->nonce > UINT32_MAX) {
        (void)report(diag, IOS27_22C_INPUT, NO_INDEX, NO_INDEX,
                     "graph generation exhausted");
        goto failed;
    }
    for (size_t i = 0; i < count; ++i) {
        ios27_dyld_image_metadata meta;
        if (!valid_name(specs[i].name) ||
            !specs[i].file || specs[i].size < 32u ||
            specs[i].dependency_count > count ||
            (specs[i].dependency_count && !specs[i].dependencies)) {
            (void)report(diag, IOS27_22C_INPUT, i, NO_INDEX,
                         "invalid name, file or dependency count");
            goto failed;
        }
        for (size_t j = 0; j < i; ++j)
            if (strcmp(specs[i].name, g->names[j]) == 0) {
                (void)report(diag, IOS27_22C_INPUT, i, j, "duplicate image name");
                goto failed;
            }
        (void)snprintf(g->names[i], sizeof(g->names[i]), "%s", specs[i].name);
        g->specs[i] = specs[i];
        g->specs[i].name = g->names[i];
        g->specs[i].dependencies = g->dep_ptrs[i];
        for (size_t k = 0; k < specs[i].dependency_count; ++k) {
            if (!valid_name(specs[i].dependencies[k])) {
                (void)report(diag, IOS27_22C_INPUT, i, k,
                             "invalid dependency install name");
                goto failed;
            }
            (void)snprintf(g->dep_names[i][k], IOS27_IMAGE_NAME_MAX,
                           "%s", specs[i].dependencies[k]);
            g->dep_ptrs[i][k] = g->dep_names[i][k];
            for (size_t p = 0; p < k; ++p)
                if (strcmp(g->dep_ptrs[i][k], g->dep_ptrs[i][p]) == 0) {
                    (void)report(diag, IOS27_22C_DEPENDENCY, i, k,
                                 "duplicate dependency ordinal");
                    goto failed;
                }
        }
        if (ios27_macho_parse(specs[i].file, specs[i].size,
                              &g->machos[i], error, sizeof(error))) {
            (void)report(diag, IOS27_22C_MACHO, i, NO_INDEX, error);
            goto failed;
        }
        if (g->machos[i].filetype != IOS27_MH_EXECUTE &&
            g->machos[i].filetype != MH_DYLIB) {
            (void)report(diag, IOS27_22C_MACHO, i, NO_INDEX,
                         "expected linked MH_EXECUTE or MH_DYLIB");
            goto failed;
        }
        if (ios27_dyld_inspect_image(specs[i].file, specs[i].size,
                                     &meta, error, sizeof(error))) {
            (void)report(diag, IOS27_22C_MACHO, i, NO_INDEX, error);
            goto failed;
        }
        if (meta.dependency_count != specs[i].dependency_count) {
            (void)report(diag, IOS27_22C_DEPENDENCY, i, NO_INDEX,
                         "LC_LOAD_DYLIB count differs from supplied catalog");
            goto failed;
        }
        for (size_t k = 0; k < meta.dependency_count; ++k) {
            if (strcmp(meta.dependencies[k].install_name, g->dep_names[i][k])) {
                (void)report(diag, IOS27_22C_DEPENDENCY, i, k,
                             "LC_LOAD_DYLIB ordinal/name mismatch");
                goto failed;
            }
        }
        g->exports[i] = calloc(1u, sizeof(*g->exports[i]));
        if (!g->exports[i]) {
            (void)report(diag, IOS27_22C_INPUT, i, NO_INDEX,
                         "export table allocation failed");
            goto failed;
        }
        if (ios27_exports_parse(specs[i].file, specs[i].size,
                                g->exports[i], error, sizeof(error))) {
            (void)report(diag, IOS27_22C_EXPORT, i, NO_INDEX, error);
            goto failed;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        for (size_t k = 0; k < g->specs[i].dependency_count; ++k) {
            size_t dep = find_name(g, g->dep_ptrs[i][k]);
            if (dep == NO_INDEX) {
                (void)report(diag, IOS27_22C_DEPENDENCY, i, k,
                             "required dependency absent from explicit catalog");
                goto failed;
            }
            g->dependencies[i][k] = dep;
        }
    }
    for (size_t i = 0; i < count; ++i)
        if (visit(g, i, colors, diag)) goto failed;
    *out = g;
    return 0;
failed:
    release_tables(g);
    free(g);
    return -1;
}
int ios27_22c_order(const ios27_22c_graph *g, size_t pos,
                     int reverse, const char **name) {
    if (!g || !name || pos >= g->order_count) return -1;
    *name = g->names[g->order[reverse ? g->order_count - 1u - pos : pos]];
    return 0;
}
int ios27_22c_load(ios27_22c_graph *g, ios27_22c_diagnostic *diag) {
    char error[IOS27_22C_DIAG_SIZE] = {0};
    clear_diag(diag);
    if (!g || g->loaded) return report(diag, IOS27_22C_LIFECYCLE,
                                       NO_INDEX, NO_INDEX,
                                       "graph absent or already loaded");
    if (ios27_images_load(g->specs, g->count, &g->loaded,
                          error, sizeof(error))) {
        g->loaded = NULL;
        return report(diag, IOS27_22C_FIXUP, NO_INDEX, NO_INDEX, error);
    }
    return 0;
}
static void graph_libraries(const ios27_22c_graph *g,
                            ios27_export_library *libs) {
    for (size_t i = 0; i < g->count; ++i) {
        libs[i].exports = g->exports[i];
        libs[i].dependencies = g->dependencies[i];
        libs[i].dependency_count = g->specs[i].dependency_count;
    }
}
int ios27_22c_resolve(const ios27_22c_graph *g, size_t importer,
                       int32_t ordinal, const char *symbol,
                       ios27_export_resolution *out,
                       ios27_22c_diagnostic *diag) {
    ios27_export_library libs[IOS27_22C_MAX_IMAGES];
    ios27_export_resolution result, weak = {0};
    size_t main_index = NO_INDEX, candidate = NO_INDEX;
    char error[IOS27_22C_DIAG_SIZE] = {0};
    int rc;
    if (out) memset(out, 0, sizeof(*out));
    clear_diag(diag);
    if (!g || !out || importer >= g->count ||
        !valid_symbol(symbol))
        return report(diag, IOS27_22C_INPUT, importer, NO_INDEX,
                      "invalid symbol, importer or output");
    graph_libraries(g, libs);
    if (ordinal > 0) {
        if ((size_t)ordinal > g->specs[importer].dependency_count)
            return report(diag, IOS27_22C_DEPENDENCY, importer,
                          (size_t)ordinal, "ordinal exceeds dependency list");
        candidate = g->dependencies[importer][(size_t)ordinal - 1u];
    } else if (ordinal == 0) candidate = importer;
    else {
        if (ordinal < -3)
            return report(diag, IOS27_22C_DEPENDENCY, importer, NO_INDEX,
                          "unsupported special library ordinal");
        for (size_t i = 0; i < g->count; ++i)
            if (g->machos[i].filetype == IOS27_MH_EXECUTE) {
                if (main_index != NO_INDEX)
                    return report(diag, IOS27_22C_DEPENDENCY, importer, i,
                                  "multiple main executables");
                main_index = i;
            }
        if (ordinal == -1) {
            if (main_index == NO_INDEX)
                return report(diag, IOS27_22C_DEPENDENCY, importer, NO_INDEX,
                              "main executable absent");
            candidate = main_index;
        }
    }
    if (candidate != NO_INDEX) {
        rc = ios27_exports_resolve(libs, g->count, candidate, symbol,
                                  &result, error, sizeof(error));
        if (rc < 0) return report(diag, IOS27_22C_EXPORT, candidate,
                                   NO_INDEX, error);
        if (rc == 0) *out = result;
        return rc;
    }
    /* Flat lookup is deterministic: main first, then catalog order.
     * A strong export outranks a weak definition without replacing an
     * earlier strong provider. No host dlsym fallback. */
    for (size_t attempt = 0; attempt < g->count; ++attempt) {
        size_t i = attempt;
        if (main_index != NO_INDEX) {
            if (attempt == 0u) i = main_index;
            else i = attempt <= main_index ? attempt - 1u : attempt;
        }
        rc = ios27_exports_resolve(libs, g->count, i, symbol,
                                   &result, error, sizeof(error));
        if (rc < 0) return report(diag, IOS27_22C_EXPORT, i,
                                   NO_INDEX, error);
        if (rc == 0) {
            if (!(result.symbol->flags & IOS27_EXPORT_WEAK_DEFINITION)) {
                *out = result;
                return 0;
            }
            if (!weak.symbol) weak = result;
        }
    }
    if (weak.symbol) {
        *out = weak;
        return 0;
    }
    return 1; /* unresolved weak or missing strong: caller decides */
}
static void closure(const ios27_22c_graph *g, size_t i, unsigned char *seen) {
    if (seen[i]) return;
    seen[i] = 1u;
    for (size_t k = 0; k < g->specs[i].dependency_count; ++k)
        closure(g, g->dependencies[i][k], seen);
}
int ios27_22c_acquire(ios27_22c_graph *g, size_t image,
                       ios27_22c_lease *lease, ios27_22c_diagnostic *diag) {
    unsigned char seen[IOS27_22C_MAX_IMAGES] = {0};
    size_t slot = NO_INDEX;
    clear_diag(diag);
    if (lease) lease->serial = 0u;
    if (!g || !lease || image >= g->count || !g->loaded)
        return report(diag, IOS27_22C_LIFECYCLE, image, NO_INDEX,
                      "graph not loaded or invalid image");
    for (size_t k = 0; k < IOS27_22C_MAX_LEASES; ++k)
        if (!g->leases[k].id) { slot = k; break; }
    if (slot == NO_INDEX || g->serial >= UINT32_MAX)
        return report(diag, IOS27_22C_BUSY, image, NO_INDEX,
                      "lease slots or generation exhausted");
    closure(g, image, seen);
    for (size_t i = 0; i < g->count; ++i)
        if (seen[i] && g->refs[i] >= IOS27_22C_MAX_LEASES)
            return report(diag, IOS27_22C_BUSY, image, i,
                          "transitive reference limit reached");
    for (size_t i = 0; i < g->count; ++i)
        if (seen[i]) ++g->refs[i];
    g->leases[slot].index = image;
    g->leases[slot].id = (g->nonce << 32u) | ++g->serial;
    lease->serial = g->leases[slot].id;
    return 0;
}
int ios27_22c_release(ios27_22c_graph *g, ios27_22c_lease lease) {
    unsigned char seen[IOS27_22C_MAX_IMAGES] = {0};
    if (!g || !lease.serial) return -1;
    for (size_t k = 0; k < IOS27_22C_MAX_LEASES; ++k)
        if (g->leases[k].id == lease.serial) {
            closure(g, g->leases[k].index, seen);
            for (size_t i = 0; i < g->count; ++i) {
                if (seen[i] && !g->refs[i]) return -1;
            }
            for (size_t i = 0; i < g->count; ++i)
                if (seen[i]) --g->refs[i];
            g->leases[k].id = 0u;
            return 0;
        }
    return -1;
}
size_t ios27_22c_pins(const ios27_22c_graph *g, size_t image) {
    return g && image < g->count ? g->refs[image] : 0u;
}
int ios27_22c_symbol(const ios27_22c_graph *g, ios27_22c_lease lease,
                      size_t provider, const char *name, uintptr_t *address) {
    unsigned char seen[IOS27_22C_MAX_IMAGES] = {0};
    if (!g || !g->loaded || !address || !name || provider >= g->count)
        return -1;
    for (size_t k = 0; k < IOS27_22C_MAX_LEASES; ++k)
        if (lease.serial && g->leases[k].id == lease.serial) {
            closure(g, g->leases[k].index, seen);
            if (!seen[provider]) return -1;
            return ios27_images_symbol(g->loaded, g->names[provider],
                                        name, address);
        }
    return -1;
}
int ios27_22c_initialize(ios27_22c_graph *g,
                          ios27_22c_init_fn init, ios27_22c_fini_fn fini,
                          void *context, ios27_22c_diagnostic *diag) {
    clear_diag(diag);
    if (!g || !g->loaded || !init || !fini || g->started)
        return report(diag, IOS27_22C_LIFECYCLE, NO_INDEX, NO_INDEX,
                      "initialization requires loaded, inactive graph and callbacks");
    g->fini = fini;
    g->fini_context = context;
    for (size_t k = 0; k < g->order_count; ++k) {
        size_t i = g->order[k];
        if (init(context, i)) {
            while (g->started) fini(context, g->order[--g->started]);
            g->fini = NULL;
            g->fini_context = NULL;
            return report(diag, IOS27_22C_LIFECYCLE, i, NO_INDEX,
                          "initializer rejected image; prior images finalized");
        }
        ++g->started;
    }
    return 0;
}
int ios27_22c_unload(ios27_22c_graph *g, ios27_22c_diagnostic *diag) {
    clear_diag(diag);
    if (!g) return report(diag, IOS27_22C_INPUT, NO_INDEX, NO_INDEX,
                          "null graph");
    for (size_t i = 0; i < g->count; ++i)
        if (g->refs[i]) return report(diag, IOS27_22C_BUSY, i, NO_INDEX,
                                      "cannot unload pinned dependency closure");
    while (g->started) {
        size_t i = g->order[--g->started];
        g->fini(g->fini_context, i);
    }
    g->fini = NULL;
    g->fini_context = NULL;
    ios27_images_unload(g->loaded);
    g->loaded = NULL;
    return 0;
}
void ios27_22c_destroy(ios27_22c_graph *g) {
    if (!g) return;
    /* Never unmap live leased code. Caller must release before destroying. */
    if (ios27_22c_unload(g, NULL)) return;
    release_tables(g);
    free(g);
}
