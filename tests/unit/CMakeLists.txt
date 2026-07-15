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

# REQ-NET-002: the public headers must not reference libcurl.
add_test(NAME public_headers_curl_free
  COMMAND ${CMAKE_COMMAND}
    -DEHEM_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/include
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_public_headers.cmake)
set_tests_properties(public_headers_curl_free PROPERTIES LABELS unit)

# REQ-API-006: the shared library must export only ehem_* symbols.
# Scripted nm/objdump check, run as a CTest with the `unit` label.
add_test(NAME export_symbols
  COMMAND ${CMAKE_COMMAND}
    -DEHEM_SHARED_LIB=$<TARGET_FILE:encedo-hem>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
set_tests_properties(export_symbols PROPERTIES LABELS unit)
