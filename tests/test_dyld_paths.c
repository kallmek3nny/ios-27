#include "ios27/dyld_paths.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>


#define IMAGE_BYTES 2048u
static void image_put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(v >> (8u * i));
}
static void create_image(uint8_t *image) {
    memset(image, 0, IMAGE_BYTES);
    image_put32(image, 0xfeedfacfu);
    image_put32(image + 4u, 0x0100000cu); /* ARM64 */
    image_put32(image + 12u, 6u);        /* MH_DYLIB */
}
static size_t append_name_command(uint8_t *image, size_t pos,
                                   uint32_t type, const char *name) {
    size_t prefix = type == IOS27_DYLD_LC_RPATH ? 12u : 24u;
    size_t length = strlen(name) + 1u;
    size_t bytes = (prefix + length + 7u) & ~(size_t)7u;
    assert(pos + bytes <= IMAGE_BYTES);
    image_put32(image + pos, type);
    image_put32(image + pos + 4u, (uint32_t)bytes);
    image_put32(image + pos + 8u, (uint32_t)prefix);
    memcpy(image + pos + prefix, name, length);
    return pos + bytes;
}
static void finish_image(uint8_t *image, size_t end, unsigned count) {
    image_put32(image + 16u, count);
    image_put32(image + 20u, (uint32_t)(end - 32u));
}
static void test_load_command_inspection(void) {
    uint8_t image[IMAGE_BYTES];
    ios27_dyld_image_metadata out;
    char error[128];
    size_t p, bad;

    create_image(image);
    p = append_name_command(image, 32u, IOS27_DYLD_LC_RPATH,
                            "@executable_path/Frameworks");
    p = append_name_command(image, p, IOS27_DYLD_LC_RPATH,
                            "@loader_path/../Frameworks");
    p = append_name_command(image, p, IOS27_DYLD_LC_LOAD_DYLIB,
                            "@rpath/First.framework/First");
    p = append_name_command(image, p, IOS27_DYLD_LC_LOAD_WEAK_DYLIB,
                            "/usr/lib/libOptional.dylib");
    p = append_name_command(image, p, IOS27_DYLD_LC_REEXPORT_DYLIB,
                            "@rpath/Reexport.framework/Reexport");
    p = append_name_command(image, p, IOS27_DYLD_LC_LOAD_UPWARD_DYLIB,
                            "@rpath/Parent.framework/Parent");
    p = append_name_command(image, p, IOS27_DYLD_LC_LAZY_LOAD_DYLIB,
                            "@rpath/Lazy.framework/Lazy");
    p = append_name_command(image, p, IOS27_DYLD_LC_ID_DYLIB,
                            "@rpath/Host.framework/Host");
    finish_image(image, p, 8u);
    assert(ios27_dyld_inspect_image(image, sizeof(image), &out,
                                   error, sizeof(error)) == 0);
    assert(error[0] == '\0' && out.has_id && out.rpath_count == 2u &&
           out.dependency_count == 5u);
    assert(strcmp(out.id, "@rpath/Host.framework/Host") == 0);
    assert(strcmp(out.rpaths[0], "@executable_path/Frameworks") == 0);
    assert(strcmp(out.rpaths[1], "@loader_path/../Frameworks") == 0);
    assert(out.dependencies[0].command == IOS27_DYLD_LC_LOAD_DYLIB &&
           strcmp(out.dependencies[0].install_name,
                  "@rpath/First.framework/First") == 0);
    assert(out.dependencies[1].command == IOS27_DYLD_LC_LOAD_WEAK_DYLIB);
    assert(out.dependencies[2].command == IOS27_DYLD_LC_REEXPORT_DYLIB);
    assert(out.dependencies[3].command == IOS27_DYLD_LC_LOAD_UPWARD_DYLIB);
    assert(out.dependencies[4].command == IOS27_DYLD_LC_LAZY_LOAD_DYLIB);

    /* Invalid late command offsets and unterminated names reject atomically. */
    bad = p - 48u; /* use a separately built short fixture below instead */
    (void)bad;
    create_image(image);
    p = append_name_command(image, 32u, IOS27_DYLD_LC_RPATH, "/Valid");
    bad = p;
    p = append_name_command(image, p, IOS27_DYLD_LC_LOAD_DYLIB, "/Broken");
    finish_image(image, p, 2u);
    image_put32(image + bad + 8u, 23u);
    assert(ios27_dyld_inspect_image(image, sizeof(image), &out,
                                   error, sizeof(error)) == -1);
    assert(out.rpath_count == 0u && out.dependency_count == 0u &&
           error[0] != '\0');
    image_put32(image + bad + 8u, 24u);
    memset(image + bad + 24u, 'x', p - bad - 24u);
    assert(ios27_dyld_inspect_image(image, sizeof(image), &out,
                                   error, sizeof(error)) == -1);
    assert(out.rpath_count == 0u);
    image_put32(image + bad + 8u, (uint32_t)(p - bad));
    assert(ios27_dyld_inspect_image(image, sizeof(image), &out,
                                   error, sizeof(error)) == -1);

    create_image(image);
    p = append_name_command(image, 32u, IOS27_DYLD_LC_ID_DYLIB, "/First");
    p = append_name_command(image, p, IOS27_DYLD_LC_ID_DYLIB, "/Second");
    finish_image(image, p, 2u);
    assert(ios27_dyld_inspect_image(image, sizeof(image), &out,
                                   error, sizeof(error)) == -1);
    assert(!out.has_id);

    create_image(image);
    p = 32u;
    for (unsigned i = 0u; i <= IOS27_DYLD_MAX_CANDIDATES; ++i)
        p = append_name_command(image, p, IOS27_DYLD_LC_RPATH, "/R");
    finish_image(image, p, IOS27_DYLD_MAX_CANDIDATES + 1u);
    assert(ios27_dyld_inspect_image(image, sizeof(image), &out,
                                   error, sizeof(error)) == -1);
    assert(out.rpath_count == 0u);

    create_image(image);
    p = 32u;
    for (unsigned i = 0u; i <= IOS27_DYLD_MAX_DEPENDENCIES; ++i)
        p = append_name_command(image, p, IOS27_DYLD_LC_LOAD_DYLIB, "/D");
    finish_image(image, p, IOS27_DYLD_MAX_DEPENDENCIES + 1u);
    assert(ios27_dyld_inspect_image(image, sizeof(image), &out,
                                   error, sizeof(error)) == -1);
    assert(out.dependency_count == 0u);

    create_image(image);
    p = append_name_command(image, 32u, IOS27_DYLD_LC_RPATH, "/R");
    finish_image(image, p, 2u); /* command count mismatch */
    assert(ios27_dyld_inspect_image(image, sizeof(image), &out,
                                   error, sizeof(error)) == -1);
    assert(ios27_dyld_inspect_image(NULL, 0u, &out,
                                   error, sizeof(error)) == -1);
    assert(ios27_dyld_inspect_image(image, sizeof(image), NULL,
                                   error, sizeof(error)) == -1);
}


