# Test wiring. CTest labels partition the suites (ARCHITECTURE.md §9):
#   ctest -L unit         offline, always run
#   ctest -L integration  real device, gated on EHEM_TEST_URL (STEP-M1-080)
#   ctest -L dangerous    added when the first dangerous test exists
#
# implements: REQ-BUILD-001 (CTest labels wired from the start)

# --- cmocka (unit-test framework) -----------------------------------------
find_package(cmocka CONFIG QUIET)
if(NOT TARGET cmocka::cmocka)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(CMOCKA IMPORTED_TARGET QUIET cmocka)
    if(TARGET PkgConfig::CMOCKA)
      add_library(cmocka::cmocka ALIAS PkgConfig::CMOCKA)
    endif()
  endif()
endif()
if(NOT TARGET cmocka::cmocka)
  message(FATAL_ERROR
    "cmocka not found — install it (scripts/install-deps-linux.sh / "
    ".ps1) or set CMAKE_PREFIX_PATH to a cmocka install.")
endif()

# ehem_add_unit_test(<name> SOURCES a.c b.c ...)
# Links the STATIC library so tests can reach internal (hidden-visibility)
# symbols; tagged with the `unit` label.
function(ehem_add_unit_test name)
  cmake_parse_arguments(A "" "" "SOURCES" ${ARGN})
  add_executable(${name} ${A_SOURCES})
  target_link_libraries(${name} PRIVATE encedo-hem::encedo-hem-static cmocka::cmocka)
  target_compile_definitions(${name} PRIVATE EHEM_VERSION_STRING="${PROJECT_VERSION}")
  target_compile_options(${name} PRIVATE ${EHEM_WARNING_FLAGS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS unit)
endfunction()

# ehem_add_integration_test(<name> SOURCES ...)
# Links the shared library (public API only) and carries the `integration`
# label; the test body skips itself when EHEM_TEST_URL is unset (STEP-M1-080).
function(ehem_add_integration_test name)
  cmake_parse_arguments(A "" "" "SOURCES" ${ARGN})
  add_executable(${name} ${A_SOURCES})
  target_link_libraries(${name} PRIVATE encedo-hem::encedo-hem cmocka::cmocka)
  target_compile_options(${name} PRIVATE ${EHEM_WARNING_FLAGS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS integration)
endfunction()

add_subdirectory(unit)
