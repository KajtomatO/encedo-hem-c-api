# Disruptive tests (label `disruptive`). Real device; MUTATES + REBOOTS it, so
# gated on EHEM_TEST_URL + EHEM_ALLOW_DISRUPTIVE=1 (STEP-M2-060). Excluded from
# `-L integration`; run with `./dev test it -d` or
# `EHEM_ALLOW_DISRUPTIVE=1 ctest -L disruptive`.

# REQ-TOOL-003: live cert-install (harvest → skip-if-current → install →
# reboot → verify) driven through the real hem-tool cert-install code path.
ehem_add_disruptive_test(test_cert_install_live SOURCES test_cert_install_live.c)

# REQ-SYS-010: ATTENDED storage unlock/lock probe. Disruptive-gated for its
# first runs because the v1.2.2 handlers read an uninitialized pointer in
# their scope check (a hard-fault would self-reset the device); public API +
# cmocka, so declared directly rather than via the hem-tool-core macro.
add_executable(test_storage_live test_storage_live.c)
target_link_libraries(test_storage_live PRIVATE ehem_test_support cmocka::cmocka)
target_include_directories(test_storage_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_options(test_storage_live PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME test_storage_live COMMAND test_storage_live)
set_tests_properties(test_storage_live PROPERTIES LABELS disruptive SKIP_RETURN_CODE 77)
