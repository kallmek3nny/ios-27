cmake_minimum_required(VERSION 3.16)
project(ios27 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(ios27_core STATIC src/macho.c src/runner.c src/process.c src/service_bindings.c src/darwin_probe.c src/inventory.c src/dyld_paths.c src/exports.c src/binds.c src/chained.c src/images.c src/mach_ipc.c src/mach_v2.c src/mach_guest.c src/darwin_mach_abi.c src/darwin_object.c src/darwin_branch26.c src/darwin_got.c)
target_include_directories(ios27_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(ios27_core PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ios27 src/main.c)
target_link_libraries(ios27 PRIVATE ios27_core)
target_compile_options(ios27 PRIVATE -Wall -Wextra -Werror -Wpedantic)

# Original-source bounded userspace model, exposed only by a separate helper.
# The ordinary ios27-loader never links these services or supplies the table.
add_library(ios27_service_bridge STATIC src/service_bridge.c src/libsystem20e_services.c)
target_include_directories(ios27_service_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(ios27_service_bridge PRIVATE -Wall -Wextra -Werror -Wpedantic)

# Trusted helper that ios27_run_executable_exec execveat()s into a fresh
# address space. Linux/Android only; it is never meant to be run directly.
if(CMAKE_SYSTEM_NAME MATCHES "Linux|Android")
    add_executable(ios27-loader src/loader.c)
    target_link_libraries(ios27-loader PRIVATE ios27_core)
    target_compile_options(ios27-loader PRIVATE -Wall -Wextra -Werror -Wpedantic)

    add_executable(ios27-services-loader src/loader.c)
    target_compile_definitions(ios27-services-loader PRIVATE IOS27_WITH_SERVICES=1)
    target_link_libraries(ios27-services-loader PRIVATE ios27_core ios27_service_bridge)
    target_compile_options(ios27-services-loader PRIVATE -Wall -Wextra -Werror -Wpedantic)
    # Resolve libc memory/string helpers before seccomp, avoiding lazy ELF
    # binding while the guest is restricted to the mandatory syscall filter.
    target_link_options(ios27-services-loader PRIVATE "LINKER:-z,now")
endif()

option(IOS27_BUILD_TESTS "Build parser and runtime tests" ON)
if(IOS27_BUILD_TESTS)
    enable_testing()
    add_executable(test_service_bridge tests/test_service_bridge.c)
    target_link_libraries(test_service_bridge PRIVATE ios27_service_bridge)
    target_include_directories(test_service_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(test_service_bridge PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME bounded-service-table COMMAND test_service_bridge)

    add_executable(test_userspace_services tests/phase20e-standalone.c
        tests/phase20e-darwin-fixture.c)
    target_link_libraries(test_userspace_services PRIVATE ios27_service_bridge)
    target_compile_options(test_userspace_services PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME bounded-userspace-services COMMAND test_userspace_services)

    add_library(ios27_shim_host STATIC src/darwin_shim.c)
    target_include_directories(ios27_shim_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(ios27_shim_host PUBLIC IOS27_SHIM_HOST_TEST=1)
    target_compile_options(ios27_shim_host PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_executable(test_darwin_shim tests/test_darwin_shim.c)
    target_include_directories(test_darwin_shim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_darwin_shim PRIVATE ios27_shim_host ios27_service_bridge)
    target_compile_options(test_darwin_shim PRIVATE -Wall -Wextra -Werror -Wpedantic)
    add_test(NAME bounded-named-service-shim COMMAND test_darwin_shim)
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
    add_test(NAME apple-runtime-source-validation
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_apple_runtime_source.py")
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

    # Linux/Android startup tests consume fixtures built here, rather than
    # invoking source-relative shell commands from CTest's working directory.
    set(IOS27_STARTUP_FIXTURES)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        foreach(fixture IN ITEMS phase17a-multisegment phase18a-startup phase19a-process)
            string(REGEX REPLACE "-.*$" "" phase "${fixture}")
            set(fallback "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase17a-link-fallback.py")
            if(phase STREQUAL "phase19a")
                set(fallback "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase19a-link-fallback.py")
            endif()
            set(output "${CMAKE_CURRENT_BINARY_DIR}/${fixture}")
            add_custom_command(
                OUTPUT "${output}"
                COMMAND timeout 75s "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/${phase}-build.py" "${output}"
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/${phase}-build.py"
                    "${fallback}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/${fixture}.c"
                VERBATIM
                COMMENT "Link SDK-free ARM64 startup fixture ${fixture}")
            list(APPEND IOS27_STARTUP_FIXTURES "${output}")
        endforeach()
        add_custom_target(ios27_startup_executables DEPENDS ${IOS27_STARTUP_FIXTURES})
        add_dependencies(test_compiled_darwin_executable ios27_startup_executables)
    endif()
    add_test(NAME compiled-darwin-real-mh-execute
        COMMAND test_compiled_darwin_executable
            "${IOS27_PHASE15A_EXECUTABLE}" ${IOS27_STARTUP_FIXTURES})

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        add_executable(test_process tests/test_process.c)
        target_link_libraries(test_process PRIVATE ios27_core)
        target_include_directories(test_process PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_options(test_process PRIVATE -Wall -Wextra -Werror -Wpedantic)
        add_dependencies(test_process ios27-loader)
        # The Landlock cases exec the project's own trusted loader build output.
        add_test(NAME contained-process-supervisor COMMAND test_process
            "$<TARGET_FILE:ios27-loader>")
        set_tests_properties(contained-process-supervisor PROPERTIES TIMEOUT 15)

        add_executable(test_process_cpu tests/test_process_cpu.c)
        target_link_libraries(test_process_cpu PRIVATE ios27_core)
        target_include_directories(test_process_cpu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_compile_options(test_process_cpu PRIVATE -Wall -Wextra -Werror -Wpedantic)
        add_test(NAME contained-process-cpu-expiry COMMAND test_process_cpu)
        set_tests_properties(contained-process-cpu-expiry PROPERTIES
            TIMEOUT 80 SKIP_RETURN_CODE 77 LABELS "cpu-expiry")

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
        add_dependencies(test_contained_macho ios27_phase15a_executable
            ios27_contained_startup ios27-loader)
        add_dependencies(test_process_cpu ios27_contained_startup ios27-loader)
        # The 16 KiB-granular phase19a fixture also works on 4 KiB hosts.
        # x86_64 reports this separate native-execution test as skipped.
        add_test(NAME contained-native-cpu-expiry COMMAND test_process_cpu
            "${IOS27_CONTAINED_STARTUP}" "$<TARGET_FILE:ios27-loader>")
        set_tests_properties(contained-native-cpu-expiry PROPERTIES
            TIMEOUT 155 SKIP_RETURN_CODE 77 LABELS "cpu-expiry;native-arm64")
        # The exec-mode cases run the project's own trusted loader build output.
        add_test(NAME contained-native-macho COMMAND test_contained_macho
            "${IOS27_PHASE15A_EXECUTABLE}" "${IOS27_CONTAINED_STARTUP}"
            "$<TARGET_FILE:ios27-loader>")
        set_tests_properties(contained-native-macho PROPERTIES TIMEOUT 15)
        set(IOS27_SERVICE_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/service-bridge-executable")
        set(IOS27_SERVICE_FALLBACK_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/service-bridge-fallback")
        # The service fixture accepts both supported compiler-linker outputs.
        # Keep this discovery separate from older fixture profiles, whose
        # tests intentionally expect their established load-command shapes.
        find_program(IOS27_SERVICE_LD64_LLD NAMES ld64.lld ld64.lld-21 ld64.lld-20
            ld64.lld-19 ld64.lld-18 ld64.lld-17)
        if(NOT IOS27_SERVICE_LD64_LLD)
            set(IOS27_SERVICE_LD64_LLD "none")
        endif()
        set(IOS27_SHIM_CLI_ARGUMENT)
        if(NOT IOS27_SERVICE_LD64_LLD STREQUAL "none")
            set(IOS27_SHIM_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/service-shim-executable")
            set(IOS27_SHIM_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/service-shim-symbols.json")
            set(IOS27_SHIM_CLI_ARGUMENT "${IOS27_SHIM_FIXTURE}")
            add_custom_command(
                OUTPUT "${IOS27_SHIM_FIXTURE}" "${IOS27_SHIM_MANIFEST}"
                COMMAND timeout 75s "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/service-shim-build.py"
                    --clang "${IOS27_DARWIN_CLANG}"
                    --linker "${IOS27_SERVICE_LD64_LLD}"
                    --out "${IOS27_SHIM_FIXTURE}" --manifest "${IOS27_SHIM_MANIFEST}"
                DEPENDS
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/service-shim-build.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase19a-build.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/service-shim-client.c"
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/darwin_shim.c"
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/darwin_shim_internal.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/include/ios27/darwin_shim.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/include/ios27/service_bindings.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/include/ios27/service_bridge.h"
                VERBATIM COMMENT "Audit and link original named-service Darwin client and shim")
            add_custom_target(ios27_service_shim ALL
                DEPENDS "${IOS27_SHIM_FIXTURE}" "${IOS27_SHIM_MANIFEST}")
        endif()
        add_test(NAME contained-process-cli
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_process_cli.py"
                "$<TARGET_FILE:ios27>" "$<TARGET_FILE:ios27-loader>"
                "${IOS27_CONTAINED_STARTUP}" "$<TARGET_FILE:ios27-services-loader>"
                "${IOS27_SERVICE_FIXTURE}" ${IOS27_SHIM_CLI_ARGUMENT})
        set_tests_properties(contained-process-cli PROPERTIES TIMEOUT 30)

        add_custom_command(
            OUTPUT "${IOS27_SERVICE_FIXTURE}" "${IOS27_SERVICE_FALLBACK_FIXTURE}"
            COMMAND timeout 75s "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/service-bridge-build.py"
                --clang "${IOS27_DARWIN_CLANG}"
                --linker "${IOS27_SERVICE_LD64_LLD}"
                --out "${IOS27_SERVICE_FIXTURE}"
                --fallback-out "${IOS27_SERVICE_FALLBACK_FIXTURE}"
            DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/service-bridge-build.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase19a-build.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/phase19a-link-fallback.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/ios27/service_bridge.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/service-bridge.c"
            VERBATIM COMMENT "Link original SDK-free ARM64 service bridge fixture")
        add_custom_target(ios27_service_fixture ALL DEPENDS
            "${IOS27_SERVICE_FIXTURE}" "${IOS27_SERVICE_FALLBACK_FIXTURE}")

        add_executable(test_service_exec tests/test_service_exec.c tests/fixtures/service-bridge.c)
        set_source_files_properties(tests/fixtures/service-bridge.c PROPERTIES
            COMPILE_DEFINITIONS IOS27_SERVICE_ENTRY=ios27_service_fixture_entry)
        target_include_directories(test_service_exec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(test_service_exec PRIVATE ios27_core ios27_service_bridge)
        target_compile_options(test_service_exec PRIVATE -Wall -Wextra -Werror -Wpedantic)
        target_link_options(test_service_exec PRIVATE "LINKER:-z,now")
        add_dependencies(test_service_exec ios27_service_fixture ios27-loader
            ios27-services-loader ios27_contained_startup)
        add_test(NAME contained-service-callbacks COMMAND test_service_exec)
        add_test(NAME contained-native-services COMMAND test_service_exec
            "${IOS27_SERVICE_FIXTURE}" "$<TARGET_FILE:ios27-loader>"
            "$<TARGET_FILE:ios27-services-loader>" "${IOS27_CONTAINED_STARTUP}")
        add_test(NAME contained-native-services-fallback COMMAND test_service_exec
            "${IOS27_SERVICE_FALLBACK_FIXTURE}" "$<TARGET_FILE:ios27-loader>"
            "$<TARGET_FILE:ios27-services-loader>" "${IOS27_CONTAINED_STARTUP}")
        set_tests_properties(contained-service-callbacks PROPERTIES TIMEOUT 15)
        set_tests_properties(contained-native-services contained-native-services-fallback
            PROPERTIES TIMEOUT 30
            SKIP_RETURN_CODE 77 LABELS "native-arm64;services")
        if(TARGET ios27_service_shim)
            add_dependencies(test_service_exec ios27_service_shim)
            add_test(NAME contained-native-service-shim COMMAND test_service_exec
                "${IOS27_SHIM_FIXTURE}" "$<TARGET_FILE:ios27-loader>"
                "$<TARGET_FILE:ios27-services-loader>" "${IOS27_CONTAINED_STARTUP}")
        else()
            add_test(NAME contained-native-service-shim COMMAND test_service_exec --no-linker)
        endif()
        set_tests_properties(contained-native-service-shim PROPERTIES TIMEOUT 30
            SKIP_RETURN_CODE 77 LABELS "native-arm64;services")

        add_executable(test_service_bindings tests/test_service_bindings.c)
        target_link_libraries(test_service_bindings PRIVATE ios27_core)
        target_compile_options(test_service_bindings PRIVATE -Wall -Wextra -Werror -Wpedantic)
        add_dependencies(test_service_bindings ios27_service_fixture ios27-services-loader)
        if(TARGET ios27_service_shim)
            add_dependencies(test_service_bindings ios27_service_shim)
            add_test(NAME service-binding-metadata COMMAND test_service_bindings
                "${IOS27_SHIM_FIXTURE}" "${IOS27_SERVICE_FIXTURE}"
                "$<TARGET_FILE:ios27-services-loader>")
        else()
            add_test(NAME service-binding-metadata COMMAND test_service_bindings --no-linker)
        endif()
        set_tests_properties(service-binding-metadata PROPERTIES TIMEOUT 15 SKIP_RETURN_CODE 77)
        add_executable(test_static_rebases tests/test_static_rebases.c)
        target_link_libraries(test_static_rebases PRIVATE ios27_core)
        target_compile_options(test_static_rebases PRIVATE -Wall -Wextra -Werror -Wpedantic)
        if(TARGET ios27_service_shim)
            add_dependencies(test_static_rebases ios27_service_shim ios27-services-loader)
            add_test(NAME static-executable-rebases COMMAND test_static_rebases
                "${IOS27_SHIM_FIXTURE}" "$<TARGET_FILE:ios27-services-loader>")
        else()
            add_test(NAME static-executable-rebases COMMAND test_static_rebases --no-linker)
        endif()
        set_tests_properties(static-executable-rebases PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
    endif()
endif()
