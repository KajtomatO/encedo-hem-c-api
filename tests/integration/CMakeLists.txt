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
  add_executable(test_keys_rm_live test_keys_rm_live.c)
  target_link_libraries(test_keys_rm_live PRIVATE hem-tool-core cmocka::cmocka)
  target_include_directories(test_keys_rm_live PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/support
    ${PROJECT_SOURCE_DIR}/src/tools/hem-tool)
  target_compile_options(test_keys_rm_live PRIVATE ${EHEM_WARNING_FLAGS})
  add_test(NAME test_keys_rm_live COMMAND test_keys_rm_live)
  set_tests_properties(test_keys_rm_live PROPERTIES LABELS integration SKIP_RETURN_CODE 77)
endif()
