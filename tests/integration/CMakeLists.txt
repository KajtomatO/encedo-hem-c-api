# Integration tests (label `integration`). Real device; skipped unless
# EHEM_TEST_URL is set (REQ-TEST-002). See tests/README.md.

ehem_add_integration_test(test_system_live SOURCES test_system_live.c)

# REQ-SYS-003: live check-in (device + Encedo cloud relay).
ehem_add_integration_test(test_checkin_live SOURCES test_checkin_live.c)

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
