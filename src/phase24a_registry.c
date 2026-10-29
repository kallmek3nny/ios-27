#include "ios27/phase24a_registry.h"

#include "ios27/dyld_paths.h"
#include "ios27/exports.h"
#include "ios27/macho.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t generation;
    uint8_t active;
    uint8_t root;
} phase24a_handle_slot;

typedef struct {
    uint32_t generation;
    uint8_t active;
} phase24a_pin_slot;

struct ios27_phase24a_registry {
    pthread_mutex_t mutex;
    ios27_image_spec specs[IOS27_IMAGES_MAX];
    char paths[IOS27_IMAGES_MAX][IOS27_DYLD_PATH_MAX];
    char main_path[IOS27_DYLD_PATH_MAX];
    ios27_dyld_image_metadata *metadata[IOS27_IMAGES_MAX];
    ios27_exports *exports[IOS27_IMAGES_MAX];
    uint8_t reachable[IOS27_IMAGES_MAX];
    uint64_t refs[IOS27_IMAGES_MAX];
    phase24a_handle_slot handles[IOS27_PHASE24A_MAX_HANDLES];
    phase24a_pin_slot pins[IOS27_PHASE24A_MAX_PINS];
    ios27_images *mapped;
    size_t count;
    ios27_phase24a_stats stats;
};

static int error_code(char *error, size_t cap, int code, const char *why) {
    if (error && cap) (void)snprintf(error, cap, "%s", why);
    return code;
}

static int terminated(const char *s, size_t bound) {
    if (!s) return 0;
    for (size_t i = 0u; i < bound; ++i)
        if (s[i] == '\0') return i != 0u;
    return 0;
}

static int find_path(const ios27_phase24a_registry *r, const char *path) {
    for (size_t i = 0u; i < r->count; ++i)
        if (strcmp(r->paths[i], path) == 0) return (int)i;
    return -1;
}

static void cleanup(ios27_phase24a_registry *r) {
    if (!r) return;
    ios27_images_unload(r->mapped);
    for (size_t i = 0u; i < IOS27_IMAGES_MAX; ++i) {
        free(r->metadata[i]);
        free(r->exports[i]);
    }
    (void)pthread_mutex_destroy(&r->mutex);
    free(r);
}

static int find_dep(ios27_phase24a_registry *r, size_t image, size_t dep,
                    char *error, size_t cap) {
    ios27_dyld_candidates paths;
    const ios27_dyld_image_metadata *meta = r->metadata[image];
    const char *runpaths[IOS27_DYLD_MAX_CANDIDATES];
    if (dep >= meta->dependency_count || meta->rpath_count > IOS27_DYLD_MAX_CANDIDATES)
        return error_code(error, cap, -1, "invalid dependency count");
    for (size_t i = 0u; i < meta->rpath_count; ++i)
        runpaths[i] = meta->rpaths[i];
    if (ios27_dyld_paths(meta->dependencies[dep].install_name,
                         r->paths[image], r->main_path, runpaths,
                         meta->rpath_count, &paths, error, cap))
        return -1;
    /* Deterministic precedence: first existing guest-catalog match. */
    for (size_t i = 0u; i < paths.count; ++i) {
        int index = find_path(r, paths.paths[i]);
        if (index >= 0) return index;
    }
    if (meta->dependencies[dep].command == IOS27_DYLD_LC_LOAD_WEAK_DYLIB)
        return -2;
    return error_code(error, cap, -1, "mandatory guest dependency missing");
}

/* Fail closed on cycles, ordinal mismatch, and edges to absent dependencies.
 * The backend independently checks the same graph before mapping.
 */
static int visit(ios27_phase24a_registry *r, size_t root, size_t at,
                 uint8_t *colors, uint8_t *mask,
                 char *error, size_t cap) {
    const ios27_dyld_image_metadata *meta = r->metadata[at];
    if (colors[at] == 1u)
        return error_code(error, cap, -1, "recursive dependency graph");
    if (colors[at] == 2u) return 0;
    colors[at] = 1u;
    *mask |= (uint8_t)(1u << at);
    for (size_t d = 0u; d < meta->dependency_count; ++d) {
        int idx = find_dep(r, at, d, error, cap);
        if (idx == -2) continue;
        if (idx < 0 || (size_t)idx == at ||
            visit(r, root, (size_t)idx, colors, mask, error, cap))
            return -1;
    }
    (void)root;
    colors[at] = 2u;
    return 0;
}