static void test_guest_catalog(void) {
    uint8_t image[IMAGE_BYTES];
    ios27_dyld_image_metadata meta;
    ios27_dyld_catalog_match match;
    char error[128];
    const char *catalog[] = {
        "/App/Frameworks/Z.framework/Z",
        "/System/Library/Frameworks/Z.framework/Z",
        "/App/Frameworks/Other.framework/Other"
    };
    const char *second[] = { "/System/Library/Frameworks/Z.framework/Z" };
    const char *dup[] = {
        "/App/Frameworks/Z.framework/Z",
        "/App//Frameworks/./Z.framework/Z"
    };
    const char *bad[] = { "/App/Frameworks/Z.framework/Z", "/../../unsafe" };
    size_t p;
    create_image(image);
    p = append_name_command(image, 32u, IOS27_DYLD_LC_RPATH,
                            "@executable_path/Frameworks");
    p = append_name_command(image, p, IOS27_DYLD_LC_RPATH,
                            "/System/Library/Frameworks");
    p = append_name_command(image, p, IOS27_DYLD_LC_LOAD_WEAK_DYLIB,
                            "@rpath/Z.framework/Z");
    finish_image(image, p, 3u);
    assert(ios27_dyld_inspect_image(image, sizeof(image), &meta,
                                   error, sizeof(error)) == 0);
    assert(ios27_dyld_match_dependency(&meta, 0u, "/App/Z", "/App/Main",
                                       catalog, 3u, &match, error,
                                       sizeof(error)) == 0);
    assert(match.image_index == 0u && match.candidate_index == 0u);
    assert(strcmp(match.guest_path, catalog[0]) == 0);
    assert(ios27_dyld_match_dependency(&meta, 0u, "/App/Z", "/App/Main",
                                       second, 1u, &match, error,
                                       sizeof(error)) == 0);
    assert(match.image_index == 0u && match.candidate_index == 1u);
    assert(strcmp(match.guest_path, second[0]) == 0);
    assert(ios27_dyld_match_dependency(&meta, 0u, "/App/Z", "/App/Main",
                                       NULL, 0u, &match, error,
                                       sizeof(error)) == 1);
    assert(match.guest_path[0] == '\0' && error[0] == '\0');
    assert(ios27_dyld_match_dependency(&meta, 0u, "/App/Z", "/App/Main",
                                       dup, 2u, &match, error,
                                       sizeof(error)) == -1);
    assert(match.guest_path[0] == '\0');
    assert(ios27_dyld_match_dependency(&meta, 0u, "/App/Z", "/App/Main",
                                       bad, 2u, &match, error,
                                       sizeof(error)) == -1);
    assert(match.guest_path[0] == '\0');

    /* Invalid late runpath rolls back even after a first candidate matched. */
    strcpy(meta.rpaths[1], "@rpath/nested");
    assert(ios27_dyld_match_dependency(&meta, 0u, "/App/Z", "/App/Main",
                                       catalog, 3u, &match, error,
                                       sizeof(error)) == -1);
    assert(match.guest_path[0] == '\0');
    strcpy(meta.rpaths[1], "/System/Library/Frameworks");

    assert(ios27_dyld_match_dependency(&meta, 1u, "/App/Z", "/App/Main",
                                       catalog, 3u, &match, error,
                                       sizeof(error)) == -1);
    meta.dependency_count = IOS27_DYLD_MAX_DEPENDENCIES + 1u;
    assert(ios27_dyld_match_dependency(&meta, 0u, "/App/Z", "/App/Main",
                                       catalog, 3u, &match, error,
                                       sizeof(error)) == -1);
    meta.dependency_count = 1u;
    meta.rpath_count = IOS27_DYLD_MAX_CANDIDATES + 1u;
    assert(ios27_dyld_match_dependency(&meta, 0u, "/App/Z", "/App/Main",
                                       catalog, 3u, &match, error,
                                       sizeof(error)) == -1);
    meta.rpath_count = 2u;
    assert(ios27_dyld_match_dependency(&meta, 0u, "/App/Z", "/App/Main",
                                       catalog, IOS27_DYLD_MAX_DEPENDENCIES + 1u,
                                       &match, error, sizeof(error)) == -1);
    assert(ios27_dyld_match_dependency(&meta, 0u, "/App/Z", "/App/Main",
                                       catalog, 3u, NULL, error,
                                       sizeof(error)) == -1);
}

