cmake_minimum_required(VERSION 3.22.1)
project(phase27e_native_host C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../../")
add_library(phase27e_host SHARED
    phase27e_jni.c
    "${PROJECT_ROOT}/src/phase26e_host.c"
    "${PROJECT_ROOT}/src/phase27e_demo.c")
target_include_directories(phase27e_host PRIVATE "${PROJECT_ROOT}/include")
target_compile_options(phase27e_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(phase27e_host PRIVATE "-Wl,-z,max-page-size=16384")
target_link_libraries(phase27e_host PRIVATE m)
