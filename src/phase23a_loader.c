#include "ios27/phase23a_loader.h"

#include "ios27/dyld_paths.h"
#include "ios27/exports.h"
#include "ios27/macho.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PHASE23A_MAX IOS27_IMAGES_MAX

typedef struct {
    ios27_images *mapping;
    uint32_t generation;
    uint32_t direct_refs;
    unsigned char closure[PHASE23A_MAX];
} phase23a_cohort;

struct ios27_phase23a_loader {
    pthread_mutex_t mutex;
    const ios27_image_spec *catalog;
    size_t count;
    ios27_exports *exports[PHASE23A_MAX];
    phase23a_cohort cohorts[PHASE23A_MAX];
    ios27_phase23a_diagnostics stats;
};

static int report(char *error, size_t cap, int result, const char *message) {
    if (error && cap) (void)snprintf(error, cap, "%s", message);
    return result;
}

static void clear_error(char *error, size_t cap) {
    if (error && cap) error[0] = '\0';
}

static int bounded_name(const char *name, size_t cap) {
    if (!name) return 0;
    for (size_t n = 0u; n < cap; ++n) {
        unsigned char c = (unsigned char)name[n];
        if (!c) return n > 0u;
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.'))
            return 0;
    }
    return 0;
}

static int index_of(const ios27_phase23a_loader *loader, const char *name) {
    if (!bounded_name(name, IOS27_IMAGE_NAME_MAX)) return -1;
    for (size_t i = 0u; i < loader->count; ++i)
        if (strcmp(name, loader->catalog[i].name) == 0)
            return (int)i;
    return -1;
}

/* Validate exact Mach-O dependency ordinals and form a strict DAG before
 * handing bytes to the existing transactional fixup/mapping loader. */
static int closure_visit(ios27_phase23a_loader *loader, size_t index,
                         unsigned char colors[PHASE23A_MAX],
                         unsigned char reach[PHASE23A_MAX],
                         char *error, size_t error_cap) {
    ios27_dyld_image_metadata *meta;
    const ios27_image_spec *spec = &loader->catalog[index];
    int result = IOS27_PHASE23A_INVALID;
    if (colors[index] == 1u)
        return report(error, error_cap, result, "recursive/cyclic dependency");
    if (colors[index] == 2u) return IOS27_PHASE23A_OK;
    colors[index] = 1u;
    meta = calloc(1u, sizeof(*meta));
    if (!meta)
        return report(error, error_cap, IOS27_PHASE23A_LIMIT,
                      "dependency metadata allocation failed");
    if (ios27_dyld_inspect_image(spec->file, spec->size, meta,
                                 error, error_cap)) goto done;
    if (meta->dependency_count != spec->dependency_count) {
        (void)report(error, error_cap, result, "dependency ordinal count mismatch");
        goto done;
    }
    for (size_t dep = 0u; dep < spec->dependency_count; ++dep) {
        const char *name = spec->dependencies[dep];
        int child;
        if (!bounded_name(name, IOS27_IMAGE_NAME_MAX) ||
            strcmp(meta->dependencies[dep].install_name, name) != 0) {
            (void)report(error, error_cap, result, "dependency install-name/order mismatch");
            goto done;
        }
        child = index_of(loader, name);
        if (child < 0) {
            if (meta->dependencies[dep].command != IOS27_DYLD_LC_LOAD_WEAK_DYLIB) {
                (void)report(error, error_cap, result, "missing mandatory dependency");
                goto done;
            }
            continue;
        }
        if (closure_visit(loader, (size_t)child, colors, reach,
                          error, error_cap)) goto done;
    }
    colors[index] = 2u;
    reach[index] = 1u;
    result = IOS27_PHASE23A_OK;
done:
    free(meta);
    return result;
}

