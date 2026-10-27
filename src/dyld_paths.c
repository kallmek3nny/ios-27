#include "ios27/dyld_paths.h"

#include <stdio.h>
#include <string.h>

/* All strings are guest-virtual data. Never inspect the host filesystem. */
static int fail(char *e, size_t n, const char *reason) {
    if (e && n) (void)snprintf(e, n, "%s", reason);
    return -1;
}

/* Never scan beyond the protocol's maximum string length. */
static int length(const char *s, size_t *n) {
    if (!s) return -1;
    for (size_t i = 0; i < IOS27_DYLD_PATH_MAX; ++i) {
        if (s[i] == '\0') {
            *n = i;
            return 0;
        }
    }
    return -1;
}

/* Collapse duplicate slashes, "." and ".."; never escape guest root. */
static int absolute_normalize(const char *src,
                              char out[IOS27_DYLD_PATH_MAX]) {
    size_t len, top = 1u;
    if (length(src, &len) || !len || src[0] != '/') return -1;
    out[0] = '/';
    out[1] = '\0';
    for (const char *p = src + 1; *p;) {
        const char *start;
        size_t n;
        while (*p == '/') ++p;
        if (!*p) break;
        start = p;
        while (*p && *p != '/') ++p;
        n = (size_t)(p - start);
        if (n == 1u && start[0] == '.') continue;
        if (n == 2u && start[0] == '.' && start[1] == '.') {
            if (top == 1u) return -1;
            while (top > 1u && out[top - 1u] != '/') --top;
            if (top > 1u) --top;
            out[top] = '\0';
            continue;
        }
        if (top > 1u) {
            if (top + 1u >= IOS27_DYLD_PATH_MAX) return -1;
            out[top++] = '/';
        }
        if (n >= IOS27_DYLD_PATH_MAX - top) return -1;
        memcpy(out + top, start, n);
        top += n;
        out[top] = '\0';
    }
    return 0;
}

static int expand(const char *path, const char *loader, const char *main,
                  char out[IOS27_DYLD_PATH_MAX]) {
    char image[IOS27_DYLD_PATH_MAX], tmp[IOS27_DYLD_PATH_MAX];
    const char *base, *suffix, *last;
    size_t dir_size, suffix_size;
    if (!path || !path[0] || !loader || !main) return -1;
    if (path[0] == '/') return absolute_normalize(path, out);
    if (strncmp(path, "@loader_path/", 13u) == 0) {
        base = loader;
        suffix = path + 13u;
    } else if (strncmp(path, "@executable_path/", 17u) == 0) {
        base = main;
        suffix = path + 17u;
    } else return -1;
    if (!suffix[0] || length(suffix, &suffix_size) ||
        absolute_normalize(base, image)) return -1;
    last = strrchr(image, '/');
    if (!last) return -1;
    dir_size = (size_t)(last - image);
    if (dir_size == 0u) dir_size = 1u;
    if (dir_size >= sizeof(tmp) - 1u ||
        suffix_size >= sizeof(tmp) - dir_size - 1u) return -1;
    memcpy(tmp, image, dir_size);
    tmp[dir_size] = '/';
    memcpy(tmp + dir_size + 1u, suffix, suffix_size + 1u);
    return absolute_normalize(tmp, out);
}

static void add_unique(ios27_dyld_candidates *out, const char *path) {
    for (size_t i = 0; i < out->count; ++i)
        if (strcmp(out->paths[i], path) == 0) return;
    if (out->count < IOS27_DYLD_MAX_CANDIDATES) {
        (void)snprintf(out->paths[out->count], IOS27_DYLD_PATH_MAX,
                       "%s", path);
        ++out->count;
    }
}

int ios27_dyld_paths(const char *install_name, const char *loader_image,
                     const char *main_executable, const char *const *rpaths,
                     size_t rpath_count, ios27_dyld_candidates *out,
                     char *error, size_t error_size) {
    ios27_dyld_candidates staged;
    char path[IOS27_DYLD_PATH_MAX];
    size_t install_size;
    if (!out) return fail(error, error_size, "null dyld output");
    memset(out, 0, sizeof(*out));
    if (!loader_image || !main_executable ||
        length(install_name, &install_size) || !install_size)
        return fail(error, error_size, "invalid dyld path input");
    if (rpath_count > IOS27_DYLD_MAX_CANDIDATES ||
        (rpath_count && !rpaths))
        return fail(error, error_size, "too many or missing runpaths");
    memset(&staged, 0, sizeof(staged));
    if (strncmp(install_name, "@rpath/", 7u) == 0) {
        const char *suffix = install_name + 7u;
        if (!suffix[0])
            return fail(error, error_size, "invalid rpath install name");
        for (size_t i = 0; i < rpath_count; ++i) {
            char root[IOS27_DYLD_PATH_MAX], joined[IOS27_DYLD_PATH_MAX];
            int bytes;
            if (expand(rpaths[i], loader_image, main_executable, root))
                return fail(error, error_size, "unsupported or unsafe runpath");
            bytes = snprintf(joined, sizeof(joined), "%s/%s", root, suffix);
            if (bytes < 0 || (size_t)bytes >= sizeof(joined) ||
                absolute_normalize(joined, path))
                return fail(error, error_size, "invalid expanded runpath");
            add_unique(&staged, path);
        }
        if (!staged.count)
            return fail(error, error_size, "no runpaths available");
    } else {
        if (expand(install_name, loader_image, main_executable, path))
            return fail(error, error_size, "unsupported or unsafe install name");
        add_unique(&staged, path);
    }
    *out = staged; /* Commit only after every candidate validates. */
    if (error && error_size) error[0] = '\0';
    return 0;
}
