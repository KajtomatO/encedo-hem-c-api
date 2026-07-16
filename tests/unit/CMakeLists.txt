# Offline unit tests (label `unit`).

ehem_add_unit_test(test_version SOURCES test_version.c)

# REQ-BUILD-003: JSON round-trips through the internal helper layer.
ehem_add_unit_test(test_json SOURCES test_json.c)

# REQ-API-001..004: context lifecycle, options, error enum, last-error.
ehem_add_unit_test(test_context SOURCES test_context.c)

# REQ-NET-001, REQ-TEST-001: transport vtable seam via the injected fake.
ehem_add_unit_test(test_transport SOURCES test_transport.c)

# REQ-NET-002..004: default (libcurl) transport error mapping + option plumbing.
ehem_add_unit_test(test_transport_curl SOURCES test_transport_curl.c)

# REQ-SYS-001, REQ-SYS-002, REQ-API-005: system status/version bindings.
ehem_add_unit_test(test_system SOURCES test_system.c)

# REQ-SYS-003, REQ-NET-005: check-in flow + automatic certificate recovery.
ehem_add_unit_test(test_checkin SOURCES test_checkin.c)

# REQ-SYS-006, REQ-TOOL-003: public cert-inspection helper (serial + validity).
ehem_add_unit_test(test_cert SOURCES test_cert.c)

# REQ-TOOL-003: hem-tool cert-install orchestration via the fake transport.
# Links hem-tool-core (the shared tool code) alongside the test support lib.
if(TARGET hem-tool-core)
  ehem_add_unit_test(test_cert_install SOURCES test_cert_install.c)
  target_link_libraries(test_cert_install PRIVATE hem-tool-core)

  # REQ-TOOL-004/005: protected-key classifier + `keys list` (hem-tool-core).
  ehem_add_unit_test(test_keys SOURCES test_keys.c)
  target_link_libraries(test_keys PRIVATE hem-tool-core)
endif()

# REQ-SYS-004, REQ-SYS-005: config GET / cert install / reboot bindings.
ehem_add_unit_test(test_config SOURCES test_config.c)

# REQ-KEY-001: keymgmt list binding + full-repository walk (pagination).
ehem_add_unit_test(test_keymgmt SOURCES test_keymgmt.c)

# REQ-AUTH-001: crypto shim (PBKDF2 / HMAC-SHA256 / X25519) against RFC vectors.
ehem_add_unit_test(test_crypto SOURCES test_crypto.c)

# REQ-AUTH-001: base64/base64url codecs + eJWT builder vs the python fixture.
ehem_add_unit_test(test_ejwt SOURCES test_ejwt.c)

# REQ-AUTH-001, REQ-AUTH-002: login sequence + scope-keyed token cache.
ehem_add_unit_test(test_auth SOURCES test_auth.c)

# REQ-NET-002 + REQ-AUTH-001: public headers must not reference libcurl/wolfSSL.
add_test(NAME public_headers_dep_free
  COMMAND ${CMAKE_COMMAND}
    -DEHEM_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/include
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_public_headers.cmake)
set_tests_properties(public_headers_dep_free PROPERTIES LABELS unit)

# REQ-API-006: the shared library must export only ehem_* symbols.
# Scripted nm/objdump check, run as a CTest with the `unit` label.
add_test(NAME export_symbols
  COMMAND ${CMAKE_COMMAND}
    -DEHEM_SHARED_LIB=$<TARGET_FILE:encedo-hem>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
set_tests_properties(export_symbols PROPERTIES LABELS unit)