static int handle_slot(ios27_phase23a_loader *loader,
                       ios27_phase23a_handle handle) {
    unsigned slot = (unsigned)(handle & UINT64_C(0xff));
    uint64_t generation = handle >> 8u;
    phase23a_cohort *cohort;
    if (slot == 0u || (size_t)slot > loader->count ||
        generation == 0u || generation > UINT32_MAX)
        return -1;
    cohort = &loader->cohorts[slot - 1u];
    if (!cohort->mapping || !cohort->direct_refs ||
        generation != cohort->generation) return -1;
    return (int)slot - 1;
}

int ios27_phase23a_create(const ios27_image_spec *catalog, size_t count,
                          ios27_phase23a_loader **out,
                          char *error, size_t error_cap) {
    ios27_phase23a_loader *loader;
    if (out) *out = NULL;
    if (!out || !catalog || count == 0u || count > PHASE23A_MAX)
        return report(error, error_cap, IOS27_PHASE23A_INVALID,
                      "invalid bounded image catalog");
    loader = calloc(1u, sizeof(*loader));
    if (!loader)
        return report(error, error_cap, IOS27_PHASE23A_LIMIT,
                      "loader allocation failed");
    if (pthread_mutex_init(&loader->mutex, NULL) != 0) {
        free(loader);
        return report(error, error_cap, IOS27_PHASE23A_LIMIT,
                      "loader mutex initialization failed");
    }
    loader->catalog = catalog;
    loader->count = count;
    for (size_t i = 0u; i < count; ++i) {
        ios27_macho_info info;
        const ios27_image_spec *spec = &catalog[i];
        if (!bounded_name(spec->name, IOS27_IMAGE_NAME_MAX) ||
            !spec->file || spec->size < 32u ||
            spec->size > 16u * 1024u * 1024u ||
            spec->dependency_count > PHASE23A_MAX ||
            (spec->dependency_count && !spec->dependencies)) {
            (void)report(error, error_cap, IOS27_PHASE23A_INVALID,
                         "invalid catalog image");
            goto rollback;
        }
        for (size_t j = 0u; j < i; ++j)
            if (strcmp(spec->name, catalog[j].name) == 0) {
                (void)report(error, error_cap, IOS27_PHASE23A_INVALID,
                             "duplicate image install name");
                goto rollback;
            }
        if (ios27_macho_parse(spec->file, spec->size, &info,
                              error, error_cap) ||
            info.filetype != 6u || info.encrypted ||
            info.unsupported_required) {
            (void)report(error, error_cap, IOS27_PHASE23A_INVALID,
                         "unsupported or invalid ARM64 dylib");
            goto rollback;
        }
        loader->exports[i] = calloc(1u, sizeof(*loader->exports[i]));
        if (!loader->exports[i]) {
            (void)report(error, error_cap, IOS27_PHASE23A_LIMIT,
                         "export index allocation failed");
            goto rollback;
        }
        if (ios27_exports_parse(spec->file, spec->size, loader->exports[i],
                                error, error_cap)) goto rollback;
        loader->cohorts[i].generation = 1u;
    }
    *out = loader;
    clear_error(error, error_cap);
    return IOS27_PHASE23A_OK;
rollback:
    for (size_t i = 0u; i < count; ++i) free(loader->exports[i]);
    (void)pthread_mutex_destroy(&loader->mutex);
    free(loader);
    return IOS27_PHASE23A_INVALID;
}

void ios27_phase23a_destroy(ios27_phase23a_loader *loader) {
    if (!loader) return;
    (void)pthread_mutex_lock(&loader->mutex);
    for (size_t i = 0u; i < loader->count; ++i) {
        ios27_images_unload(loader->cohorts[i].mapping);
        loader->cohorts[i].mapping = NULL;
        free(loader->exports[i]);
    }
    (void)pthread_mutex_unlock(&loader->mutex);
    (void)pthread_mutex_destroy(&loader->mutex);
    free(loader);
}