int ios27_phase24a_create(const ios27_image_spec *specs,
                           const char *const *guest_paths, size_t count,
                           const char *main_executable,
                           ios27_phase24a_registry **out,
                           char *error, size_t cap) {
    ios27_phase24a_registry *r;
    ios27_dyld_candidates normalized;
    if (out) *out = NULL;
    if (!out || !specs || !guest_paths || !main_executable ||
        !count || count > IOS27_IMAGES_MAX ||
        !terminated(main_executable, IOS27_DYLD_PATH_MAX))
        return error_code(error, cap, IOS27_PHASE24A_INVALID, "invalid catalog");
    r = calloc(1u, sizeof(*r));
    if (!r) return error_code(error, cap, IOS27_PHASE24A_LIMIT, "allocation failed");
    if (pthread_mutex_init(&r->mutex, NULL)) {
        free(r);
        return error_code(error, cap, IOS27_PHASE24A_LIMIT, "mutex allocation failed");
    }
    r->count = count;
    if (ios27_dyld_paths(main_executable, main_executable, main_executable,
                         NULL, 0u, &normalized, error, cap) ||
        normalized.count != 1u ||
        strcmp(normalized.paths[0], main_executable) != 0) goto invalid;
    (void)snprintf(r->main_path, sizeof(r->main_path), "%s", main_executable);
    for (size_t i = 0u; i < count; ++i) {
        if (!terminated(guest_paths[i], IOS27_DYLD_PATH_MAX) ||
            ios27_dyld_paths(guest_paths[i], main_executable, main_executable,
                             NULL, 0u, &normalized, error, cap) ||
            normalized.count != 1u ||
            strcmp(normalized.paths[0], guest_paths[i]) != 0) goto invalid;
        (void)snprintf(r->paths[i], sizeof(r->paths[i]), "%s", guest_paths[i]);
        for (size_t j = 0u; j < i; ++j)
            if (!strcmp(r->paths[i], r->paths[j])) goto invalid;
        if (!specs[i].file || specs[i].size < 32u ||
            !terminated(specs[i].name, IOS27_IMAGE_NAME_MAX) ||
            specs[i].dependency_count > IOS27_IMAGES_MAX ||
            (specs[i].dependency_count && !specs[i].dependencies)) goto invalid;
        r->specs[i] = specs[i];
        r->metadata[i] = calloc(1u, sizeof(*r->metadata[i]));
        r->exports[i] = calloc(1u, sizeof(*r->exports[i]));
        if (!r->metadata[i] || !r->exports[i]) goto invalid;
        if (ios27_dyld_inspect_image(specs[i].file, specs[i].size,
                                     r->metadata[i], error, cap) ||
            ios27_exports_parse(specs[i].file, specs[i].size,
                                r->exports[i], error, cap) ||
            r->metadata[i]->dependency_count != specs[i].dependency_count)
            goto invalid;
        for (size_t j = 0u; j < specs[i].dependency_count; ++j)
            if (!terminated(specs[i].dependencies[j], IOS27_DYLD_PATH_MAX) ||
                strcmp(specs[i].dependencies[j],
                       r->metadata[i]->dependencies[j].install_name))
                goto invalid;
        for (size_t j = 0u; j < i; ++j)
            if (!strcmp(r->specs[j].name, specs[i].name)) goto invalid;
    }
    for (size_t i = 0u; i < count; ++i) {
        uint8_t colors[IOS27_IMAGES_MAX] = {0};
        if (visit(r, i, i, colors, &r->reachable[i], error, cap)) goto invalid;
    }
    if (error && cap) error[0] = '\0';
    *out = r;
    return IOS27_PHASE24A_OK;
invalid:
    cleanup(r);
    return error_code(error, cap, IOS27_PHASE24A_INVALID,
                      "invalid guest paths, exports or dependency graph");
}

void ios27_phase24a_destroy(ios27_phase24a_registry *r) {
    if (r) cleanup(r); /* Caller guarantees quiescence and no active guest calls. */
}

static void retire_if_unused(ios27_phase24a_registry *r) {
    if (r->mapped && !r->stats.live_handles && !r->stats.active_pins) {
        ios27_images_unload(r->mapped);
        r->mapped = NULL;
        ++r->stats.graph_unmaps;
    }
}

