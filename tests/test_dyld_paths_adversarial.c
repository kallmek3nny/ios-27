/* Adversarial tests for ios27_dyld_paths (crew card M, t-4e6db8).
 * They cover what tests/test_dyld_paths.c does not: exact PATH_MAX
 * boundaries, unterminated buffers, expansion past PATH_MAX, dot-dot at the
 * guest root, repeated separators, prefix lookalikes, NULL and embedded-NUL
 * inputs, error buffers of size 0 and 1, and the candidate cap. Every
 * reject must return -1, set a reason and leave the whole output zeroed. */
#undef NDEBUG
#include "ios27/dyld_paths.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOADER "/Applications/Test.app/Frameworks/Tool.framework/Tool"
#define EXE "/Applications/Test.app/Test"

static ios27_dyld_candidates g_out;

static int all_zero(const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; ++i)
        if (b[i]) return 0;
    return 1;
}

/* Fill the output with garbage so each call must clear it itself. */
static void prime(void) {
    memset(&g_out, 0xa5, sizeof(g_out));
}

/* A reject returns -1, sets a reason and leaves the whole output zeroed. */
static void expect_reject(const char *install, const char *loader,
                          const char *exe, const char *const *rpaths,
                          size_t count) {
    char error[128];
    prime();
    memset(error, 0, sizeof(error));
    assert(ios27_dyld_paths(install, loader, exe, rpaths, count, &g_out,
                            error, sizeof(error)) == -1);
    assert(all_zero(&g_out, sizeof(g_out)));
    assert(error[0] != '\0');
}

/* An accept yields exactly one candidate and clears the error buffer. */
static void expect_one(const char *install, const char *loader,
                       const char *exe, const char *const *rpaths,
                       size_t count, const char *want) {
    char error[128];
    prime();
    memset(error, 'Z', sizeof(error));
    assert(ios27_dyld_paths(install, loader, exe, rpaths, count, &g_out,
                            error, sizeof(error)) == 0);
    assert(error[0] == '\0');
    assert(g_out.count == 1u && strcmp(g_out.paths[0], want) == 0);
}

/* Write prefix, then fill bytes up to total, then a NUL at buf[total]. */
static void make_path(char *buf, const char *prefix, char fill,
                      size_t total) {
    size_t n = strlen(prefix);
    assert(n <= total);
    memcpy(buf, prefix, n);
    memset(buf + n, fill, total - n);
    buf[total] = '\0';
}

static void test_install_name_bounds(void) {
    char path[IOS27_DYLD_PATH_MAX + 1u];

    /* 511 bytes plus NUL is the longest accepted install name. */
    make_path(path, "/", 'a', IOS27_DYLD_PATH_MAX - 1u);
    expect_one(path, LOADER, EXE, NULL, 0u, path);
    /* 512 bytes plus NUL is one byte over the bound. */
    make_path(path, "/", 'a', IOS27_DYLD_PATH_MAX);
    expect_reject(path, LOADER, EXE, NULL, 0u);

    /* Separators count toward the raw bound, before normalization. */
    make_path(path, "", '/', IOS27_DYLD_PATH_MAX - 1u);
    path[IOS27_DYLD_PATH_MAX - 2u] = 'X';
    expect_one(path, LOADER, EXE, NULL, 0u, "/X");
    make_path(path, "", '/', IOS27_DYLD_PATH_MAX);
    path[IOS27_DYLD_PATH_MAX - 1u] = 'X';
    expect_reject(path, LOADER, EXE, NULL, 0u);
}

/* Heap buffers of exactly PATH_MAX bytes with no NUL. ASan flags any read
 * past the end, so these also show each scan stops at the protocol bound. */
static void test_unterminated_inputs(void) {
    const char *runs[1];
    char *raw = malloc(IOS27_DYLD_PATH_MAX);
    char *other = malloc(IOS27_DYLD_PATH_MAX);
    assert(raw && other);

    memset(raw, 'a', IOS27_DYLD_PATH_MAX);
    raw[0] = '/';
    expect_reject(raw, LOADER, EXE, NULL, 0u);
    memcpy(raw, "@rpath/", 7u);
    runs[0] = "/R";
    expect_reject(raw, LOADER, EXE, runs, 1u);
    memcpy(raw, "@loader_path/", 13u);
    expect_reject(raw, LOADER, EXE, NULL, 0u);
    memcpy(raw, "@executable_path/", 17u);
    expect_reject(raw, LOADER, EXE, NULL, 0u);

    /* An unterminated loader or main executable rejects when it is used. */
    memset(other, 'b', IOS27_DYLD_PATH_MAX);
    other[0] = '/';
    expect_reject("@loader_path/X", other, EXE, NULL, 0u);
    expect_reject("@executable_path/X", LOADER, other, NULL, 0u);
    runs[0] = "@loader_path/Frameworks";
    expect_reject("@rpath/X", other, EXE, runs, 1u);

    /* Unterminated absolute and relative runpaths. */
    runs[0] = other;
    expect_reject("@rpath/X", LOADER, EXE, runs, 1u);
    other[0] = 'b';
    expect_reject("@rpath/X", LOADER, EXE, runs, 1u);
    free(other);
    free(raw);
}

