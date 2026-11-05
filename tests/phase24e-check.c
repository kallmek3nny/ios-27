#include "ios27/phase24e_application.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *data; size_t size; } file_bytes;

static int read_file(const char *path, file_bytes *out) {
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 32 ||
        n > (long)IOS27_24E_MAX_IMAGE_BYTES || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return -1;
    }
    out->data = malloc((size_t)n);
    if (!out->data) { fclose(f); return -1; }
    out->size = (size_t)n;
    if (fread(out->data, 1u, out->size, f) != out->size || fclose(f)) {
        free(out->data);
        out->data = NULL;
        return -1;
    }
    return 0;
}
static uint64_t rng(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13u;
    x ^= x >> 7u;
    x ^= x << 17u;
    *state = x;
    return x;
}
static void catalog(const file_bytes files[9], size_t app_index,
                    ios27_24e_image out[7], size_t *count) {
    static const char *names[] = {"base", "math", "logic", "state",
                                   "pipeline", "ctor"};
    static const char *paths[] = {
        "/App/Frameworks/Base.dylib", "/App/Frameworks/Math.dylib",
        "/App/Frameworks/Logic.dylib", "/App/Frameworks/State.dylib",
        "/App/Frameworks/Pipeline.dylib", "/App/Frameworks/Ctor.dylib"
    };
    out[0] = (ios27_24e_image){"app", "/App/phase24e",
                                 files[app_index].data, files[app_index].size};
    for (size_t i = 0u; i < 6u; ++i)
        out[i + 1u] = (ios27_24e_image){names[i], paths[i],
                                       files[i].data, files[i].size};
    *count = 6u; /* constructor library exists but is NOT part of success */
}
static ios27_24e_application application(ios27_24e_image *images,
                                         size_t count, const char *const *argv,
                                         uint64_t operations) {
    static const char *const envp[] = {"MODE=trusted", NULL};
    static const char *const apple[] = {"exec=/App/phase24e", NULL};
    ios27_24e_application app = {0};
    app.images = images;
    app.image_count = count;
    app.executable_name = "app";
    app.executable_path = "/App/phase24e";
    app.argc = 2;
    app.argv = argv;
    app.envp = envp;
    app.apple = apple;
    app.expected_guest_operations = operations;
    app.expected_exit_code = 0;
    return app;
}
static int no_leaks(const ios27_24e_report *report) {
    return report->live_graphs == 0u && report->live_stacks == 0u;
}
static void print_report(const char *kind, const char *name, int successes,
                         uint64_t operations, uint64_t replay) {
    printf("{\"kind\":\"%s\",\"name\":\"%s\",\"runs\":%d,"
           "\"verified_native_operations\":%llu,\"replay_hash\":%llu,"
           "\"arch\":\"%s\"}\n",
           kind, name, successes, (unsigned long long)operations,
           (unsigned long long)replay,
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
           "arm64");
#else
           "non-arm64");
#endif
}

