cmake_minimum_required(VERSION 3.16)
project(ios27 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(ios27_core STATIC src/macho.c src/runner.c src/darwin_probe.c src/inventory.c src/dyld_paths.c src/exports.c src/binds.c src/chained.c src/images.c src/mach_ipc.c src/mach_v2.c src/mach_guest.c)
target_include_directories(ios27_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(ios27_core PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ios27 src/main.c)
target_link_libraries(ios27 PRIVATE ios27_core)
target_compile_options(ios27 PRIVATE -Wall -Wextra -Werror -Wpedantic)

option(IOS27_BUILD_TESTS "Build parser and runtime tests" ON)
if(IOS27_BUILD_TESTS)
    enable_testing()
    add_executable(test_macho tests/test_macho.c)
    target_link_libraries(test_macho PRIVATE ios27_core)
    target_compile_options(test_macho PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME macho-parser COMMAND test_macho)

    add_executable(test_darwin_probe tests/test_darwin_probe.c)
    target_link_libraries(test_darwin_probe PRIVATE ios27_core)
    target_compile_options(test_darwin_probe PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME darwin-svc-probe COMMAND test_darwin_probe)

    add_executable(test_inventory tests/test_inventory.c)
    target_link_libraries(test_inventory PRIVATE ios27_core)
    target_compile_options(test_inventory PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME macho-inventory COMMAND test_inventory)

    add_executable(test_dyld_paths tests/test_dyld_paths.c)
    target_link_libraries(test_dyld_paths PRIVATE ios27_core)
    target_compile_options(test_dyld_paths PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME dyld-virtual-paths COMMAND test_dyld_paths)

    add_executable(test_exports tests/test_exports.c)
    target_link_libraries(test_exports PRIVATE ios27_core)
    target_compile_options(test_exports PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME dyld-export-trie COMMAND test_exports)

    add_executable(test_binds tests/test_binds.c)
    target_link_libraries(test_binds PRIVATE ios27_core)
    target_compile_options(test_binds PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME dyld-classic-binding COMMAND test_binds)

    add_executable(test_chained tests/test_chained.c)
    target_link_libraries(test_chained PRIVATE ios27_core)
    target_compile_options(test_chained PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME dyld-chained-fixups COMMAND test_chained)

    add_executable(test_images tests/test_images.c)
    target_link_libraries(test_images PRIVATE ios27_core)
    target_compile_options(test_images PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME native-multi-image-wx COMMAND test_images)

    add_executable(test_mach_ipc tests/test_mach_ipc.c)
    target_link_libraries(test_mach_ipc PRIVATE ios27_core)
    target_compile_options(test_mach_ipc PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME synthetic-mach-ipc COMMAND test_mach_ipc)

    add_executable(test_mach_v2 tests/test_mach_v2.c)
    target_link_libraries(test_mach_v2 PRIVATE ios27_core)
    target_compile_options(test_mach_v2 PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME task-local-mach-names COMMAND test_mach_v2)

    add_executable(test_mach_guest tests/test_mach_guest.c)
    target_link_libraries(test_mach_guest PRIVATE ios27_core)
    target_compile_options(test_mach_guest PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME bounded-mach-guest-memory COMMAND test_mach_guest)
endif()
