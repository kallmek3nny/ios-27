#include "ios27/dyld_paths.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    puts("dyld path tests passed");
    return 0;
}