int main(int argc, char **argv) {
    file_bytes files[9] = {{0}};
    static const char *const modes[] = {"calc", "state", "pipeline"};
    static const uint64_t expected[] = {3072u, 1024u, 1024u};
    uint64_t seed = UINT64_C(0x24e5b7a1f03dc921);
    int runs, success = 0;
    if (argc != 11) return fprintf(stderr, "expected runs and nine fixtures\n"), 2;
    runs = atoi(argv[1]);
    if (runs < 1 || runs > 200) return 2;
    for (size_t i = 0u; i < 9u; ++i)
        if (read_file(argv[i + 2u], &files[i]))
            return fprintf(stderr, "fixture read failure: %s\n", argv[i + 2u]), 3;

    for (size_t m = 0u; m < 3u; ++m) {
        uint64_t total = 0u, baseline = 0u;
        ios27_24e_image images[7];
        size_t count;
        const char *const args[] = {"phase24e", modes[m], NULL};
        ios27_24e_application app;
        catalog(files, m + 6u, images, &count);
        app = application(images, count, args, expected[m]);
        for (int i = 0; i < runs; ++i) {
            ios27_24e_report report;
            int rc = ios27_24e_run(&app, &report);
            if (!no_leaks(&report)) {
                fprintf(stderr, "%s resource leak at %d\n", modes[m], i);
                goto failed;
            }
#if defined(__aarch64__) && (defined(__linux__) || defined(__ANDROID__))
            if (rc || report.status != IOS27_24E_NATIVE_EXECUTED ||
                !report.actual_guest_entry || report.instruction_trace_available ||
                report.verified_guest_operations != expected[m] ||
                report.exit_code != 0) {
                fprintf(stderr, "guest %s failure at %d: status=%d %s\n",
                        modes[m], i, report.status, report.diagnostic);
                goto failed;
            }
#else
            if (!rc || report.status != IOS27_24E_UNSUPPORTED_HOST ||
                report.actual_guest_entry ||
                report.verified_guest_operations != 0u) {
                fprintf(stderr, "host classification failure at %d: %s\n",
                        i, report.diagnostic);
                goto failed;
            }
#endif
            if (i && baseline != report.replay_hash) {
                fprintf(stderr, "%s nondeterministic replay\n", modes[m]);
                goto failed;
            }
            baseline = report.replay_hash;
            total += report.verified_guest_operations;
            success++;
        }
        if (strcmp(args[0], "phase24e") || strcmp(args[1], modes[m])) {
            fprintf(stderr, "guest modified host string pointers\n");
            goto failed;
        }
        print_report("application", modes[m], runs, total, baseline);
    }
    {
        ios27_24e_image images[7];
        size_t count;
        const char *const args[] = {"phase24e", "calc", NULL};
        ios27_24e_application app;
        uint8_t *altered = malloc(files[6].size);
        if (!altered) goto failed;
        catalog(files, 6u, images, &count);
        app = application(images, count, args, 3072u);
        for (int i = 0; i < 128; ++i) {
            ios27_24e_report report;
            memcpy(altered, files[6].data, files[6].size);
            altered[0] ^= (uint8_t)(1u + rng(&seed) % 255u);
            images[0].bytes = altered;
            if (!ios27_24e_run(&app, &report) ||
                report.status != IOS27_24E_REJECTED ||
                report.actual_guest_entry || report.verified_guest_operations ||
                !no_leaks(&report)) {
                fprintf(stderr, "fuzz accepted malformed Mach-O magic: %d\n", i);
                free(altered);
                goto failed;
            }
        }
        images[0].bytes = files[6].data;
        free(altered);
        print_report("negative", "fuzz-128", 128, 0u, seed);
    }
    {
        ios27_24e_image images[7];
        size_t count;
        ios27_24e_report report;
        const char *const args[] = {"phase24e", "calc", NULL};
        ios27_24e_application app;
        catalog(files, 6u, images, &count);
        count = 7u;
        app = application(images, count, args, 3072u);
        if (!ios27_24e_run(&app, &report) ||
            report.status != IOS27_24E_UNSUPPORTED_ABI ||
            report.actual_guest_entry || !no_leaks(&report)) {
            fprintf(stderr, "constructor handling was not fail-closed: %s\n",
                    report.diagnostic);
            goto failed;
        }
        print_report("negative", "constructor-gate", 1, 0u, report.replay_hash);
    }
    {
        ios27_24e_image images[7];
        size_t count;
        ios27_24e_report report;
        const char *const args[] = {"phase24e", "calc", NULL};
        ios27_24e_application app;
        catalog(files, 6u, images, &count);
        app = application(images, count, args, 3072u);
        app.needs_objc = 1u;
        app.needs_mach = 1u;
        app.needs_threads = 1u;
        app.needs_syscalls = 1u;
        if (!ios27_24e_run(&app, &report) ||
            report.status != IOS27_24E_UNSUPPORTED_ABI ||
            report.actual_guest_entry || !no_leaks(&report)) {
            fprintf(stderr, "unverified app service interface was accepted\n");
            goto failed;
        }
        print_report("negative", "runtime-gates", 1, 0u, report.replay_hash);
    }
    fprintf(stderr, "phase24e: %d startup classifications, 128 fuzz cases, no live leaks\n",
            success);
    for (size_t i = 0u; i < 9u; ++i) free(files[i].data);
    return 0;
failed:
    for (size_t i = 0u; i < 9u; ++i) free(files[i].data);
    return 1;
}
