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

# REQ-AUTH-008: broker registration probe — register/init + pending polls.
# Disruptive per REQ-TEST-006 broker gating (creates a dangling broker
# registration; never touches a phone). Public API + cmocka, so declared
# directly (the hem-tool-core macro links no cmocka), like test_storage_live.
add_executable(test_notify_register_live test_notify_register_live.c)
target_link_libraries(test_notify_register_live PRIVATE ehem_test_support cmocka::cmocka)
target_include_directories(test_notify_register_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_options(test_notify_register_live PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME test_notify_register_live COMMAND test_notify_register_live)
set_tests_properties(test_notify_register_live PROPERTIES LABELS disruptive SKIP_RETURN_CODE 77)

# REQ-TEST-004: the per-family generation matrix — QUARANTINED into the
# disruptive label (M8 gate, user decision 2026-08-06): on the current
# device state it reproducibly hard-stalls the firmware (4/4 that day, even
# alone on a fresh boot), and recovery is a PHYSICAL power-cycle — the
# definition of disruptive. History + details in KNOWN-ISSUES ("Device
# stalls/hangs") and REQ-TEST-004 rev 3. Internal-linking (crypto shim).
add_executable(test_keygen_matrix_live test_keygen_matrix_live.c)
target_link_libraries(test_keygen_matrix_live PRIVATE ehem_test_support cmocka::cmocka)
target_include_directories(test_keygen_matrix_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_options(test_keygen_matrix_live PRIVATE ${EHEM_WARNING_FLAGS})
add_test(NAME test_keygen_matrix_live COMMAND test_keygen_matrix_live)
set_tests_properties(test_keygen_matrix_live PROPERTIES LABELS disruptive SKIP_RETURN_CODE 77)
