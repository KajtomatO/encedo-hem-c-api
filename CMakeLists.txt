cmake_minimum_required(VERSION 3.16)
project(hem C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Expose explicit_bzero and other POSIX extensions under -std=c11
add_compile_definitions(_DEFAULT_SOURCE)

# -------------------------------------------------------------------------
# Dependencies
# -------------------------------------------------------------------------
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# -------------------------------------------------------------------------
# libhem (static library)
# -------------------------------------------------------------------------
add_library(hem STATIC
    src/hem_ctx.c
    src/hem_http.c
    src/hem_json.c
    src/hem_auth.c
    src/hem_system.c
    src/hem_keymgmt.c
    src/hem_crypto.c
    third_party/cJSON/cJSON.c
)

target_include_directories(hem
    PUBLIC  include
    PRIVATE src
            third_party/cJSON
)

target_link_libraries(hem
    PRIVATE CURL::libcurl
            OpenSSL::Crypto
)

# -------------------------------------------------------------------------
# hem_test (MVP test program)
# -------------------------------------------------------------------------
add_executable(hem_test test/hem_test.c)

target_link_libraries(hem_test PRIVATE hem)
