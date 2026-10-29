#include "ios27/phase22c_graph.h"
#include "ios27/dyld_paths.h"
#include "ios27/binds.h"
#include "ios27/chained.h"
#include "ios27/macho.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NO_INDEX SIZE_MAX
#define MH_DYLIB 6u
#define LC_DYLD_INFO 0x22u
#define LC_DYLD_INFO_ONLY 0x80000022u
#define LC_DYLD_CHAINED_FIXUPS 0x80000034u

typedef struct {
    uint64_t id;
    size_t index;
} lease_slot;

struct ios27_22c_graph {
    size_t count, order_count, started;
    uint64_t nonce, serial;
    ios27_image_spec specs[IOS27_22C_MAX_IMAGES];
    char names[IOS27_22C_MAX_IMAGES][IOS27_IMAGE_NAME_MAX];
    char dep_names[IOS27_22C_MAX_IMAGES][IOS27_22C_MAX_IMAGES][IOS27_DYLD_PATH_MAX];
    char guest_paths[IOS27_22C_MAX_IMAGES][IOS27_DYLD_PATH_MAX];
    const char *guest_path_ptrs[IOS27_22C_MAX_IMAGES];
    char main_executable[IOS27_DYLD_PATH_MAX];
    unsigned using_paths;
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
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

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
static int valid_dep_name(const char *s) {
    if (!s) return 0;
    if (valid_name(s)) return 1;
    if (s[0] != '/' && s[0] != '@') return 0;
    for (size_t i = 0; i < IOS27_DYLD_PATH_MAX; ++i) {
        unsigned char ch = (unsigned char)s[i];
        if (!ch) return i > 0u;
        if (ch <= 0x20u || ch >= 0x7fu || ch == '\\') return 0;
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

typedef struct {
    ios27_dyld_image_metadata meta;
    char search[IOS27_DYLD_MAX_CANDIDATES][IOS27_DYLD_PATH_MAX];
    size_t count;
    unsigned queued;
} path_state;
static int add_runpath(path_state *state, const char *value) {
    for (size_t j = 0; j < state->count; ++j)
        if (!strcmp(state->search[j], value)) return 0;
    if (state->count == IOS27_DYLD_MAX_CANDIDATES) return -1;
    (void)snprintf(state->search[state->count++],
                   IOS27_DYLD_PATH_MAX, "%s", value);
    return 0;
}
static int resolve_catalog_paths(ios27_22c_graph *g, const char *const *paths,
                                 const char *main, ios27_22c_diagnostic *diag) {
    path_state *states = calloc(g->count, sizeof(*states));
    size_t queue[IOS27_22C_MAX_IMAGES], nqueue = 0u, pos = 0u;
    size_t main_index = NO_INDEX;
    char error[IOS27_22C_DIAG_SIZE];
    int result = -1;
    if (!states) return report(diag, IOS27_22C_INPUT, NO_INDEX, NO_INDEX,
                                "runpath state allocation failed");
    for (size_t i = 0; i < g->count; ++i) {
        ios27_dyld_candidates candidate;
        if (!paths[i] ||
            ios27_dyld_paths(paths[i], paths[i], main, NULL, 0u,
                             &candidate, error, sizeof(error)) ||
            candidate.count != 1u || strcmp(candidate.paths[0], paths[i])) {
            (void)report(diag, IOS27_22C_DEPENDENCY, i, NO_INDEX,
                         "noncanonical or missing guest image path");
            goto done;
        }
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(paths[j], paths[i])) {
                (void)report(diag, IOS27_22C_DEPENDENCY, i, j,
                             "ambiguous duplicate guest image path");
                goto done;
            }
        (void)snprintf(g->guest_paths[i], IOS27_DYLD_PATH_MAX, "%s", paths[i]);
        g->guest_path_ptrs[i] = g->guest_paths[i];
        if (!strcmp(paths[i], main)) main_index = i;
        if (ios27_dyld_inspect_image(g->specs[i].file, g->specs[i].size,
                                     &states[i].meta, error, sizeof(error))) {
            (void)report(diag, IOS27_22C_MACHO, i, NO_INDEX, error);
            goto done;
        }
        for (size_t r = 0; r < states[i].meta.rpath_count; ++r) {
            if (ios27_dyld_paths(states[i].meta.rpaths[r], paths[i], main,
                                 NULL, 0u, &candidate, error, sizeof(error)) ||
                candidate.count != 1u ||
                add_runpath(&states[i], candidate.paths[0])) {
                (void)report(diag, IOS27_22C_DEPENDENCY, i, r,
                             "malformed or excessive guest runpath");
                goto done;
            }
        }
    }
    if (main_index != NO_INDEX) {
        states[main_index].queued = 1u;
        queue[nqueue++] = main_index;
    }
    for (size_t root = 0; root <= g->count; ++root) {
        if (root && !states[root - 1u].queued) {
            states[root - 1u].queued = 1u;
            queue[nqueue++] = root - 1u;
        }
        while (pos < nqueue) {
            size_t parent = queue[pos++];
            path_state *s = &states[parent];
            ios27_dyld_image_metadata meta = s->meta;
            meta.rpath_count = s->count;
            for (size_t r = 0; r < s->count; ++r)
                (void)snprintf(meta.rpaths[r], IOS27_DYLD_PATH_MAX,
                               "%s", s->search[r]);
            for (size_t k = 0; k < g->specs[parent].dependency_count; ++k) {
                ios27_dyld_catalog_match found;
                int rc = ios27_dyld_match_dependency(
                    &meta, k, paths[parent], main, g->guest_path_ptrs,
                    g->count, &found, error, sizeof(error));
                if (rc != 0 || found.image_index == parent) {
                    (void)report(diag, IOS27_22C_DEPENDENCY, parent, k,
                                 rc < 0 ? error :
                                 "dependency absent, ambiguous or self-referential");
                    goto done;
                }
                g->dependencies[parent][k] = found.image_index;
                if (!states[found.image_index].queued) {
                    path_state *child = &states[found.image_index];
                    for (size_t r = 0; r < s->count; ++r)
                        if (add_runpath(child, s->search[r])) {
                            (void)report(diag, IOS27_22C_DEPENDENCY, parent, k,
                                         "transitive runpath quota exceeded");
                            goto done;
                        }
                    child->queued = 1u;
                    queue[nqueue++] = found.image_index;
                }
            }
        }
    }
    result = 0;
done:
    free(states);
    return result;
}

static void release_tables(ios27_22c_graph *g) {
    for (size_t i = 0; i < g->count; ++i) {
        free(g->exports[i]);
        g->exports[i] = NULL;
    }
}
static int create_impl(const ios27_image_spec *specs, size_t count,
                       const char *const *guest_paths,
                       const char *main_executable,
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
    g->using_paths = guest_paths != NULL;
    if (!!guest_paths != !!main_executable ||
        (main_executable && strlen(main_executable) >= IOS27_DYLD_PATH_MAX)) {
        (void)report(diag, IOS27_22C_INPUT, NO_INDEX, NO_INDEX,
                     "guest catalog and main path must be supplied together");
        goto failed;
    }
    if (main_executable)
        (void)snprintf(g->main_executable, sizeof(g->main_executable),
                       "%s", main_executable);
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
            if (!valid_dep_name(specs[i].dependencies[k]) ||
                (!guest_paths && !valid_name(specs[i].dependencies[k]))) {
                (void)report(diag, IOS27_22C_INPUT, i, k,
                             "invalid dependency install name");
                goto failed;
            }
            (void)snprintf(g->dep_names[i][k], IOS27_DYLD_PATH_MAX,
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
        if (g->machos[i].encrypted || specs[i].size > (16u * 1024u * 1024u)) {
            (void)report(diag, IOS27_22C_MACHO, i, NO_INDEX,
                         "encrypted or oversized linked image unsupported");
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
    if (guest_paths) {
        if (resolve_catalog_paths(g, guest_paths, main_executable, diag))
            goto failed;
    } else for (size_t i = 0; i < count; ++i) {
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
int ios27_22c_create(const ios27_image_spec *specs, size_t count,
                      ios27_22c_graph **out, ios27_22c_diagnostic *diag) {
    return create_impl(specs, count, NULL, NULL, out, diag);
}
int ios27_22c_create_with_paths(const ios27_image_spec *specs, size_t count,
                                 const char *const *paths, const char *main,
                                 ios27_22c_graph **out, ios27_22c_diagnostic *diag) {
    return create_impl(specs, count, paths, main, out, diag);
}
int ios27_22c_order(const ios27_22c_graph *g, size_t pos,
                     int reverse, const char **name) {
    if (!g || !name || pos >= g->order_count) return -1;
    *name = g->names[g->order[reverse ? g->order_count - 1u - pos : pos]];
    return 0;
}

/* Read-only fixup diagnostics. Existing src/binds.c and src/chained.c
 * validate the bytecode and relocation footprint, while this gate
 * identifies the exact importing image and missing symbol before mmap. */
static int preflight_fixups(const ios27_22c_graph *g,
                           ios27_22c_diagnostic *diag) {
    for (size_t i = 0; i < g->count; ++i) {
        const uint8_t *file = g->specs[i].file;
        const size_t length = g->specs[i].size;
        size_t cursor = 32u;
        unsigned classic = 0u, chained = 0u;
        char error[IOS27_22C_DIAG_SIZE] = {0};
        for (uint32_t cmd = 0u; cmd < u32(file + 16u); ++cmd) {
            uint32_t opcode = u32(file + cursor);
            if (opcode == LC_DYLD_INFO || opcode == LC_DYLD_INFO_ONLY) ++classic;
            if (opcode == LC_DYLD_CHAINED_FIXUPS) ++chained;
            cursor += u32(file + cursor + 4u);
        }
        if (classic > 1u || chained > 1u || (classic && chained))
            return report(diag, IOS27_22C_FIXUP, i, NO_INDEX,
                          "duplicate or mixed classic/chained fixup commands");
        if (chained) {
            ios27_chained_plan *plan = calloc(1u, sizeof(*plan));
            if (!plan) return report(diag, IOS27_22C_INPUT, i, NO_INDEX,
                                     "chained plan allocation failed");
            int parsed = ios27_chained_parse(file, length, &g->machos[i],
                                             plan, error, sizeof(error));
            if (parsed) {
                free(plan);
                return report(diag, IOS27_22C_FIXUP, i, NO_INDEX, error);
            }
            for (size_t p = 0; p < plan->count; ++p) {
                if (!plan->patches[p].is_bind) continue;
                if (plan->patches[p].import_index >= plan->import_count) {
                    free(plan);
                    return report(diag, IOS27_22C_FIXUP, i, p,
                                  "chained relocation import outside table");
                }
                const ios27_chained_import *import =
                    &plan->imports[plan->patches[p].import_index];
                ios27_export_resolution symbol;
                ios27_22c_diagnostic detail;
                int resolved = ios27_22c_resolve(g, i, import->ordinal,
                                                  import->symbol, &symbol, &detail);
                if (resolved < 0 || (resolved > 0 && !import->weak_import &&
                                      import->ordinal != -3)) {
                    char message[IOS27_22C_DIAG_SIZE];
                    (void)snprintf(message, sizeof(message),
                                   "image %zu chained import %.96s: %.64s", i,
                                   import->symbol, resolved < 0 ? detail.message :
                                   "unresolved mandatory symbol");
                    free(plan);
                    return report(diag, IOS27_22C_FIXUP, i, p, message);
                }
            }
            free(plan);
        } else if (classic) {
            ios27_bind_plan *plan = calloc(1u, sizeof(*plan));
            if (!plan) return report(diag, IOS27_22C_INPUT, i, NO_INDEX,
                                     "classic plan allocation failed");
            int parsed = ios27_binds_parse(file, length, &g->machos[i],
                                           plan, error, sizeof(error));
            if (parsed) {
                free(plan);
                return report(diag, IOS27_22C_FIXUP, i, NO_INDEX, error);
            }
            for (size_t p = 0; p < plan->count; ++p) {
                ios27_export_resolution symbol;
                ios27_22c_diagnostic detail;
                int resolved = ios27_22c_resolve(g, i,
                               plan->entries[p].ordinal, plan->entries[p].symbol,
                               &symbol, &detail);
                if (resolved != 0) {
                    char message[IOS27_22C_DIAG_SIZE];
                    (void)snprintf(message, sizeof(message),
                                   "image %zu classic import %.96s: %.64s", i,
                                   plan->entries[p].symbol,
                                   resolved < 0 ? detail.message :
                                   "unresolved mandatory symbol");
                    free(plan);
                    return report(diag, IOS27_22C_FIXUP, i, p, message);
                }
            }
            free(plan);
        }
    }
    return 0;
}

int ios27_22c_load(ios27_22c_graph *g, ios27_22c_diagnostic *diag) {
    char error[IOS27_22C_DIAG_SIZE] = {0};
    clear_diag(diag);
    if (!g || g->loaded) return report(diag, IOS27_22C_LIFECYCLE,
                                       NO_INDEX, NO_INDEX,
                                       "graph absent or already loaded");
    if (preflight_fixups(g, diag)) return -1;
    int rc = g->using_paths ?
        ios27_images_load_with_paths(g->specs, g->count, g->guest_path_ptrs,
                                      g->main_executable, &g->loaded,
                                      error, sizeof(error)) :
        ios27_images_load(g->specs, g->count, &g->loaded,
                          error, sizeof(error));
    if (rc) {
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