int ios27_phase23a_open(ios27_phase23a_loader *loader, const char *name,
                        ios27_phase23a_handle *out,
                        char *error, size_t error_cap) {
    int index, status = IOS27_PHASE23A_INVALID;
    phase23a_cohort *cohort;
    unsigned char reach[PHASE23A_MAX] = {0};
    unsigned char colors[PHASE23A_MAX] = {0};
    ios27_image_spec subset[PHASE23A_MAX];
    ios27_images *staged = NULL;
    size_t selected = 0u;
    if (out) *out = 0u;
    if (!loader || !out)
        return report(error, error_cap, status, "null loader or handle output");
    (void)pthread_mutex_lock(&loader->mutex);
    index = index_of(loader, name);
    if (index < 0) {
        status = report(error, error_cap, IOS27_PHASE23A_NOT_FOUND,
                        "image not in explicit catalog");
        goto done;
    }
    cohort = &loader->cohorts[(size_t)index];
    if (cohort->mapping) {
        if (cohort->direct_refs == UINT32_MAX) {
            status = report(error, error_cap, IOS27_PHASE23A_LIMIT,
                            "direct reference limit exceeded");
            goto done;
        }
        ++cohort->direct_refs;
    } else {
        if (!cohort->generation) {
            status = report(error, error_cap, IOS27_PHASE23A_LIMIT,
                            "handle generation exhausted");
            goto done;
        }
        status = closure_visit(loader, (size_t)index, colors, reach,
                               error, error_cap);
        if (status) goto done;
        for (size_t i = 0u; i < loader->count; ++i)
            if (reach[i]) subset[selected++] = loader->catalog[i];
        if (ios27_images_load(subset, selected, &staged, error, error_cap)) {
            status = IOS27_PHASE23A_LOAD_FAILED;
            goto done;
        }
        /* Atomic commit: no partially linked mapping becomes observable. */
        cohort->mapping = staged;
        staged = NULL;
        memcpy(cohort->closure, reach, sizeof(reach));
        cohort->direct_refs = 1u;
        ++loader->stats.mapping_loads;
        ++loader->stats.live_roots;
    }
    ++loader->stats.opens;
    *out = ((uint64_t)cohort->generation << 8u) | (uint64_t)((size_t)index + 1u);
    clear_error(error, error_cap);
    status = IOS27_PHASE23A_OK;
done:
    if (status != IOS27_PHASE23A_OK) ++loader->stats.failures;
    (void)pthread_mutex_unlock(&loader->mutex);
    ios27_images_unload(staged);
    return status;
}

int ios27_phase23a_close(ios27_phase23a_loader *loader,
                         ios27_phase23a_handle handle,
                         char *error, size_t error_cap) {
    int index;
    ios27_images *retired = NULL;
    if (!loader)
        return report(error, error_cap, IOS27_PHASE23A_INVALID, "null loader");
    (void)pthread_mutex_lock(&loader->mutex);
    index = handle_slot(loader, handle);
    if (index < 0) {
        ++loader->stats.failures;
        (void)pthread_mutex_unlock(&loader->mutex);
        return report(error, error_cap, IOS27_PHASE23A_INVALID,
                      "stale or invalid image handle");
    }
    phase23a_cohort *cohort = &loader->cohorts[(size_t)index];
    --cohort->direct_refs;
    ++loader->stats.closes;
    if (cohort->direct_refs == 0u) {
        retired = cohort->mapping;
        cohort->mapping = NULL;
        memset(cohort->closure, 0, sizeof(cohort->closure));
        if (cohort->generation != UINT32_MAX) ++cohort->generation;
        else cohort->generation = 0u; /* fail closed on generation wrap */
        ++loader->stats.mapping_unloads;
        --loader->stats.live_roots;
    }
    (void)pthread_mutex_unlock(&loader->mutex);
    /* Address retirement occurs only after the final direct root reference. */
    ios27_images_unload(retired);
    clear_error(error, error_cap);
    return IOS27_PHASE23A_OK;
}

