#include "ios27/dyld_paths.h"
#include <stdio.h>
#include <string.h>

static int fail(char *e, size_t n, const char *reason) {
    if (e && n) (void)snprintf(e, n, "%s", reason);
    return -1;
}

static int absolute_normalize(const char *src,
                              char out[IOS27_DYLD_PATH_MAX]) {
    size_t top = 1u;
    if (!src || src[0] != '/' || strlen(src) >= IOS27_DYLD_PATH_MAX)
        return -1;
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
            if (out[top - 1u] == '/') top--;
            while (top > 1u && out[top - 1u] != '/') top--;
            if (top > 1u && out[top - 1u] == '/') top--;
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
    char tmp[IOS27_DYLD_PATH_MAX];
    const char *base = NULL, *suffix = NULL, *last;
    size_t dir_size, suffix_size;
    if (!path || !*path || !loader || !main) return -1;
    if (path[0] == '/') return absolute_normalize(path, out);
    if (strncmp(path, "@loader_path/", 13u) == 0) {
        base = loader; suffix = path + 13u;
    } else if (strncmp(path, "@executable_path/", 17u) == 0) {
        base = main; suffix = path + 17u;
    } else return -1;
    if (base[0] != '/' || strlen(base) >= IOS27_DYLD_PATH_MAX ||
        strlen(suffix) >= IOS27_DYLD_PATH_MAX) return -1;
    last = strrchr(base, '/');
    if (!last) return -1;
    dir_size = (size_t)(last - base);
    if (dir_size == 0u) dir_size = 1u;
    suffix_size = strlen(suffix);
    if (dir_size + 1u + suffix_size >= sizeof(tmp)) return -1;
    memcpy(tmp, base, dir_size);
    tmp[dir_size] = '/';
    memcpy(tmp + dir_size + 1u, suffix, suffix_size + 1u);
    return absolute_normalize(tmp, out);
}

static void add_unique(ios27_dyld_candidates *out, const char *path) {
    for (size_t i = 0; i < out->count; i++)
        if (strcmp(out->paths[i], path) == 0) return;
    if (out->count < IOS27_DYLD_MAX_CANDIDATES) {
        (void)snprintf(out->paths[out->count], IOS27_DYLD_PATH_MAX,
                       "%s", path);
        out->count++;
    }
}

int ios27_dyld_paths(const char *install_name, const char *loader_image,
                     const char *main_executable, const char *const *rpaths,
                     size_t rpath_count, ios27_dyld_candidates *out,
                     char *error, size_t error_size) {
    char path[IOS27_DYLD_PATH_MAX];
    if (!out || !install_name || !loader_image || !main_executable)
        return fail(error, error_size, "null dyld path input");
    memset(out, 0, sizeof(*out));
    if (rpath_count > IOS27_DYLD_MAX_CANDIDATES ||
        (rpath_count && !rpaths))
        return fail(error, error_size, "too many or missing runpaths");
    if (strncmp(install_name, "@rpath/", 7u) == 0) {
        const char *suffix = install_name + 7u;
        if (!*suffix || strlen(suffix) >= sizeof(path))
            return fail(error, error_size, "invalid rpath install name");
        for (size_t i = 0; i < rpath_count; i++) {
            char root[IOS27_DYLD_PATH_MAX], joined[IOS27_DYLD_PATH_MAX];
            int bytes;
            if (expand(rpaths[i], loader_image, main_executable, root) != 0)
                return fail(error, error_size, "unsupported or unsafe runpath");
            bytes = snprintf(joined, sizeof(joined), "%s/%s", root, suffix);
            if (bytes < 0 || (size_t)bytes >= sizeof(joined) ||
                absolute_normalize(joined, path) != 0)
                return fail(error, error_size, "invalid expanded runpath");
            add_unique(out, path);
        }
        if (out->count == 0u)
            return fail(error, error_size, "no runpaths available");
    } else {
        if (expand(install_name, loader_image, main_executable, path) != 0)
            return fail(error, error_size, "unsupported or unsafe install name");
        add_unique(out, path);
    }
    if (error && error_size) error[0] = '\0';
    return 0;
}
