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

    # -------------------------------------------------------------------------
    # Integration tests (require a live device -- not run by default)
    # Run: ctest -L integration
    # -------------------------------------------------------------------------
    set(INTEGRATION_TESTS
        test_system
        test_keymgmt
        test_crypto_aes
        test_crypto_hmac
        test_crypto_ecdh
        test_crypto_exdsa
        test_pqc
        test_logger
    )

    foreach(T IN LISTS INTEGRATION_TESTS)
        add_executable(${T} test/integration/${T}.c)
        target_include_directories(${T} PRIVATE src)
        target_link_libraries(${T} PRIVATE hem ${CMOCKA_LIB})
        add_test(NAME ${T} COMMAND ${T})
        set_tests_properties(${T} PROPERTIES
            LABELS      "integration"
            DISABLED    TRUE
        )
    endforeach()

    # -------------------------------------------------------------------------
    # Destructive tests (irreversible side-effects -- disabled and labelled)
    # Run: ctest -L destructive   (after removing DISABLED from the one you want)
    # -------------------------------------------------------------------------
    set(DESTRUCTIVE_TESTS
        test_config_write
        test_reboot
        test_shutdown
        test_storage
        test_firmware
        test_device_init
    )

    foreach(T IN LISTS DESTRUCTIVE_TESTS)
        add_executable(${T} test/destructive/${T}.c)
        target_include_directories(${T} PRIVATE src)
        target_link_libraries(${T} PRIVATE hem ${CMOCKA_LIB})
        add_test(NAME ${T} COMMAND ${T})
        set_tests_properties(${T} PROPERTIES
            LABELS      "destructive"
            DISABLED    TRUE
        )
    endforeach()

else()
    message(STATUS "CMocka not found -- unit tests disabled (install libcmocka-dev)")
endif()
