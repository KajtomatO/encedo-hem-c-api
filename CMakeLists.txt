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
    src/hem_pqc.c
    src/hem_logger.c
    src/hem_storage.c
    src/hem_upgrade.c
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

# -------------------------------------------------------------------------
# Unit tests (CMocka, no device required)
# -------------------------------------------------------------------------
find_library(CMOCKA_LIB cmocka)
find_path(CMOCKA_INCLUDE cmocka.h)

if(CMOCKA_LIB AND CMOCKA_INCLUDE)
    enable_testing()

    # test_json: base64 and JSON helper unit tests
    add_executable(test_json test/unit/test_json.c)
    target_include_directories(test_json PRIVATE src third_party/cJSON)
    target_link_libraries(test_json PRIVATE hem ${CMOCKA_LIB})
    add_test(NAME unit_json COMMAND test_json)

    # test_auth_ejwt: eJWT construction unit tests
    add_executable(test_auth_ejwt test/unit/test_auth_ejwt.c)
    target_include_directories(test_auth_ejwt PRIVATE src third_party/cJSON)
    target_link_libraries(test_auth_ejwt PRIVATE hem ${CMOCKA_LIB} OpenSSL::Crypto)
    add_test(NAME unit_auth_ejwt COMMAND test_auth_ejwt)
else()
    message(STATUS "CMocka not found -- unit tests disabled (install libcmocka-dev)")
endif()