/* A 401-byte directory: 401 + 1 + 109 = 511 bytes fits, one more does not. */
static void test_expansion_bounds(void) {
    char base[IOS27_DYLD_PATH_MAX], install[IOS27_DYLD_PATH_MAX];
    char want[IOS27_DYLD_PATH_MAX], longrun[IOS27_DYLD_PATH_MAX + 1u];
    const char *runs[2];

    make_path(base, "/", 'd', 401u);
    memcpy(base + 401u, "/Image", 7u);
    make_path(want, "/", 'd', IOS27_DYLD_PATH_MAX - 1u);
    want[401] = '/';
    memset(want + 402, 'x', 109u);

    make_path(install, "@loader_path/", 'x', 13u + 109u);
    expect_one(install, base, EXE, NULL, 0u, want);
    make_path(install, "@loader_path/", 'x', 13u + 110u);
    expect_reject(install, base, EXE, NULL, 0u);
    make_path(install, "@executable_path/", 'x', 17u + 109u);
    expect_one(install, LOADER, base, NULL, 0u, want);
    make_path(install, "@executable_path/", 'x', 17u + 110u);
    expect_reject(install, LOADER, base, NULL, 0u);

    /* The same directory as a runpath, joined to an @rpath suffix. */
    base[401] = '\0';
    runs[0] = base;
    make_path(install, "@rpath/", 'x', 7u + 109u);
    expect_one(install, LOADER, EXE, runs, 1u, want);
    make_path(install, "@rpath/", 'x', 7u + 110u);
    expect_reject(install, LOADER, EXE, runs, 1u);
    /* Overflow in a later runpath discards the earlier candidate. */
    runs[0] = "/Good";
    runs[1] = base;
    expect_reject(install, LOADER, EXE, runs, 2u);

    /* An absolute runpath of 512 bytes plus NUL is over the bound. */
    make_path(longrun, "/", 'r', IOS27_DYLD_PATH_MAX);
    runs[1] = longrun;
    expect_reject("@rpath/Lib", LOADER, EXE, runs, 2u);
}

static void test_root_boundary(void) {
    static const char *const escapes[] = {
        "/..", "//..", "/./..", "/A/../..", "/A/./../..", "/A/B/../../..",
        "@rpath/../../Lib", "@loader_path/../..", "@executable_path/../.."
    };
    const char *runs[1] = { "/A" };

    for (size_t i = 0; i < sizeof(escapes) / sizeof(escapes[0]); ++i)
        expect_reject(escapes[i], "/A/Loader", "/A/Main", runs, 1u);

    /* Climbing back to the root exactly, then descending, stays inside. */
    expect_one("/A/../B", "/A/Loader", "/A/Main", runs, 1u, "/B");
    expect_one("/A/B/../../C", "/A/Loader", "/A/Main", runs, 1u, "/C");
    expect_one("@rpath/../Lib", "/A/Loader", "/A/Main", runs, 1u, "/Lib");
    expect_one("@loader_path/../X", "/A/Loader", "/A/Main", runs, 1u, "/X");
    expect_one("@executable_path/../X", "/A/Loader", "/A/Main", runs, 1u,
               "/X");

    /* Only exact "." and ".." components are special. */
    expect_one("/.../X", "/A/Loader", "/A/Main", NULL, 0u, "/.../X");
    expect_one("/A/..b/.c", "/A/Loader", "/A/Main", NULL, 0u, "/A/..b/.c");

    /* A loader at the root has "/" as its directory. */
    expect_one("@loader_path/X", "/Loader", "/A/Main", NULL, 0u, "/X");
    expect_reject("@loader_path/..", "/Loader", "/A/Main", NULL, 0u);
    expect_reject("@loader_path/./..", "/Loader", "/A/Main", NULL, 0u);

    /* An unsafe loader or main executable never anchors an expansion. */
    expect_reject("@loader_path/X", "/../Loader", "/A/Main", NULL, 0u);
    expect_reject("@executable_path/X", "/A/Loader", "/A/../../Main",
                  NULL, 0u);
    runs[0] = "@loader_path/Frameworks";
    expect_reject("@rpath/X", "/../Loader", "/A/Main", runs, 1u);

    /* Runpaths that climb above the root reject. */
    runs[0] = "@loader_path/../../..";
    expect_reject("@rpath/X", "/A/B/Loader", "/A/Main", runs, 1u);
    runs[0] = "/A/../..";
    expect_reject("@rpath/X", "/A/Loader", "/A/Main", runs, 1u);
}

