cmake_minimum_required(VERSION 3.16)
project(ios27 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(ios27_core STATIC src/macho.c src/runner.c src/process.c src/darwin_probe.c src/inventory.c src/dyld_paths.c src/exports.c src/binds.c src/chained.c src/images.c src/mach_ipc.c src/mach_v2.c src/mach_guest.c src/darwin_mach_abi.c src/darwin_object.c src/darwin_branch26.c src/darwin_got.c)
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

    add_executable(test_darwin_mach_abi tests/test_darwin_mach_abi.c)
    target_link_libraries(test_darwin_mach_abi PRIVATE ios27_core)
    target_compile_options(test_darwin_mach_abi PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME darwin-fixture-abi-lifecycle COMMAND test_darwin_mach_abi)
    # Compile an SDK-free real ARM64 Darwin relocatable Mach-O object.
    find_program(IOS27_DARWIN_CLANG NAMES clang clang-20 clang-19 clang-18 clang-17)
    if(NOT IOS27_DARWIN_CLANG)
        message(FATAL_ERROR "Clang with arm64-apple-darwin target support is required for phase12 tests")
    endif()
    set(IOS27_DARWIN_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/arm64_darwin_fixture.o")
    add_custom_command(
        OUTPUT "${IOS27_DARWIN_FIXTURE}"
        COMMAND timeout 30s "${IOS27_DARWIN_CLANG}" --target=arm64-apple-darwin
                -ffreestanding -fno-builtin -fno-stack-protector
                -fno-unwind-tables -fno-asynchronous-unwind-tables -O2
                -c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/arm64_darwin_fixture.c"
                -o "${IOS27_DARWIN_FIXTURE}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/arm64_darwin_fixture.c"
        VERBATIM
        COMMENT "Compile SDK-free ARM64 Darwin Mach-O relocatable object")
    add_custom_target(ios27_compiled_darwin_fixture ALL DEPENDS "${IOS27_DARWIN_FIXTURE}")

    add_executable(test_compiled_darwin_object tests/test_compiled_darwin_object.c)
    target_link_libraries(test_compiled_darwin_object PRIVATE ios27_core)
    target_compile_options(test_compiled_darwin_object PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_dependencies(test_compiled_darwin_object ios27_compiled_darwin_fixture)
    add_test(NAME compiled-darwin-object COMMAND test_compiled_darwin_object "${IOS27_DARWIN_FIXTURE}")
    # Phase 13: an independently compiled Darwin object whose external C
    # call forces an actual ARM64_RELOC_BRANCH26 entry in __TEXT,__text.
    set(IOS27_DARWIN_CONSUMER "${CMAKE_CURRENT_BINARY_DIR}/arm64_darwin_consumer.o")
    add_custom_command(
        OUTPUT "${IOS27_DARWIN_CONSUMER}"
        COMMAND timeout 30s "${IOS27_DARWIN_CLANG}" --target=arm64-apple-darwin
                -ffreestanding -fno-builtin -fno-stack-protector
                -fno-unwind-tables -fno-asynchronous-unwind-tables -O2
                -c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/arm64_darwin_consumer.c"
                -o "${IOS27_DARWIN_CONSUMER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/arm64_darwin_consumer.c"
        VERBATIM
        COMMENT "Compile external BRANCH26 relocation in SDK-free ARM64 Darwin object")
    add_custom_target(ios27_compiled_darwin_consumer ALL DEPENDS "${IOS27_DARWIN_CONSUMER}")
    add_executable(test_compiled_darwin_reloc tests/test_compiled_darwin_reloc.c)
    target_link_libraries(test_compiled_darwin_reloc PRIVATE ios27_core)
    target_compile_options(test_compiled_darwin_reloc PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_dependencies(test_compiled_darwin_reloc ios27_compiled_darwin_fixture ios27_compiled_darwin_consumer)
    add_test(NAME compiled-darwin-branch26 COMMAND test_compiled_darwin_reloc
             "${IOS27_DARWIN_FIXTURE}" "${IOS27_DARWIN_CONSUMER}")
    # Phase 14: two real SDK-free Clang MH_OBJECTs with GOT_LOAD_PAGE21/
    # GOT_LOAD_PAGEOFF12 relocations for a cross-object const data reference.
    set(IOS27_DARWIN_DATA_CONSUMER "${CMAKE_CURRENT_BINARY_DIR}/arm64_darwin_data_consumer.o")
    set(IOS27_DARWIN_DATA_PROVIDER "${CMAKE_CURRENT_BINARY_DIR}/arm64_darwin_data_provider.o")
    add_custom_command(
        OUTPUT "${IOS27_DARWIN_DATA_CONSUMER}"
        COMMAND timeout 30s "${IOS27_DARWIN_CLANG}" --target=arm64-apple-darwin
                -ffreestanding -fno-builtin -fno-stack-protector
                -fno-unwind-tables -fno-asynchronous-unwind-tables -O2
                -c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/arm64_darwin_data_consumer.c"
                -o "${IOS27_DARWIN_DATA_CONSUMER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/arm64_darwin_data_consumer.c"
        VERBATIM)
    add_custom_command(
        OUTPUT "${IOS27_DARWIN_DATA_PROVIDER}"
        COMMAND timeout 30s "${IOS27_DARWIN_CLANG}" --target=arm64-apple-darwin
                -ffreestanding -fno-builtin -fno-stack-protector
                -fno-unwind-tables -fno-asynchronous-unwind-tables -O2
                -c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/arm64_darwin_data_provider.c"
                -o "${IOS27_DARWIN_DATA_PROVIDER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/arm64_darwin_data_provider.c"
        VERBATIM)
    add_custom_target(ios27_compiled_darwin_got_objects ALL
                      DEPENDS "${IOS27_DARWIN_DATA_CONSUMER}" "${IOS27_DARWIN_DATA_PROVIDER}")
    add_executable(test_compiled_darwin_got tests/test_compiled_darwin_got.c)
    target_link_libraries(test_compiled_darwin_got PRIVATE ios27_core)
    target_compile_options(test_compiled_darwin_got PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_dependencies(test_compiled_darwin_got ios27_compiled_darwin_got_objects)
    add_test(NAME compiled-darwin-got-load COMMAND test_compiled_darwin_got
             "${IOS27_DARWIN_DATA_CONSUMER}" "${IOS27_DARWIN_DATA_PROVIDER}")

    # Phase 15a: link original freestanding Darwin Clang MH_OBJECTs into a
    # real MH_EXECUTE via the open-source LLVM Mach-O linker, no Apple SDK.
    find_program(IOS27_DARWIN_LD64_LLD NAMES ld64.lld)
    if(NOT IOS27_DARWIN_LD64_LLD)
        message(STATUS "Phase15a: using narrow source linker fallback, LLVM ld64.lld not installed")
        set(IOS27_DARWIN_LD64_LLD "none")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(IOS27_PHASE15A_EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/phase15a-executable")
    add_custom_command(
        OUTPUT "${IOS27_PHASE15A_EXECUTABLE}"
        COMMAND timeout 115s "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase15a-build.py"
            --clang "${IOS27_DARWIN_CLANG}"
            --linker "${IOS27_DARWIN_LD64_LLD}"
            --main "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/phase15a-main.c"
            --helper "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/phase15a-helper.c"
            --out "${IOS27_PHASE15A_EXECUTABLE}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase15a-build.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase15a-link-fallback.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/phase15a-main.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/phase15a-helper.c"
        VERBATIM
        COMMENT "Link genuine SDK-free ARM64 Mach-O MH_EXECUTE")
    add_custom_target(ios27_phase15a_executable ALL
        DEPENDS "${IOS27_PHASE15A_EXECUTABLE}")
    add_executable(test_compiled_darwin_executable
        tests/test_compiled_darwin_executable.c)
    target_link_libraries(test_compiled_darwin_executable PRIVATE ios27_core)
    target_compile_options(test_compiled_darwin_executable PRIVATE
        -Wall -Wextra -Werror -Wpedantic)
    add_dependencies(test_compiled_darwin_executable ios27_phase15a_executable)
    add_test(NAME compiled-darwin-real-mh-execute
        COMMAND test_compiled_darwin_executable "${IOS27_PHASE15A_EXECUTABLE}")

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        add_executable(test_process tests/test_process.c)
        target_link_libraries(test_process PRIVATE ios27_core)
        target_include_directories(test_process PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_options(test_process PRIVATE -Wall -Wextra -Werror -Wpedantic)
        add_test(NAME contained-process-supervisor COMMAND test_process)
        set_tests_properties(contained-process-supervisor PROPERTIES TIMEOUT 15)

        add_executable(test_contained_macho tests/test_contained_macho.c)
        target_link_libraries(test_contained_macho PRIVATE ios27_core)
        target_compile_options(test_contained_macho PRIVATE -Wall -Wextra -Werror -Wpedantic)
        # Keep the original phase19a workload as a build-owned dependency;
        # CTest must not invoke a compiler relative to its working directory.
        set(IOS27_CONTAINED_STARTUP "${CMAKE_CURRENT_BINARY_DIR}/contained-phase19a-process")
        add_custom_command(
            OUTPUT "${IOS27_CONTAINED_STARTUP}"
            COMMAND timeout 75s "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase19a-build.py"
                "${IOS27_CONTAINED_STARTUP}"
            DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase19a-build.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase19a-link-fallback.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/phase19a-process.c"
            VERBATIM
            COMMENT "Link SDK-free ARM64 contained startup fixture")
        add_custom_target(ios27_contained_startup DEPENDS "${IOS27_CONTAINED_STARTUP}")
        add_dependencies(test_contained_macho ios27_phase15a_executable ios27_contained_startup)
        add_test(NAME contained-native-macho COMMAND test_contained_macho
            "${IOS27_PHASE15A_EXECUTABLE}" "${IOS27_CONTAINED_STARTUP}")
        set_tests_properties(contained-native-macho PROPERTIES TIMEOUT 15)
    endif()
endif()
