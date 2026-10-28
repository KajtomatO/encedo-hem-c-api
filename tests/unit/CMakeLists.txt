# Offline unit tests (label `unit`).

ehem_add_unit_test(test_version SOURCES test_version.c)

# REQ-BUILD-003: JSON round-trips through the internal helper layer.
ehem_add_unit_test(test_json SOURCES test_json.c)

# REQ-API-001..004: context lifecycle, options, error enum, last-error.
ehem_add_unit_test(test_context SOURCES test_context.c)

# REQ-API-010: crypto-backend compatibility check at ehem_global_init — the
# seam-forced refusals, implicit-init propagation, idempotency across a refusal.
ehem_add_unit_test(test_backend_compat SOURCES test_backend_compat.c)

# REQ-API-011: build information. The expected values come from the build's
# own dependency discovery (pkg-config / FindOpenSSL, FindCURL, the link-mode
# detection in the top-level CMakeLists.txt) — no test includes a backend header.
ehem_add_unit_test(test_build_info SOURCES test_build_info.c)
target_compile_definitions(test_build_info PRIVATE
  EHEM_TEST_CRYPTO_BACKEND="${EHEM_CRYPTO_NAME}"
  EHEM_TEST_CRYPTO_VERSION="${EHEM_CRYPTO_VERSION}"
  EHEM_TEST_CRYPTO_LINK="${EHEM_CRYPTO_LINK}"
  EHEM_TEST_TRANSPORT_VERSION="${EHEM_CURL_VERSION}"
  EHEM_TEST_TRANSPORT_LINK="${EHEM_TRANSPORT_LINK}")

# REQ-NET-001, REQ-TEST-001: transport vtable seam via the injected fake.
ehem_add_unit_test(test_transport SOURCES test_transport.c)

# REQ-NET-002..004: default (libcurl) transport error mapping + option plumbing.
ehem_add_unit_test(test_transport_curl SOURCES test_transport_curl.c)

# REQ-SYS-001, REQ-SYS-002, REQ-API-005: system status/version bindings.
ehem_add_unit_test(test_system SOURCES test_system.c)

# REQ-SYS-003, REQ-NET-005: check-in flow + automatic certificate recovery.
ehem_add_unit_test(test_checkin SOURCES test_checkin.c)

# REQ-SYS-006: public cert-inspection helper (serial + validity) — what
# hem-tool's cert-install skip-if-current builds on (encedo-hem-tool).
ehem_add_unit_test(test_cert SOURCES test_cert.c)

# REQ-TEST-006: simulated ExtAuth authenticator — shim AES-128-CBC vector,
# scheme-A codec (round-trip, firmware-construction cross-check, tamper),
# HS256 JWT build/open/peek (M8).
ehem_add_unit_test(test_ext_sim SOURCES test_ext_sim.c)

# REQ-AUTH-006: ExtAuth pairing bindings init/validate/mac (M8).
ehem_add_unit_test(test_ext SOURCES test_ext.c)

# REQ-AUTH-008: notification-broker client (M8).
ehem_add_unit_test(test_notify SOURCES test_notify.c)

# REQ-AUTH-009: mobile confirmation engine — pollable + blocking wait (M8).
ehem_add_unit_test(test_confirm SOURCES test_confirm.c)

# REQ-AUTH-010: mobile login mode via the ensure-token chokepoint (M8).
ehem_add_unit_test(test_mobile SOURCES test_mobile.c)

# The 18 hem-tool unit tests, the forced-mismatch CLI run and their helpers
# moved to encedo-hem-tool (c/tests/unit) with the tool — M13, 2026-10-28.

# REQ-SYS-004, REQ-SYS-005: config GET / cert install / reboot bindings.
ehem_add_unit_test(test_config SOURCES test_config.c)

# REQ-KEY-001: keymgmt list binding + full-repository walk (pagination).
ehem_add_unit_test(test_keymgmt SOURCES test_keymgmt.c)

# REQ-KEY-006: key-type classifier (pure — no transport, no crypto backend).
ehem_add_unit_test(test_keytype SOURCES test_keytype.c)

# REQ-OPS-001: exdsa sign binding — body/scope/decode/error mapping.
ehem_add_unit_test(test_sign SOURCES test_sign.c)

# REQ-OPS-003: exdsa verify binding — body/empty-200/scope-share/error mapping.
ehem_add_unit_test(test_verify SOURCES test_verify.c)

# REQ-OPS-004: ecdh binding — peer selection, body bytes, decode, errors.
ehem_add_unit_test(test_ecdh SOURCES test_ecdh.c)