static void test_separators(void) {
    const char *runs[1] = { "/A/B/" };

    expect_one("/A/B/", LOADER, EXE, NULL, 0u, "/A/B");
    expect_one("///A///B///", LOADER, EXE, NULL, 0u, "/A/B");
    expect_one("@loader_path//X", LOADER, EXE, NULL, 0u,
               "/Applications/Test.app/Frameworks/Tool.framework/X");
    expect_one("@executable_path///Frameworks//A.dylib", LOADER, EXE, NULL,
               0u, "/Applications/Test.app/Frameworks/A.dylib");
    expect_one("@rpath//Lib.dylib", LOADER, EXE, runs, 1u, "/A/B/Lib.dylib");
    runs[0] = "@executable_path//Frameworks//";
    expect_one("@rpath/X", LOADER, EXE, runs, 1u,
               "/Applications/Test.app/Frameworks/X");
    runs[0] = "@loader_path/.";
    expect_one("@rpath/X", "/A/Loader", EXE, runs, 1u, "/A/X");
}

static void test_prefix_lookalikes(void) {
    static const char *const names[] = {
        "", "@rpath", "@rpathX/Lib", "@rpath/", "@RPATH/Lib",
        "@loader_path", "@loader_pathX/Lib", "@loader_path\\Lib",
        "@Loader_path/Lib", "@executable_path", "@executable_paths/Lib",
        "@executable_path/", "@foo/Lib", "@/Lib", "@", "Lib", "./Lib",
        "../Lib", "~/Lib", " /usr/lib/libA.dylib", "\\usr\\lib\\libA.dylib"
    };
    static const char *const bad_runs[] = {
        "", "@rpath", "@rpath/Frameworks", "@loader_path", "@loader_path/",
        "@loader_pathX/F", "@executable_path", "@executable_path/",
        "@executable_paths/F", "@foo/F", "Frameworks", "./Frameworks",
        "@loader_path/../../../../..", "@executable_path/../../..",
        "@loader_path\0/F", NULL
    };
    const char *one[1], *two[2];

    one[0] = "/R";
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        expect_reject(names[i], LOADER, EXE, one, 1u);
    /* Each bad runpath rejects alone and after a valid one. */
    two[0] = "/Good";
    for (size_t i = 0; i < sizeof(bad_runs) / sizeof(bad_runs[0]); ++i) {
        one[0] = bad_runs[i];
        two[1] = bad_runs[i];
        expect_reject("@rpath/Lib", LOADER, EXE, one, 1u);
        expect_reject("@rpath/Lib", LOADER, EXE, two, 2u);
    }
}

static void test_null_inputs(void) {
    const char *runs[1] = { "/R" };

    expect_reject(NULL, LOADER, EXE, NULL, 0u);
    expect_reject("/ok", NULL, EXE, NULL, 0u);
    expect_reject("/ok", LOADER, NULL, NULL, 0u);
    expect_reject("@executable_path/X", NULL, EXE, NULL, 0u);
    expect_reject("@loader_path/X", LOADER, NULL, NULL, 0u);
    expect_reject("@rpath/X", LOADER, EXE, runs, 0u);
    expect_reject(NULL, NULL, NULL, NULL, IOS27_DYLD_MAX_CANDIDATES + 1u);
}

/* Each string ends at its first NUL; later bytes are never consulted. */
static void test_embedded_nuls(void) {
    const char *runs[1] = { "/R" };

    expect_one("/usr/lib/libA.dylib\0/../../../etc/passwd", LOADER, EXE,
               NULL, 0u, "/usr/lib/libA.dylib");
    expect_reject("\0/usr/lib/libA.dylib", LOADER, EXE, NULL, 0u);
    expect_reject("@rpath/\0Lib", LOADER, EXE, runs, 1u);
    expect_reject("@loader_path/\0X", LOADER, EXE, NULL, 0u);
    expect_reject("@executable_path/\0X", LOADER, EXE, NULL, 0u);
    runs[0] = "/Good\0/../../..";
    expect_one("@rpath/Lib", LOADER, EXE, runs, 1u, "/Good/Lib");
    expect_one("@loader_path/X", "/A/Loader\0/../../..", EXE, NULL, 0u,
               "/A/X");
    expect_one("@executable_path/X", LOADER, "/A/Main\0/../../..", NULL, 0u,
               "/A/X");
}

