/* Standalone phase 23e checker; links the unchanged merged ios27_core.
 * No success is claimed for application startup, constructors or native
 * execution on an x86_64 host. Test results are structured JSON lines.
 */
#include "ios27/phase23e_harness.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ensure(int ok, const char *message) {
    if (!ok) (void)fprintf(stderr, "phase23e FAIL: %s\n", message);
    return ok ? 0 : -1;
}
static uint8_t *read_image(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) ||
        (length = ftell(file)) < 32 ||
        (unsigned long)length > IOS27_23E_MAX_BYTES ||
        fseek(file, 0, SEEK_SET)) {
        (void)fclose(file);
        return NULL;
    }
    bytes = malloc((size_t)length);
    if (!bytes) {
        (void)fclose(file);
        return NULL;
    }
    *size = (size_t)length;
    if (fread(bytes, 1u, *size, file) != *size) {
        free(bytes);
        (void)fclose(file);
        return NULL;
    }
    if (fclose(file)) {
        free(bytes);
        return NULL;
    }
    return bytes;
}
static int check_app(const char *kind, ios27_23e_image *all,
                     const unsigned *which, size_t count, const char *path) {
    static const char *const args[] = {"phase23e", NULL, NULL};
    static const char *const env[] = {"MODE=trusted", NULL};
    static const char *const apple[] = {"exec=/App/phase23e", NULL};
    const char *const actual_args[] = {"phase23e", kind, NULL};
    ios27_23e_image catalog[IOS27_IMAGES_MAX];
    ios27_23e_application app;
    ios27_23e_report report, again;
    (void)args;
    for (size_t i = 0; i < count; ++i)
        catalog[i] = all[which[i]];
    memset(&app, 0, sizeof(app));
    app.images = catalog;
    app.count = count;
    app.main_name = catalog[0].name;
    app.main_path = path;
    app.argc = 2;
    app.argv = actual_args;
    app.envp = env;
    app.apple = apple;
    if (ensure(ios27_23e_application_probe(&app, &report) == -1,
               "application execution must be fail-closed") ||
        ensure(report.status == IOS27_23E_UNSUPPORTED_ABI,
               report.diagnostic) ||
        ensure(report.mapped_graphs_live == 0u,
               "application startup leaked graph") ||
        ensure(report.verified_guest_operations == 0u,
               "unsupported app cannot report operations"))
        return -1;
    if (ensure(ios27_23e_application_probe(&app, &again) == -1,
               "replay startup should fail closed") ||
        ensure(again.trace == report.trace &&
               again.status == report.status,
               "non-deterministic app replay trace"))
        return -1;
    (void)printf("{\"case\":\"%s\",\"status\":\"unsupported_abi\","
                 "\"images\":%zu,\"native_ops\":0,"
                 "\"replay_trace\":\"%016" PRIx64 "\"}\n",
                 kind, report.images_loaded, report.trace);
    return 0;
}
int main(int argc, char **argv) {
    static const char *const names[] = {
        "base.dylib", "math.dylib", "logic.dylib",
        "state.dylib", "pipeline.dylib", "ctor.dylib",
        "app-calc", "app-state", "app-pipeline"
    };
    static const char *const paths[] = {
        "/App/Frameworks/Base.dylib", "/App/Frameworks/Math.dylib",
        "/App/Frameworks/Logic.dylib", "/App/Frameworks/State.dylib",
        "/App/Frameworks/Pipeline.dylib", "/App/Frameworks/Ctor.dylib",
        "/App/Calc", "/App/State", "/App/Pipeline"
    };
    const unsigned calc[] = {6u, 0u, 1u, 2u};
    const unsigned state[] = {7u, 0u, 3u};
    const unsigned pipeline[] = {8u, 0u, 1u, 2u, 3u, 4u};
    const unsigned library_order[] = {4u, 2u, 3u, 1u, 0u};
    ios27_23e_image all[9], libs[5];
    ios27_23e_report report, again;
    uint8_t *owned[9] = {NULL};
    uint32_t rng = UINT32_C(0x23e1029);
    unsigned runs;
    uint64_t verified = 0u;
#if !defined(__aarch64__)
    (void)verified;
#endif
    if (argc != 11) {
        fputs("usage: phase23e-check RUNS BASE MATH LOGIC STATE PIPELINE "
              "CTOR APP_CALC APP_STATE APP_PIPELINE\n", stderr);
        return 2;
    }
    runs = (unsigned)strtoul(argv[1], NULL, 10);
    if (runs < 1u || runs > 200u) return 2;
    for (size_t i = 0u; i < 9u; ++i) {
        size_t n = 0u;
        owned[i] = read_image(argv[i + 2u], &n);
        if (!owned[i]) return 2;
        all[i] = (ios27_23e_image){names[i], paths[i], owned[i], n};
    }
    if (check_app("calc", all, calc, 4u, paths[6]) ||
        check_app("state", all, state, 3u, paths[7]) ||
        check_app("pipeline", all, pipeline, 6u, paths[8]))
        return 1;
    for (size_t i = 0u; i < 5u; ++i)
        libs[i] = all[library_order[i]];
    /* Every native return is verified. Unlike an emulated success counter,
     * this grows ONLY when actual aarch64 instructions have executed. */
    for (unsigned cycle = 0u; cycle < runs; ++cycle) {
        int rc = ios27_23e_library_stress(libs, 5u, "pipeline.dylib",
                                          1024u, &report);
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
        if (ensure(rc == 0 && report.status == IOS27_23E_OK &&
                   report.verified_guest_operations == 1024u,
                   report.diagnostic)) return 1;
        verified += report.verified_guest_operations;
#else
        if (ensure(rc == -1 && report.status == IOS27_23E_UNSUPPORTED_HOST &&
                   report.verified_guest_operations == 0u,
                   report.diagnostic)) return 1;
#endif
        if (ensure(report.mapped_graphs_live == 0u, "library graph leaked"))
            return 1;
        if (!cycle) {
            const int replay = ios27_23e_library_stress(
                libs, 5u, "pipeline.dylib", 1024u, &again);
            if (ensure(replay == rc && report.trace == again.trace,
                       "library stress replay diverged")) return 1;
        }
    }
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
    if (ensure(verified >= 102400u, "native 100k-op target missed"))
        return 1;
    (void)printf("{\"case\":\"libraries\",\"status\":\"verified_native\","
                 "\"native_ops\":%" PRIu64 ",\"cycles\":%u}\n",
                 verified, runs);
#else
    (void)printf("{\"case\":\"libraries\",\"status\":\"unsupported_host\","
                 "\"native_ops\":0,\"cycles\":%u}\n", runs);
#endif
    /* Deterministic malformed-image fuzz: corrupt the actual Mach header,
     * demand a parser rejection and verify that no mapped graph survives. */
    for (unsigned trial = 0u; trial < 128u; ++trial) {
        uint8_t *mutant = malloc(all[0].size);
        ios27_23e_image faulty[5];
        if (!mutant) return 1;
        memcpy(mutant, all[0].data, all[0].size);
        rng ^= rng << 13u;
        rng ^= rng >> 17u;
        rng ^= rng << 5u;
        mutant[trial % 4u] ^= (uint8_t)((rng & 255u) | 1u);
        memcpy(faulty, libs, sizeof(faulty));
        faulty[4].data = mutant;
        if (ensure(ios27_23e_library_stress(faulty, 5u, "pipeline.dylib",
                                             1u, &report) == -1 &&
                   report.status == IOS27_23E_REJECTED &&
                   report.mapped_graphs_live == 0u &&
                   report.verified_guest_operations == 0u,
                   "corrupted header unexpectedly accepted")) {
            free(mutant);
            return 1;
        }
        free(mutant);
    }
    if (ensure(ios27_23e_library_stress(libs, 5u, "pipeline.dylib",
                                        1u, &report) !=
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
               -1
#else
               0
#endif
               , "recovery after fuzz failed"))
        return 1;
    (void)printf("{\"case\":\"fuzz\",\"status\":\"rejected_and_recovered\","
                 "\"malformed_headers\":128,\"seed\":\"0x23e1029\"}\n");
    {
        const ios27_23e_image initializer[] = {all[5]};
        if (ensure(ios27_23e_library_stress(initializer, 1u,
                          "ctor.dylib", 1u, &report) == -1 &&
                   report.status == IOS27_23E_UNSUPPORTED_ABI &&
                   report.mapped_graphs_live == 0u &&
                   report.verified_guest_operations == 0u,
                   "constructor was silently accepted"))
            return 1;
        (void)printf("{\"case\":\"constructors\","
                     "\"status\":\"unsupported_abi\",\"native_ops\":0}\n");
    }
    for (size_t i = 0u; i < 9u; ++i) free(owned[i]);
    return 0;
}
