cmake_minimum_required(VERSION 3.22.1)
project(phase26e_android C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_library(phase26e_jni SHARED
    phase26e_jni.c
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../../src/phase26e_host.c")
target_include_directories(phase26e_jni PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../../include")
target_compile_options(phase26e_jni PRIVATE -Wall -Wextra -Wpedantic -Werror)
find_library(log-lib log)
target_link_libraries(phase26e_jni PRIVATE "${log-lib}" m)
