cmake_minimum_required(VERSION 3.16)
project(ios27 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(ios27_core STATIC src/macho.c src/runner.c src/darwin_probe.c)
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
endif()
