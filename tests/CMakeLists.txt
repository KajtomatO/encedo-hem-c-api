# Test wiring. CTest labels partition the suites (ARCHITECTURE.md §9):
#   ctest -L unit         offline, always run
#   ctest -L integration  real device, gated on EHEM_TEST_URL (STEP-M1-080)
#   ctest -L disruptive   real device, mutates + reboots it; gated on
#                         EHEM_TEST_URL + EHEM_ALLOW_DISRUPTIVE=1 (STEP-M2-060)
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
  # ehem_test_support PUBLIC-links the static lib, so listing it (not the lib
  # directly) also gives the correct static-archive link order: test → support
  # → static lib. Unit tests may reach internal headers (json.h, transport.h,
  # context.h) to test below the public API; support PUBLIC-includes src/.
  target_link_libraries(${name} PRIVATE ehem_test_support cmocka::cmocka)
  target_compile_definitions(${name} PRIVATE EHEM_VERSION_STRING="${PROJECT_VERSION}")
  target_compile_options(${name} PRIVATE ${EHEM_WARNING_FLAGS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS unit)
endfunction()

# ehem_add_integration_test(<name> SOURCES ...)
# Links the shared library (public API only) and carries the `integration`
# label. Gating is shared infra (tests/support/integration_env.h): the test
# main() exits EHEM_SKIP_EXIT (77) when EHEM_TEST_URL is unset, and
# SKIP_RETURN_CODE makes CTest report that as "Skipped", not a failure
# (REQ-TEST-002).
function(ehem_add_integration_test name)
  cmake_parse_arguments(A "" "" "SOURCES" ${ARGN})
  add_executable(${name} ${A_SOURCES})
  target_link_libraries(${name} PRIVATE encedo-hem::encedo-hem cmocka::cmocka)
  # PROJECT_SOURCE_DIR (not CMAKE_CURRENT_SOURCE_DIR) because this runs in the
  # caller's scope; the shared gating header lives under tests/support.
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(${name} PRIVATE ${EHEM_WARNING_FLAGS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS integration SKIP_RETURN_CODE 77)
endfunction()

# ehem_add_disruptive_test(<name> SOURCES ...)
# A live test that MUTATES + REBOOTS the device: label `disruptive`, excluded
# from `-L integration`, and gated at runtime on EHEM_TEST_URL +
# EHEM_ALLOW_DISRUPTIVE=1 (ehem_require_disruptive → exit 77 = skip). Links the
# shared hem-tool cert-install code (hem-tool-core, public API) so the live run
# drives exactly the tool a user would.
function(ehem_add_disruptive_test name)
  cmake_parse_arguments(A "" "" "SOURCES" ${ARGN})
  add_executable(${name} ${A_SOURCES})
  target_link_libraries(${name} PRIVATE hem-tool-core)
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(${name} PRIVATE ${EHEM_WARNING_FLAGS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS disruptive SKIP_RETURN_CODE 77)
endfunction()

# --- shared test support --------------------------------------------------
# Fake transport and (later) fixtures/vectors. Our code, so held to the same
# warning flags; PUBLIC-includes the internal src/ headers (transport.h,
# context.h) and the support dir, and pulls the static lib transitively so
# tests linking it resolve the internal symbols the fake calls.
add_library(ehem_test_support STATIC
  support/fake_transport.c
  support/ext_sim.c)
target_link_libraries(ehem_test_support PUBLIC encedo-hem::encedo-hem-static)
target_include_directories(ehem_test_support PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_compile_options(ehem_test_support PRIVATE ${EHEM_WARNING_FLAGS})

add_subdirectory(unit)
add_subdirectory(integration)
# Disruptive live tests need the shared tool code (hem-tool-core), which only
# exists when the CLI is built.
if(TARGET hem-tool-core)
  add_subdirectory(disruptive)
endif()