int ios27_phase24a_open(ios27_phase24a_registry *r,
                        const char *path, ios27_phase24a_handle *out,
                        char *error, size_t cap) {
    int root, slot = -1;
    if (out) *out = 0u;
    if (!r || !out || !terminated(path, IOS27_DYLD_PATH_MAX))
        return error_code(error, cap, IOS27_PHASE24A_INVALID, "invalid open arguments");
    (void)pthread_mutex_lock(&r->mutex);
    root = find_path(r, path);
    if (root < 0) goto absent;
    for (size_t i = 0u; i < IOS27_PHASE24A_MAX_HANDLES; ++i)
        if (!r->handles[i].active && r->handles[i].generation != UINT32_MAX) {
            slot = (int)i;
            break;
        }
    if (slot < 0) {
        ++r->stats.failures;
        (void)pthread_mutex_unlock(&r->mutex);
        return error_code(error, cap, IOS27_PHASE24A_LIMIT, "handle table full");
    }
    if (!r->mapped) {
        const char *paths[IOS27_IMAGES_MAX];
        ios27_images *mapped = NULL;
        for (size_t i = 0u; i < r->count; ++i) paths[i] = r->paths[i];
        if (ios27_images_load_with_paths(r->specs, r->count, paths,
                                          r->main_path, &mapped, error, cap)) {
            ++r->stats.failures;
            (void)pthread_mutex_unlock(&r->mutex);
            return IOS27_PHASE24A_MAP_FAILED;
        }
        r->mapped = mapped;
        ++r->stats.graph_maps;
    }
    phase24a_handle_slot *h = &r->handles[(size_t)slot];
    if (!h->generation) h->generation = 1u;
    h->active = 1u;
    h->root = (uint8_t)root;
    for (size_t i = 0u; i < r->count; ++i)
        if (r->reachable[(size_t)root] & (uint8_t)(1u << i)) ++r->refs[i];
    ++r->stats.live_handles;
    ++r->stats.opens;
    *out = ((uint64_t)h->generation << 32u) | ((uint64_t)slot + 1u);
    (void)pthread_mutex_unlock(&r->mutex);
    if (error && cap) error[0] = '\0';
    return IOS27_PHASE24A_OK;
absent:
    ++r->stats.failures;
    (void)pthread_mutex_unlock(&r->mutex);
    return error_code(error, cap, IOS27_PHASE24A_NOT_FOUND, "unknown guest image");
}

static phase24a_handle_slot *valid_handle(ios27_phase24a_registry *r,
                                          ios27_phase24a_handle handle) {
    uint32_t index = (uint32_t)handle;
    if (!index || index > IOS27_PHASE24A_MAX_HANDLES) return NULL;
    phase24a_handle_slot *h = &r->handles[index - 1u];
    if (!h->active || h->generation != (uint32_t)(handle >> 32u))
        return NULL;
    return h;
}

int ios27_phase24a_close(ios27_phase24a_registry *r,
                         ios27_phase24a_handle handle,
                         char *error, size_t cap) {
    phase24a_handle_slot *h;
    if (!r) return error_code(error, cap, IOS27_PHASE24A_INVALID, "null registry");
    (void)pthread_mutex_lock(&r->mutex);
    h = valid_handle(r, handle);
    if (!h) {
        ++r->stats.failures;
        (void)pthread_mutex_unlock(&r->mutex);
        return error_code(error, cap, IOS27_PHASE24A_INVALID, "stale or invalid handle");
    }
    for (size_t i = 0u; i < r->count; ++i)
        if (r->reachable[h->root] & (uint8_t)(1u << i)) --r->refs[i];
    h->active = 0u;
    ++h->generation;
    --r->stats.live_handles;
    ++r->stats.closes;
    retire_if_unused(r);
    (void)pthread_mutex_unlock(&r->mutex);
    if (error && cap) error[0] = '\0';
    return IOS27_PHASE24A_OK;
}

static int pin_address(ios27_phase24a_registry *r, uintptr_t address,
                       uintptr_t *out, ios27_phase24a_pin *pin,
                       char *error, size_t cap) {
    for (size_t i = 0u; i < IOS27_PHASE24A_MAX_PINS; ++i) {
        phase24a_pin_slot *p = &r->pins[i];
        if (!p->active && p->generation != UINT32_MAX) {
            if (!p->generation) p->generation = 1u;
            p->active = 1u;
            ++r->stats.active_pins;
            ++r->stats.lookups;
            *out = address;
            *pin = ((uint64_t)p->generation << 32u) | ((uint64_t)i + 1u);
            return IOS27_PHASE24A_OK;
        }
    }
    ++r->stats.failures;
    return error_code(error, cap, IOS27_PHASE24A_LIMIT, "pin table full");
}