# REQ-OPS-005: hmac hash/verify bindings — flows, bodies, bounds, errors.
ehem_add_unit_test(test_hmac SOURCES test_hmac.c)

# REQ-OPS-006: cipher encrypt/decrypt bindings — bodies, shapes, bounds.
ehem_add_unit_test(test_cipher SOURCES test_cipher.c)

# REQ-OPS-007/008: ML-KEM + ML-DSA bindings incl. the raw-status quirk.
ehem_add_unit_test(test_pqc SOURCES test_pqc.c)

# REQ-OPS-002: ehem_random encrypt-IV harvest — request count, concat, tail.
ehem_add_unit_test(test_random SOURCES test_random.c)

# REQ-AUTH-001: crypto shim (PBKDF2 / HMAC-SHA256 / X25519) against RFC vectors.
ehem_add_unit_test(test_crypto SOURCES test_crypto.c)

# REQ-AUTH-001: base64/base64url codecs + eJWT builder vs the python fixture.
ehem_add_unit_test(test_ejwt SOURCES test_ejwt.c)

# REQ-AUTH-001, REQ-AUTH-002: login sequence + scope-keyed token cache.
ehem_add_unit_test(test_auth SOURCES test_auth.c)

# REQ-AUTH-012: BIP39 mnemonic → the Manager's master secret (fixtures produced
# by the Manager's own JavaScript).
ehem_add_unit_test(test_bip39 SOURCES test_bip39.c)

# REQ-AUTH-011: device initialisation binding via the fake transport (M10).
ehem_add_unit_test(test_init SOURCES test_init.c)

# REQ-NET-002 + REQ-AUTH-001 + REQ-BUILD-007: public headers must not reference
# libcurl, wolfSSL or OpenSSL.
add_test(NAME public_headers_dep_free
  COMMAND ${CMAKE_COMMAND}
    -DEHEM_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/include
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_public_headers.cmake)
set_tests_properties(public_headers_dep_free PROPERTIES LABELS unit)

# REQ-BUILD-007: no test knows the crypto backend — only the per-backend
# tests/support/crypto_ref_<backend>.c includes a wolfSSL or OpenSSL header.
add_test(NAME tests_backend_free
  COMMAND ${CMAKE_COMMAND}
    -DEHEM_TESTS_DIR=${PROJECT_SOURCE_DIR}/tests
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_tests_backend_free.cmake)
set_tests_properties(tests_backend_free PROPERTIES LABELS unit)

# REQ-API-006: the shared library must export only ehem_* symbols.
# Scripted nm/objdump check, run as a CTest with the `unit` label — wherever a
# shared library is built (EHEM_BUILD_SHARED, REQ-BUILD-006).
if(TARGET encedo-hem)
  add_test(NAME export_symbols
    COMMAND ${CMAKE_COMMAND}
      -DEHEM_SHARED_LIB=$<TARGET_FILE:encedo-hem>
      -DEHEM_EXPORTS_BASELINE=${CMAKE_CURRENT_SOURCE_DIR}/exports_baseline_1.0.txt
      -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
  set_tests_properties(export_symbols PROPERTIES LABELS unit)
endif()

# REQ-API-007: docs/API-GUIDE.md must index every public symbol (the
# headers are the per-symbol reference; the guide is the map to them).
add_test(NAME docs_coverage
  COMMAND ${CMAKE_COMMAND}
    -DEHEM_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/include
    -DEHEM_DOCS_FILE=${PROJECT_SOURCE_DIR}/docs/API-GUIDE.md
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_docs_coverage.cmake)
set_tests_properties(docs_coverage PROPERTIES LABELS unit)

# REQ-API-009: every string/character literal in the SDK sources
# is ASCII (a small lexer — comments keep their typography; vendored code is
# third-party and excluded).
file(GLOB_RECURSE EHEM_ASCII_SOURCES CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/src/*.c ${PROJECT_SOURCE_DIR}/src/*.h
  ${PROJECT_SOURCE_DIR}/include/*.h)
list(FILTER EHEM_ASCII_SOURCES EXCLUDE REGEX "/vendor/")
add_executable(check_ascii_strings check_ascii_strings.c)
target_compile_options(check_ascii_strings PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME ascii_strings COMMAND check_ascii_strings ${EHEM_ASCII_SOURCES})
set_tests_properties(ascii_strings PROPERTIES LABELS unit)
ehem_add_unit_test(test_logger_storage SOURCES test_logger_storage.c)
