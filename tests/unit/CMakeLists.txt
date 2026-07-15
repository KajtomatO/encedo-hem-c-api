# Offline unit tests (label `unit`).

ehem_add_unit_test(test_version SOURCES test_version.c)

# REQ-API-006: the shared library must export only ehem_* symbols.
# Scripted nm/objdump check, run as a CTest with the `unit` label.
add_test(NAME export_symbols
  COMMAND ${CMAKE_COMMAND}
    -DEHEM_SHARED_LIB=$<TARGET_FILE:encedo-hem>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake)
set_tests_properties(export_symbols PROPERTIES LABELS unit)
