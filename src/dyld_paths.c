#include "ios27/dyld_paths.h"
#include "ios27/macho.h"

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


static uint32_t load_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

/* Read only a C string located within its own Mach-O load command. */
static int load_command_name(const uint8_t *cmd, size_t command_size,
                             size_t min_offset,
                             char out[IOS27_DYLD_PATH_MAX]) {
    size_t offset = load_u32(cmd + 8u);
    const char *begin;
    const char *end;
    size_t n;
    if (offset < min_offset || offset >= command_size) return -1;
    begin = (const char *)(cmd + offset);
    end = memchr(begin, '\0', command_size - offset);
    if (!end) return -1;
    n = (size_t)(end - begin);
    if (n == 0u || n >= IOS27_DYLD_PATH_MAX) return -1;
    memcpy(out, begin, n);
    out[n] = '\0';
    return 0;
}

static int dependency_command(uint32_t type) {
    return type == IOS27_DYLD_LC_LOAD_DYLIB ||
           type == IOS27_DYLD_LC_LOAD_WEAK_DYLIB ||
           type == IOS27_DYLD_LC_REEXPORT_DYLIB ||
           type == IOS27_DYLD_LC_LOAD_UPWARD_DYLIB ||
           type == IOS27_DYLD_LC_LAZY_LOAD_DYLIB;
}

int ios27_dyld_inspect_image(const uint8_t *image, size_t size,
                             ios27_dyld_image_metadata *out,
                             char *error, size_t error_size) {
    ios27_macho_info parsed;
    ios27_dyld_image_metadata staged;
    size_t pos, end;
    uint32_t count;
    if (!out) return fail(error, error_size, "null dyld metadata output");
    memset(out, 0, sizeof(*out));
    if (!image) return fail(error, error_size, "null dyld image");
    if (ios27_macho_parse(image, size, &parsed, error, error_size))
        return -1;
    (void)parsed; /* validation only, no mapped image or linker state */
    if (size < 32u || load_u32(image + 20u) > size - 32u)
        return fail(error, error_size, "invalid Mach-O command area");
    pos = 32u;
    end = pos + (size_t)load_u32(image + 20u);
    count = load_u32(image + 16u);
    memset(&staged, 0, sizeof(staged));
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t type;
        size_t command_size;
        const uint8_t *cmd;
        if (pos > end || end - pos < 8u)
            return fail(error, error_size, "truncated dyld load command");
        cmd = image + pos;
        type = load_u32(cmd);
        command_size = load_u32(cmd + 4u);
        if (command_size < 8u || command_size % 8u ||
            command_size > end - pos)
            return fail(error, error_size, "invalid dyld load command size");
        if (type == IOS27_DYLD_LC_RPATH) {
            if (command_size < 12u ||
                staged.rpath_count == IOS27_DYLD_MAX_CANDIDATES ||
                load_command_name(cmd, command_size, 12u,
                                  staged.rpaths[staged.rpath_count]))
                return fail(error, error_size, "invalid LC_RPATH");
            ++staged.rpath_count;
        } else if (dependency_command(type)) {
            ios27_dyld_dependency *dep;
            if (command_size < 24u ||
                staged.dependency_count == IOS27_DYLD_MAX_DEPENDENCIES)
                return fail(error, error_size, "invalid dependency count or size");
            dep = &staged.dependencies[staged.dependency_count];
            if (load_command_name(cmd, command_size, 24u, dep->install_name))
                return fail(error, error_size, "invalid dependent install name");
            dep->command = type;
            ++staged.dependency_count;
        } else if (type == IOS27_DYLD_LC_ID_DYLIB) {
            if (command_size < 24u || staged.has_id ||
                load_command_name(cmd, command_size, 24u, staged.id))
                return fail(error, error_size, "invalid LC_ID_DYLIB");
            staged.has_id = 1u;
        }
        pos += command_size;
    }
    if (pos != end)
        return fail(error, error_size, "dyld command count mismatch");
    *out = staged;
    if (error && error_size) error[0] = '\0';
    return 0;
}


int ios27_dyld_match_dependency(const ios27_dyld_image_metadata *metadata,
                                 size_t dependency_index,
                                 const char *loader_image,
                                 const char *main_executable,
                                 const char *const *guest_image_paths,
                                 size_t image_count,
                                 ios27_dyld_catalog_match *out,
                                 char *error, size_t error_size) {
    ios27_dyld_candidates candidates;
    const char *rpaths[IOS27_DYLD_MAX_CANDIDATES];
    char normalized[IOS27_DYLD_MAX_DEPENDENCIES][IOS27_DYLD_PATH_MAX];
    if (!out) return fail(error, error_size, "null catalog match output");
    memset(out, 0, sizeof(*out));
    if (!metadata ||
        metadata->rpath_count > IOS27_DYLD_MAX_CANDIDATES ||
        metadata->dependency_count > IOS27_DYLD_MAX_DEPENDENCIES ||
        dependency_index >= metadata->dependency_count ||
        !dependency_command(metadata->dependencies[dependency_index].command) ||
        image_count > IOS27_DYLD_MAX_DEPENDENCIES ||
        (image_count && !guest_image_paths))
        return fail(error, error_size, "invalid dyld catalog input");
    for (size_t i = 0; i < metadata->rpath_count; ++i)
        rpaths[i] = metadata->rpaths[i];
    if (ios27_dyld_paths(metadata->dependencies[dependency_index].install_name,
                         loader_image, main_executable, rpaths,
                         metadata->rpath_count, &candidates, error, error_size))
        return -1;
    /* Validate entire catalog first, not just entries near a match. */
    for (size_t i = 0; i < image_count; ++i) {
        if (absolute_normalize(guest_image_paths[i], normalized[i]))
            return fail(error, error_size, "unsafe guest image catalog path");
        for (size_t j = 0; j < i; ++j)
            if (strcmp(normalized[i], normalized[j]) == 0)
                return fail(error, error_size, "duplicate guest image catalog path");
    }
    for (size_t candidate = 0; candidate < candidates.count; ++candidate) {
        for (size_t image = 0; image < image_count; ++image) {
            if (strcmp(candidates.paths[candidate], normalized[image]) == 0) {
                out->image_index = image;
                out->candidate_index = candidate;
                (void)snprintf(out->guest_path, sizeof(out->guest_path),
                               "%s", candidates.paths[candidate]);
                if (error && error_size) error[0] = '\0';
                return 0;
            }
        }
    }
    if (error && error_size) error[0] = '\0';
    return 1; /* not available in guest catalog */
}
