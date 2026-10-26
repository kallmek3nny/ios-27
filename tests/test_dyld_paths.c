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
    puts("dyld path tests passed");
    return 0;
}