int main(void) {
    ios27_dyld_candidates out;
    char error[128];
    const char *loader = "/Applications/Test.app/Frameworks/Tool.framework/Tool";
    const char *exe = "/Applications/Test.app/Test";
    const char *runs[] = {
        "@loader_path/../..", "@executable_path/Frameworks",
        "/System/Library/Frameworks", "@executable_path/Frameworks"
    };
    assert(ios27_dyld_paths("/usr/lib/libSystem.B.dylib", loader, exe,
                            runs, 4, &out, error, sizeof(error)) == 0);
    assert(out.count == 1 &&
           strcmp(out.paths[0], "/usr/lib/libSystem.B.dylib") == 0);
    assert(ios27_dyld_paths("@loader_path/../X.dylib", loader, exe,
                            runs, 4, &out, error, sizeof(error)) == 0);
    assert(out.count == 1 &&
           strcmp(out.paths[0], "/Applications/Test.app/Frameworks/X.dylib") == 0);
    assert(ios27_dyld_paths("@executable_path/Frameworks/A.dylib", loader, exe,
                            runs, 4, &out, error, sizeof(error)) == 0);
    assert(strcmp(out.paths[0], "/Applications/Test.app/Frameworks/A.dylib") == 0);
    assert(ios27_dyld_paths("@rpath/UIKit.framework/UIKit", loader, exe,
                            runs, 4, &out, error, sizeof(error)) == 0);
    assert(out.count == 3);
    assert(strcmp(out.paths[0], "/Applications/Test.app/UIKit.framework/UIKit") == 0);
    assert(strcmp(out.paths[1], "/Applications/Test.app/Frameworks/UIKit.framework/UIKit") == 0);
    assert(strcmp(out.paths[2], "/System/Library/Frameworks/UIKit.framework/UIKit") == 0);
    assert(ios27_dyld_paths("@rpath/One.dylib", loader, exe, NULL, 0,
                            &out, error, sizeof(error)) != 0);
    assert(ios27_dyld_paths("relative.dylib", loader, exe,
                            runs, 4, &out, error, sizeof(error)) != 0);
    assert(ios27_dyld_paths("@rpath/../../../../../../dev/block/bad", loader, exe,
                            runs, 4, &out, error, sizeof(error)) != 0);
    assert(ios27_dyld_paths("/../../etc", loader, exe,
                            runs, 4, &out, error, sizeof(error)) != 0);

    /* Canonicalization is lexical, scoped to the virtual guest root. */
    assert(ios27_dyld_paths("/A//B/../C/./D", loader, exe,
                            NULL, 0, &out, error, sizeof(error)) == 0);
    assert(out.count == 1 && strcmp(out.paths[0], "/A/C/D") == 0);
    assert(ios27_dyld_paths("@loader_path/X", "/A/B/../C/Loader", exe,
                            NULL, 0, &out, error, sizeof(error)) == 0);
    assert(strcmp(out.paths[0], "/A/C/X") == 0);
    assert(ios27_dyld_paths("@executable_path/X", loader, "/Main",
                            NULL, 0, &out, error, sizeof(error)) == 0);
    assert(strcmp(out.paths[0], "/X") == 0);

    /* Repeated rpaths collapse in encounter order, including lexical aliases. */
    {
        const char *repeated[] = { "/A/B", "/A//B", "/A/C/../B", "/Z" };
        assert(ios27_dyld_paths("@rpath/Lib.dylib", loader, exe,
                                repeated, 4, &out, error, sizeof(error)) == 0);
        assert(out.count == 2);
        assert(strcmp(out.paths[0], "/A/B/Lib.dylib") == 0);
        assert(strcmp(out.paths[1], "/Z/Lib.dylib") == 0);
    }

    /* No partial success after an invalid later runpath. */
    {
        const char *broken[] = { "/Good", "@rpath/nested" };
        assert(ios27_dyld_paths("@rpath/X", loader, exe,
                                broken, 2, &out, error, sizeof(error)) == -1);
        assert(out.count == 0 && error[0] != '\0');
    }
    assert(ios27_dyld_paths("@loader_path/", loader, exe,
                            NULL, 0, &out, error, sizeof(error)) == -1);
    assert(out.count == 0);
    assert(ios27_dyld_paths("/ok", "relative", exe,
                            NULL, 0, &out, error, sizeof(error)) == 0);
    assert(ios27_dyld_paths("@loader_path/X", "relative", exe,
                            NULL, 0, &out, error, sizeof(error)) == -1);
    assert(out.count == 0);

    /* Input caps are strict and never read past the path protocol bound. */
    {
        char oversized[IOS27_DYLD_PATH_MAX + 2u];
        memset(oversized, 'x', sizeof(oversized));
        oversized[0] = '/';
        oversized[sizeof(oversized) - 1u] = '\0';
        assert(ios27_dyld_paths(oversized, loader, exe,
                                NULL, 0, &out, error, sizeof(error)) == -1);
        assert(out.count == 0);
    }
    assert(ios27_dyld_paths("/ok", loader, exe,
                            NULL, IOS27_DYLD_MAX_CANDIDATES + 1u,
                            &out, error, sizeof(error)) == -1);
    assert(out.count == 0);
    assert(ios27_dyld_paths("@rpath/X", loader, exe,
                            NULL, 1u, &out, error, sizeof(error)) == -1);
    assert(out.count == 0);
    assert(ios27_dyld_paths("/ok", loader, exe,
                            NULL, 0u, NULL, error, sizeof(error)) == -1);
    assert(ios27_dyld_paths("/ok", loader, exe,
                            NULL, 0u, &out, NULL, 0u) == 0);
    test_load_command_inspection();
    test_guest_catalog();
    puts("dyld path tests passed");
    return 0;
}