int ios27_phase23a_symbol(ios27_phase23a_loader *loader,
                          ios27_phase23a_handle handle,
                          const char *symbol, uintptr_t *out,
                          char *error, size_t error_cap) {
    int index, result = IOS27_PHASE23A_NOT_FOUND;
    uintptr_t address = 0u;
    if (out) *out = 0u;
    if (!loader || !out || !symbol || !symbol[0])
        return report(error, error_cap, IOS27_PHASE23A_INVALID,
                      "invalid lookup arguments");
    (void)pthread_mutex_lock(&loader->mutex);
    index = handle_slot(loader, handle);
    if (index < 0) {
        result = IOS27_PHASE23A_INVALID;
        (void)report(error, error_cap, result, "stale or invalid image handle");
    } else if (ios27_images_symbol(loader->cohorts[(size_t)index].mapping,
                                   loader->catalog[(size_t)index].name,
                                   symbol, &address) == 0) {
        *out = address;
        ++loader->stats.lookups;
        clear_error(error, error_cap);
        result = IOS27_PHASE23A_OK;
    } else {
        (void)report(error, error_cap, result, "symbol not exported by image");
    }
    if (result != IOS27_PHASE23A_OK) ++loader->stats.failures;
    (void)pthread_mutex_unlock(&loader->mutex);
    return result;
}

int ios27_phase23a_flat_symbol(ios27_phase23a_loader *loader,
                               const char *symbol, uintptr_t *out,
                               char *error, size_t error_cap) {
    uintptr_t weak_address = 0u, address = 0u;
    int status = IOS27_PHASE23A_NOT_FOUND;
    if (out) *out = 0u;
    if (!loader || !out || !symbol || !symbol[0])
        return report(error, error_cap, IOS27_PHASE23A_INVALID,
                      "invalid flat lookup arguments");
    (void)pthread_mutex_lock(&loader->mutex);
    for (size_t i = 0u; i < loader->count; ++i) {
        const phase23a_cohort *root = &loader->cohorts[i];
        if (!root->mapping) continue;
        for (size_t j = 0u; j < loader->count; ++j) {
            const ios27_export *exp;
            if (!root->closure[j]) continue;
            exp = ios27_exports_find(loader->exports[j], symbol);
            if (!exp || ios27_images_symbol(root->mapping,
                    loader->catalog[j].name, symbol, &address)) continue;
            if (!(exp->flags & IOS27_EXPORT_WEAK_DEFINITION)) {
                *out = address;
                status = IOS27_PHASE23A_OK;
                goto done;
            }
            if (!weak_address) weak_address = address;
        }
    }
    if (weak_address) {
        *out = weak_address;
        status = IOS27_PHASE23A_OK;
    }
done:
    if (status == IOS27_PHASE23A_OK) {
        ++loader->stats.lookups;
        clear_error(error, error_cap);
    } else {
        ++loader->stats.failures;
        (void)report(error, error_cap, status, "flat symbol not exported");
    }
    (void)pthread_mutex_unlock(&loader->mutex);
    return status;
}

int ios27_phase23a_reference_count(ios27_phase23a_loader *loader,
                                   const char *name, uint64_t *out) {
    int index;
    uint64_t count = 0u;
    if (!loader || !out) return IOS27_PHASE23A_INVALID;
    *out = 0u;
    (void)pthread_mutex_lock(&loader->mutex);
    index = index_of(loader, name);
    if (index >= 0) {
        for (size_t i = 0u; i < loader->count; ++i)
            if (loader->cohorts[i].closure[(size_t)index])
                count += loader->cohorts[i].direct_refs;
        *out = count;
    }
    (void)pthread_mutex_unlock(&loader->mutex);
    return index >= 0 ? IOS27_PHASE23A_OK : IOS27_PHASE23A_NOT_FOUND;
}

int ios27_phase23a_diagnostics_read(ios27_phase23a_loader *loader,
                                    ios27_phase23a_diagnostics *out) {
    if (!loader || !out) return IOS27_PHASE23A_INVALID;
    (void)pthread_mutex_lock(&loader->mutex);
    *out = loader->stats;
    (void)pthread_mutex_unlock(&loader->mutex);
    return IOS27_PHASE23A_OK;
}