int ios27_phase24a_symbol(ios27_phase24a_registry *r,
                          ios27_phase24a_handle handle, const char *symbol,
                          uintptr_t *out, ios27_phase24a_pin *pin,
                          char *error, size_t cap) {
    phase24a_handle_slot *h;
    uintptr_t address = 0u;
    int result;
    if (out) *out = 0u;
    if (pin) *pin = 0u;
    if (!r || !out || !pin || !terminated(symbol, IOS27_EXPORT_NAME_MAX))
        return error_code(error, cap, IOS27_PHASE24A_INVALID, "invalid lookup");
    (void)pthread_mutex_lock(&r->mutex);
    h = valid_handle(r, handle);
    if (!h || !r->mapped) {
        result = IOS27_PHASE24A_INVALID;
    } else if (ios27_images_symbol(r->mapped, r->specs[h->root].name,
                                   symbol, &address)) {
        result = IOS27_PHASE24A_NOT_FOUND;
    } else {
        result = pin_address(r, address, out, pin, error, cap);
    }
    if (result != IOS27_PHASE24A_OK) ++r->stats.failures;
    (void)pthread_mutex_unlock(&r->mutex);
    if (result != IOS27_PHASE24A_OK)
        return error_code(error, cap, result, "symbol not found or handle invalid");
    if (error && cap) error[0] = '\0';
    return result;
}

int ios27_phase24a_flat_symbol(ios27_phase24a_registry *r,
                               const char *symbol, uintptr_t *out,
                               ios27_phase24a_pin *pin,
                               char *error, size_t cap) {
    uintptr_t selected = 0u;
    int found = 0, result = IOS27_PHASE24A_NOT_FOUND;
    if (out) *out = 0u;
    if (pin) *pin = 0u;
    if (!r || !out || !pin || !terminated(symbol, IOS27_EXPORT_NAME_MAX))
        return error_code(error, cap, IOS27_PHASE24A_INVALID, "invalid flat lookup");
    (void)pthread_mutex_lock(&r->mutex);
    if (r->mapped) for (size_t i = 0u; i < r->count; ++i) {
        const ios27_export *entry;
        uintptr_t address = 0u;
        int is_weak;
        if (!r->refs[i] ||
            ios27_images_symbol(r->mapped, r->specs[i].name, symbol, &address))
            continue;
        entry = ios27_exports_find(r->exports[i], symbol);
        is_weak = entry && (entry->flags & IOS27_EXPORT_WEAK_DEFINITION) ? 1 : 0;
        if (!found || (found == 1 && !is_weak)) {
            selected = address;
            found = is_weak ? 1 : 2;
        }
        if (found == 2) break; /* Strong, earliest catalog order wins. */
    }
    if (found) result = pin_address(r, selected, out, pin, error, cap);
    if (result != IOS27_PHASE24A_OK) ++r->stats.failures;
    (void)pthread_mutex_unlock(&r->mutex);
    if (result != IOS27_PHASE24A_OK)
        return error_code(error, cap, result, "flat symbol unavailable");
    if (error && cap) error[0] = '\0';
    return result;
}

int ios27_phase24a_release(ios27_phase24a_registry *r,
                           ios27_phase24a_pin token,
                           char *error, size_t cap) {
    uint32_t index = (uint32_t)token;
    if (!r || !index || index > IOS27_PHASE24A_MAX_PINS)
        return error_code(error, cap, IOS27_PHASE24A_INVALID, "invalid pin");
    (void)pthread_mutex_lock(&r->mutex);
    phase24a_pin_slot *p = &r->pins[index - 1u];
    if (!p->active || p->generation != (uint32_t)(token >> 32u)) {
        ++r->stats.failures;
        (void)pthread_mutex_unlock(&r->mutex);
        return error_code(error, cap, IOS27_PHASE24A_INVALID, "stale pin");
    }
    p->active = 0u;
    ++p->generation;
    --r->stats.active_pins;
    retire_if_unused(r);
    (void)pthread_mutex_unlock(&r->mutex);
    if (error && cap) error[0] = '\0';
    return IOS27_PHASE24A_OK;
}

int ios27_phase24a_refs(ios27_phase24a_registry *r,
                        const char *path, uint64_t *out) {
    int index;
    if (!r || !out || !terminated(path, IOS27_DYLD_PATH_MAX)) return -1;
    (void)pthread_mutex_lock(&r->mutex);
    index = find_path(r, path);
    if (index >= 0) *out = r->refs[(size_t)index];
    (void)pthread_mutex_unlock(&r->mutex);
    return index >= 0 ? 0 : -1;
}

int ios27_phase24a_stats_read(ios27_phase24a_registry *r,
                             ios27_phase24a_stats *out) {
    if (!r || !out) return -1;
    (void)pthread_mutex_lock(&r->mutex);
    *out = r->stats;
    (void)pthread_mutex_unlock(&r->mutex);
    return 0;
}
