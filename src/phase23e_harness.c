#include "ios27/phase23e_harness.h"
#include "ios27/dyld_paths.h"
#include "ios27/macho.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IOS27_23E_SECTIONS_MAX 64u
#define IOS27_23E_FNV_SEED UINT64_C(14695981039346656037)

typedef struct {
    ios27_macho_info info;
    ios27_dyld_image_metadata dyld;
    const char *deps[IOS27_IMAGES_MAX];
} phase23e_checked;

static uint32_t r32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t r64(const uint8_t *p) {
    return (uint64_t)r32(p) | ((uint64_t)r32(p + 4u) << 32u);
}
static uint64_t hash_bytes(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = data;
    for (size_t i = 0u; i < n; ++i) {
        h ^= p[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}
static uint64_t hash_word(uint64_t h, uint64_t value) {
    unsigned char bytes[8];
    for (size_t i = 0u; i < sizeof(bytes); ++i)
        bytes[i] = (unsigned char)(value >> (8u * i));
    return hash_bytes(h, bytes, sizeof(bytes));
}
static void report_failure(ios27_23e_report *r, ios27_23e_status status,
                           const char *message) {
    r->status = status;
    (void)snprintf(r->diagnostic, sizeof(r->diagnostic), "%s", message);
    r->trace = hash_word(r->trace, (uint64_t)status);
    r->trace = hash_bytes(r->trace, message, strlen(message));
}
static int checked_string(const char *s, size_t limit) {
    return s && memchr(s, 0, limit) && s[0] != 0;
}
static int checked_vector(const char *const *items, size_t count,
                          size_t *budget) {
    if (!items) return count == 0u ? 0 : -1;
    for (size_t i = 0u; i < count; ++i) {
        const char *end;
        size_t n;
        if (!items[i] || !(end = memchr(items[i], 0, IOS27_23E_MAX_STRING)))
            return -1;
        n = (size_t)(end - items[i]) + 1u;
        if (n > *budget) return -1;
        *budget -= n;
    }
    return items[count] ? -1 : 0;
}
static int checked_context(const ios27_23e_application *app) {
    size_t budget = 16u * 1024u;
    size_t env = 0u, apple = 0u;
    if (app->argc < 0 || app->argc > (int)IOS27_23E_MAX_ARGS ||
        (app->argc && !app->argv)) return -1;
    if (app->envp) {
        while (env <= IOS27_23E_MAX_ARGS && app->envp[env]) ++env;
        if (env > IOS27_23E_MAX_ARGS) return -1;
    }
    if (app->apple) {
        while (apple <= IOS27_23E_MAX_ARGS && app->apple[apple]) ++apple;
        if (apple > IOS27_23E_MAX_ARGS) return -1;
    }
    return checked_vector(app->argv, (size_t)app->argc, &budget) ||
           checked_vector(app->envp, env, &budget) ||
           checked_vector(app->apple, apple, &budget) ? -1 : 0;
}
/* Scan actual section metadata before any mapping. No constructor callbacks
 * are executed by the merged loader: startup must fail closed, not silently
 * skip side effects. Section bounds have been checked by macho_parse, but
 * inspect them again here for independent diagnostics. */
static int has_initializer_section(const uint8_t *data, size_t size) {
    size_t p = 32u;
    uint32_t count;
    if (!data || size < 32u) return -1;
    count = r32(data + 16u);
    if (r32(data + 20u) > size - 32u) return -1;
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t cmd, bytes;
        if (p > size - 8u) return -1;
        cmd = r32(data + p);
        bytes = r32(data + p + 4u);
        if (bytes < 8u || bytes > size - p) return -1;
        if (cmd == 0x19u) {
            uint32_t sections;
            if (bytes < 72u) return -1;
            sections = r32(data + p + 64u);
            if (sections > IOS27_23E_SECTIONS_MAX ||
                (size_t)sections > (bytes - 72u) / 80u) return -1;
            for (uint32_t j = 0u; j < sections; ++j) {
                const uint8_t *s = data + p + 72u + 80u * (size_t)j;
                uint32_t type = r32(s + 64u) & 255u;
                if ((type == 9u || type == 10u || type == 0x16u ||
                     memcmp(s, "__init_offsets", 14u) == 0 ||
                     memcmp(s, "__mod_init_func", 15u) == 0 ||
                     memcmp(s, "__mod_term_func", 15u) == 0) &&
                    r64(s + 40u))
                    return 1;
            }
        }
        p += bytes;
    }
    return 0;
}
static int prepare(const ios27_23e_image *images, size_t count,
                   phase23e_checked checked[IOS27_IMAGES_MAX],
                   ios27_image_spec specs[IOS27_IMAGES_MAX],
                   const char *paths[IOS27_IMAGES_MAX],
                   ios27_23e_report *report) {
    char err[256] = {0};
    if (!images || !count || count > IOS27_IMAGES_MAX) {
        report_failure(report, IOS27_23E_REJECTED, "invalid image catalog size");
        return -1;
    }
    for (size_t i = 0u; i < count; ++i) {
        const ios27_23e_image *im = &images[i];
        int init;
        if (!checked_string(im->name, IOS27_IMAGE_NAME_MAX) ||
            !checked_string(im->guest_path, IOS27_DYLD_PATH_MAX) ||
            im->guest_path[0] != '/' || !im->data ||
            im->size < 32u || im->size > IOS27_23E_MAX_BYTES) {
            report_failure(report, IOS27_23E_REJECTED,
                           "missing, oversized or invalid catalog image");
            return -1;
        }
        for (size_t j = 0u; j < i; ++j)
            if (!strcmp(im->name, images[j].name) ||
                !strcmp(im->guest_path, images[j].guest_path)) {
                report_failure(report, IOS27_23E_REJECTED,
                               "duplicate image identity or guest path");
                return -1;
            }
        report->trace = hash_bytes(report->trace, im->name, strlen(im->name));
        report->trace = hash_bytes(report->trace, im->guest_path,
                                   strlen(im->guest_path));
        report->trace = hash_bytes(report->trace, im->data, im->size);
        if (ios27_macho_parse(im->data, im->size, &checked[i].info,
                              err, sizeof(err)) ||
            ios27_dyld_inspect_image(im->data, im->size, &checked[i].dyld,
                                     err, sizeof(err))) {
            report_failure(report, IOS27_23E_REJECTED,
                           err[0] ? err : "malformed Mach-O");
            return -1;
        }
        init = has_initializer_section(im->data, im->size);
        if (init < 0) {
            report_failure(report, IOS27_23E_REJECTED,
                           "invalid initializer section table");
            return -1;
        }
        if (init) {
            report_failure(report, IOS27_23E_UNSUPPORTED_ABI,
                           "initializer/destructor callback execution unsupported");
            return -1;
        }
        if (checked[i].dyld.dependency_count > IOS27_IMAGES_MAX) {
            report_failure(report, IOS27_23E_REJECTED,
                           "too many load-command dependencies");
            return -1;
        }
        for (size_t j = 0u; j < checked[i].dyld.dependency_count; ++j)
            checked[i].deps[j] = checked[i].dyld.dependencies[j].install_name;
        specs[i] = (ios27_image_spec) {
            im->name, im->data, im->size, checked[i].deps,
            checked[i].dyld.dependency_count
        };
        paths[i] = im->guest_path;
    }
    return 0;
}
static void finish(ios27_23e_report *r, ios27_images *graph) {
    ios27_images_unload(graph);
    r->mapped_graphs_live = 0u;
    r->trace = hash_word(r->trace, (uint64_t)r->verified_guest_operations);
}
int ios27_23e_application_probe(const ios27_23e_application *app,
                                 ios27_23e_report *report) {
    phase23e_checked *checked;
    ios27_image_spec specs[IOS27_IMAGES_MAX];
    const char *paths[IOS27_IMAGES_MAX];
    ios27_images *graph = NULL;
    char error[256] = {0};
    uintptr_t entry = 0u;
    size_t main_count = 0u;
    if (!report) return -1;
    memset(report, 0, sizeof(*report));
    report->trace = IOS27_23E_FNV_SEED;
    report->status = IOS27_23E_REJECTED;
    if (!app || !app->main_name || !app->main_path ||
        checked_context(app)) {
        report_failure(report, IOS27_23E_REJECTED, "invalid guest startup context");
        return -1;
    }
    if (app->needs_objc || app->needs_mach || app->needs_threads) {
        report_failure(report, IOS27_23E_UNSUPPORTED_ABI,
                       "requested ObjC, Mach, or threading ABI has no verified app bridge");
        return -1;
    }
    checked = calloc(IOS27_IMAGES_MAX, sizeof(*checked));
    if (!checked) {
        report_failure(report, IOS27_23E_LOAD_FAILED, "metadata allocation failed");
        return -1;
    }
    if (prepare(app->images, app->count, checked, specs, paths, report))
        goto done;
    for (size_t i = 0u; i < app->count; ++i) {
        if (checked[i].info.filetype == IOS27_MH_EXECUTE) {
            ++main_count;
            if (strcmp(app->images[i].name, app->main_name) != 0 ||
                strcmp(app->images[i].guest_path, app->main_path) != 0 ||
                !checked[i].info.has_main) {
                report_failure(report, IOS27_23E_REJECTED,
                               "unmatched or missing LC_MAIN executable");
                goto done;
            }
        }
    }
    if (main_count != 1u) {
        report_failure(report, IOS27_23E_REJECTED,
                       "catalog must contain exactly one MH_EXECUTE");
        goto done;
    }
    if (ios27_images_load_with_paths(specs, app->count, paths, app->main_path,
                                     &graph, error, sizeof(error))) {
        report_failure(report, IOS27_23E_LOAD_FAILED,
                       error[0] ? error : "graph load rejected");
        goto done;
    }
    report->mapped_graphs_live = 1u;
    report->images_loaded = app->count;
    if (ios27_images_entrypoint(graph, app->main_name, &entry) || !entry) {
        report_failure(report, IOS27_23E_UNSUPPORTED_ABI,
                       "loader denies LC_MAIN entry (dyld startup ABI unavailable)");
        goto done;
    }
    /* LC_MAIN validation does not imply a complete application ABI.
     * The merged loader has no guarded-stack application entry API for
     * multi-image MH_EXECUTE. Refuse to jump to a guest function here. */
    report_failure(report, IOS27_23E_UNSUPPORTED_ABI,
                   "native multi-image process startup bridge not verified");
done:
    finish(report, graph);
    free(checked);
    return -1;
}
int ios27_23e_library_stress(const ios27_23e_image *images, size_t count,
                             const char *root_image, unsigned iterations,
                             ios27_23e_report *report) {
    phase23e_checked *checked;
    ios27_image_spec specs[IOS27_IMAGES_MAX];
    const char *paths[IOS27_IMAGES_MAX];
    ios27_images *graph = NULL;
    char error[256] = {0};
    uintptr_t address = 0u;
    if (!report) return -1;
    memset(report, 0, sizeof(*report));
    report->status = IOS27_23E_REJECTED;
    report->trace = IOS27_23E_FNV_SEED;
    if (!root_image || !iterations || iterations > 1000000u) {
        report_failure(report, IOS27_23E_REJECTED, "invalid native library workload");
        return -1;
    }
    checked = calloc(IOS27_IMAGES_MAX, sizeof(*checked));
    if (!checked) {
        report_failure(report, IOS27_23E_LOAD_FAILED, "metadata allocation failed");
        return -1;
    }
    if (prepare(images, count, checked, specs, paths, report))
        goto done;
    for (size_t i = 0u; i < count; ++i)
        if (checked[i].info.filetype != 6u) {
            report_failure(report, IOS27_23E_REJECTED,
                           "native library workload requires MH_DYLIB only");
            goto done;
        }
    if (ios27_images_load_with_paths(specs, count, paths, "/App/phase23e",
                                     &graph, error, sizeof(error))) {
        report_failure(report, IOS27_23E_LOAD_FAILED,
                       error[0] ? error : "graph load rejected");
        goto done;
    }
    report->mapped_graphs_live = 1u;
    report->images_loaded = count;
    if (ios27_images_symbol(graph, root_image, "_phase23e_pipeline_eval",
                             &address) || !address) {
        report_failure(report, IOS27_23E_LOAD_FAILED,
                       "required native pipeline export missing");
        goto done;
    }
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    {
        int (*call)(int);
        memcpy(&call, &address, sizeof(call));
        for (unsigned i = 0u; i < iterations; ++i) {
            int expected = (int)(43u + i);
            int observed = call(3);
            if (observed != expected) {
                report_failure(report, IOS27_23E_EXECUTION_FAILED,
                               "native cross-dylib return/state mismatch");
                goto done;
            }
            report->verified_guest_operations++;
            report->trace = hash_word(report->trace, (uint64_t)(unsigned)observed);
        }
    }
    report->status = IOS27_23E_OK;
    (void)snprintf(report->diagnostic, sizeof(report->diagnostic),
                   "verified compiler-linked native cross-dylib execution");
#else
    report_failure(report, IOS27_23E_UNSUPPORTED_HOST,
                   "host cannot execute ARM64 Darwin instructions");
#endif
done:
    finish(report, graph);
    free(checked);
    return report->status == IOS27_23E_OK ? 0 : -1;
}
