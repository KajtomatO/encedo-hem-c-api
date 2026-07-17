# Integration tests (label `integration`). Real device; skipped unless
# EHEM_TEST_URL is set (REQ-TEST-002). See tests/README.md.

ehem_add_integration_test(test_system_live SOURCES test_system_live.c)

# REQ-SYS-003: live check-in (device + Encedo cloud relay).
ehem_add_integration_test(test_checkin_live SOURCES test_checkin_live.c)

# REQ-SYS-004: live authenticated config GET (public API only).
ehem_add_integration_test(test_config_live SOURCES test_config_live.c)

# REQ-KEY-001: live authenticated keymgmt list walk (public API only).
ehem_add_integration_test(test_keymgmt_live SOURCES test_keymgmt_live.c)

# REQ-KEY-004/005, REQ-TEST-003: live create → list → delete round-trip with
# EHEMTEST key hygiene (public API only).
ehem_add_integration_test(test_keymgmt_mutate_live SOURCES test_keymgmt_mutate_live.c)

# REQ-KEY-002: live descr prefix search (hit + no-match) (public API only).
ehem_add_integration_test(test_keymgmt_search_live SOURCES test_keymgmt_search_live.c)

# REQ-KEY-003: live single-key get + a per-KID scope probe. Exception to the
# public-API-only rule (like test_auth_live): the scope probe drives the INTERNAL
# ehem_proto_request_json to try scopes the public binding never uses, so it links
# the static lib + internal headers via ehem_test_support. Still `integration`.
add_executable(test_keymgmt_get_live test_keymgmt_get_live.c)
target_link_libraries(test_keymgmt_get_live PRIVATE ehem_test_support cmocka::cmocka)
target_include_directories(test_keymgmt_get_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_options(test_keymgmt_get_live PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME test_keymgmt_get_live COMMAND test_keymgmt_get_live)
set_tests_properties(test_keymgmt_get_live PROPERTIES LABELS integration SKIP_RETURN_CODE 77)

# REQ-OPS-001 (+ REQ-KEY-006 live cross-check): live sign → local wolfCrypt
# verify (the M4 gate criterion) + the sign scope probe closing §12 risk 3.
# Internal-linking like test_keymgmt_get_live (probe + crypto shim).
add_executable(test_sign_live test_sign_live.c)
target_link_libraries(test_sign_live PRIVATE ehem_test_support cmocka::cmocka)
target_include_directories(test_sign_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_options(test_sign_live PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME test_sign_live COMMAND test_sign_live)
set_tests_properties(test_sign_live PROPERTIES LABELS integration SKIP_RETURN_CODE 77)

# REQ-OPS-003: live sign → device-side verify + tamper negatives (public API).
ehem_add_integration_test(test_verify_live SOURCES test_verify_live.c)

# REQ-OPS-004: live ECDH — X25519 shim cross-check + P-384 truncation probe.
# Internal-linking (crypto shim + wolfCrypt SHA-256), like test_sign_live.
add_executable(test_ecdh_live test_ecdh_live.c)
target_link_libraries(test_ecdh_live PRIVATE ehem_test_support cmocka::cmocka)
target_include_directories(test_ecdh_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_options(test_ecdh_live PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME test_ecdh_live COMMAND test_ecdh_live)
set_tests_properties(test_ecdh_live PROPERTIES LABELS integration SKIP_RETURN_CODE 77)

# REQ-OPS-005: live HMAC round-trips + the derived-key HKDF-conflict probe.
# Internal-linking (crypto shim X25519/HMAC), like test_ecdh_live.
add_executable(test_hmac_live test_hmac_live.c)
target_link_libraries(test_hmac_live PRIVATE ehem_test_support cmocka::cmocka)
target_include_directories(test_hmac_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_options(test_hmac_live PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME test_hmac_live COMMAND test_hmac_live)
set_tests_properties(test_hmac_live PROPERTIES LABELS integration SKIP_RETURN_CODE 77)

# REQ-OPS-006: live cipher matrix + HKDF-info probe. Internal-linking (shim
# X25519 + wolfCrypt HKDF/AES-GCM), like test_ecdh_live.
add_executable(test_cipher_live test_cipher_live.c)
target_link_libraries(test_cipher_live PRIVATE ehem_test_support cmocka::cmocka)
target_include_directories(test_cipher_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_options(test_cipher_live PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME test_cipher_live COMMAND test_cipher_live)
set_tests_properties(test_cipher_live PROPERTIES LABELS integration SKIP_RETURN_CODE 77)

# REQ-OPS-007/008: live ML-KEM/ML-DSA round-trips + quirk probes (public API,
# longer keygen timeout).
ehem_add_integration_test(test_pqc_live SOURCES test_pqc_live.c)

# REQ-TEST-004: the M5 per-family generation matrix — create → list → get/
# classify → sign(where ExDSA-capable)+local verify → delete, for every fw
# create type. Internal-linking (crypto shim local verify), like test_sign_live.
add_executable(test_keygen_matrix_live test_keygen_matrix_live.c)
target_link_libraries(test_keygen_matrix_live PRIVATE ehem_test_support cmocka::cmocka)
target_include_directories(test_keygen_matrix_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_options(test_keygen_matrix_live PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME test_keygen_matrix_live COMMAND test_keygen_matrix_live)
set_tests_properties(test_keygen_matrix_live PROPERTIES LABELS integration SKIP_RETURN_CODE 77)

# REQ-AUTH-001/002/003: live authenticated token acquisition. Exception to the
# public-API-only rule for integration tests — no public authenticated binding
# exists until M2-050, so this drives the INTERNAL ehem_auth_ensure_token and
# therefore links the static lib + internal headers (via ehem_test_support),
# like the unit suite. Still carries the `integration` label + skip gating.
add_executable(test_auth_live test_auth_live.c)
target_link_libraries(test_auth_live PRIVATE ehem_test_support cmocka::cmocka)
target_include_directories(test_auth_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_options(test_auth_live PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME test_auth_live COMMAND test_auth_live)
set_tests_properties(test_auth_live PROPERTIES LABELS integration SKIP_RETURN_CODE 77)

# REQ-TOOL-006: live `keys rm --label-prefix` removes EHEMTEST keys. Drives the
# shared hem-tool-core code (public API), so it links hem-tool-core; only exists
# when the CLI is built. Integration (EHEMTEST-only mutation), not disruptive.
if(TARGET hem-tool-core)
  # REQ-OPS-002 + REQ-TOOL-010: live hardware-RNG harvest + tool hygiene.
  add_executable(test_random_live test_random_live.c)
  target_link_libraries(test_random_live PRIVATE hem-tool-core cmocka::cmocka)
  target_include_directories(test_random_live PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/support
    ${PROJECT_SOURCE_DIR}/src/tools/hem-tool)
  target_compile_options(test_random_live PRIVATE ${EHEM_WARNING_FLAGS})
  add_test(NAME test_random_live COMMAND test_random_live)
  set_tests_properties(test_random_live PROPERTIES LABELS integration SKIP_RETURN_CODE 77)

  add_executable(test_keys_rm_live test_keys_rm_live.c)
  target_link_libraries(test_keys_rm_live PRIVATE hem-tool-core cmocka::cmocka)
  target_include_directories(test_keys_rm_live PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/support
    ${PROJECT_SOURCE_DIR}/src/tools/hem-tool)
  target_compile_options(test_keys_rm_live PRIVATE ${EHEM_WARNING_FLAGS})
  add_test(NAME test_keys_rm_live COMMAND test_keys_rm_live)
  set_tests_properties(test_keys_rm_live PROPERTIES LABELS integration SKIP_RETURN_CODE 77)
endif()
