# tests/consumer/subdir — the out-of-tree consumer of REQ-BUILD-011: a project
# that adds the SDK's source tree as a subdirectory with the SDK's tests and
# its shared variant switched off and its test support on, and links the static
# SDK plus ehem::test-support. check_subdir_consumer.cmake configures, builds
# and runs it (the `subdir_consumer` CTest, label unit). The shape
# encedo-hem-tool's c/CMakeLists.txt gives the submodule build (c-v1.3.0).
#
# supports: REQ-BUILD-011

cmake_minimum_required(VERSION 3.20)
project(encedo-hem-subdir-consumer C)

set(EHEM_SDK_SOURCE_DIR "" CACHE PATH "the SDK source tree this project adds")
if(NOT EHEM_SDK_SOURCE_DIR)
  message(FATAL_ERROR "EHEM_SDK_SOURCE_DIR not set")
endif()
# The gate passes the SDK's own sanitizer switch through; an instrumented SDK
# needs an instrumented consumer.
option(EHEM_SANITIZE "Build with ASan/UBSan, like the SDK" OFF)
if(EHEM_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# The consumer decides these as NORMAL variables before the add; the SDK's
# option() calls honour them (CMP0077, NEW in the SDK's policy scope).
set(BUILD_TESTING OFF)
set(EHEM_BUILD_SHARED OFF)
set(EHEM_BUILD_TEST_SUPPORT ON)
set(EHEM_TEST_SUPPORT_CRYPTO_REF OFF)    # this consumer's test needs no backend header
add_subdirectory("${EHEM_SDK_SOURCE_DIR}" sdk EXCLUDE_FROM_ALL)

add_executable(subdir_consumer main.c)
target_link_libraries(subdir_consumer PRIVATE
  encedo-hem::encedo-hem-static
  ehem::test-support)