static void test_error_buffers(void) {
    char e[8];

    /* Size 0: a non-NULL buffer is never written. */
    prime();
    memset(e, 'Z', sizeof(e));
    assert(ios27_dyld_paths("relative", LOADER, EXE, NULL, 0u, &g_out,
                            e, 0u) == -1);
    assert(all_zero(&g_out, sizeof(g_out)));
    for (size_t i = 0; i < sizeof(e); ++i) assert(e[i] == 'Z');
    assert(ios27_dyld_paths("/ok", LOADER, EXE, NULL, 0u, &g_out,
                            e, 0u) == 0);
    for (size_t i = 0; i < sizeof(e); ++i) assert(e[i] == 'Z');

    /* Size 1: only the terminator is written, on reject and on accept. */
    prime();
    memset(e, 'Z', sizeof(e));
    assert(ios27_dyld_paths("relative", LOADER, EXE, NULL, 0u, &g_out,
                            e, 1u) == -1);
    assert(all_zero(&g_out, sizeof(g_out)));
    assert(e[0] == '\0' && e[1] == 'Z');
    memset(e, 'Z', sizeof(e));
    assert(ios27_dyld_paths("/ok", LOADER, EXE, NULL, 0u, &g_out,
                            e, 1u) == 0);
    assert(e[0] == '\0' && e[1] == 'Z');

    /* A short buffer truncates the reason and never writes past its size. */
    prime();
    memset(e, 'Z', sizeof(e));
    assert(ios27_dyld_paths("relative", LOADER, EXE, NULL, 0u, &g_out,
                            e, 4u) == -1);
    assert(all_zero(&g_out, sizeof(g_out)));
    assert(strlen(e) == 3u && e[4] == 'Z');

    /* A NULL buffer with a nonzero size is ignored. */
    prime();
    assert(ios27_dyld_paths("relative", LOADER, EXE, NULL, 0u, &g_out,
                            NULL, 128u) == -1);
    assert(all_zero(&g_out, sizeof(g_out)));

    /* A NULL output still reports a reason. */
    memset(e, 'Z', sizeof(e));
    assert(ios27_dyld_paths("/ok", LOADER, EXE, NULL, 0u, NULL,
                            e, sizeof(e)) == -1);
    assert(e[0] != '\0' && e[0] != 'Z' && strlen(e) < sizeof(e));
}

static void test_candidate_cap(void) {
    static const char *const spellings[] = {
        "/R", "/R/", "//R", "/./R", "/R/.", "/Q/../R", "///R//", "/R/S/.."
    };
    char storage[IOS27_DYLD_MAX_CANDIDATES][16];
    const char *runs[IOS27_DYLD_MAX_CANDIDATES];
    char want[32], error[128];

    /* Exactly MAX_CANDIDATES distinct runpaths all survive, in order. */
    for (unsigned i = 0u; i < IOS27_DYLD_MAX_CANDIDATES; ++i) {
        (void)snprintf(storage[i], sizeof(storage[i]), "/R%u", i);
        runs[i] = storage[i];
    }
    prime();
    assert(ios27_dyld_paths("@rpath/X", LOADER, EXE, runs,
                            IOS27_DYLD_MAX_CANDIDATES, &g_out, error,
                            sizeof(error)) == 0);
    assert(g_out.count == IOS27_DYLD_MAX_CANDIDATES);
    for (unsigned i = 0u; i < IOS27_DYLD_MAX_CANDIDATES; ++i) {
        (void)snprintf(want, sizeof(want), "/R%u/X", i);
        assert(strcmp(g_out.paths[i], want) == 0);
    }

    /* MAX_CANDIDATES spellings of one directory collapse to one. */
    for (unsigned i = 0u; i < IOS27_DYLD_MAX_CANDIDATES; ++i)
        runs[i] = spellings[i % (sizeof(spellings) / sizeof(spellings[0]))];
    expect_one("@rpath/X", LOADER, EXE, runs, IOS27_DYLD_MAX_CANDIDATES,
               "/R/X");
}

int main(void) {
    test_install_name_bounds();
    test_unterminated_inputs();
    test_expansion_bounds();
    test_root_boundary();
    test_separators();
    test_prefix_lookalikes();
    test_null_inputs();
    test_embedded_nuls();
    test_error_buffers();
    test_candidate_cap();
    puts("dyld path adversarial tests passed");
    return 0;
}
