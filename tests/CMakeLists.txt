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
# Links the shared library (public API only) — the static SDK when the build has
# no shared variant (EHEM_BUILD_SHARED=OFF, REQ-BUILD-006) — and carries the
# `integration` label. Gating is shared infra (tests/support/integration_env.h): the test
# main() exits EHEM_SKIP_EXIT (77) when EHEM_TEST_URL is unset, and
# SKIP_RETURN_CODE makes CTest report that as "Skipped", not a failure
# (REQ-TEST-002).
function(ehem_add_integration_test name)
  cmake_parse_arguments(A "" "" "SOURCES" ${ARGN})
  add_executable(${name} ${A_SOURCES})
  if(TARGET encedo-hem)
    target_link_libraries(${name} PRIVATE encedo-hem::encedo-hem cmocka::cmocka)
  else()
    target_link_libraries(${name} PRIVATE ${EHEM_STATIC_TARGET} cmocka::cmocka)
  endif()
  # PROJECT_SOURCE_DIR (not CMAKE_CURRENT_SOURCE_DIR) because this runs in the
  # caller's scope; the shared gating header lives under tests/support.
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
  target_compile_options(${name} PRIVATE ${EHEM_WARNING_FLAGS})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS integration SKIP_RETURN_CODE 77)
endfunction()

# Disruptive live tests (label `disruptive`; MUTATE + REBOOT the device; gated
# at runtime on EHEM_TEST_URL + EHEM_ALLOW_DISRUPTIVE=1, ehem_require_disruptive
# → exit 77 = skip) are declared directly in disruptive/CMakeLists.txt — the
# ehem_add_disruptive_test helper left with hem-tool's cert-install test (M13).

# --- shared test support --------------------------------------------------
# ehem_test_support (alias ehem::test-support) — the fake transport, the
# simulated authenticator, crypto_ref and the fixtures — is defined in
# support/CMakeLists.txt, which the top-level CMakeLists.txt adds whenever
# EHEM_BUILD_TEST_SUPPORT is on (REQ-BUILD-011: out-of-tree consumers link the
# same target). The suites below need it with crypto_ref inside.
if(NOT TARGET ehem_test_support)
  message(FATAL_ERROR "BUILD_TESTING needs EHEM_BUILD_TEST_SUPPORT=ON (tests/support)")
endif()
if(NOT EHEM_TEST_SUPPORT_CRYPTO_REF)
  message(FATAL_ERROR "the SDK's own tests need EHEM_TEST_SUPPORT_CRYPTO_REF=ON (the live tests' crypto_ref)")
endif()

add_subdirectory(unit)
add_subdirectory(integration)
# tests/release: tls-probe, the release job's REQ-NET-005 classifier driver
# (not a CTest; see its CMakeLists.txt).
add_subdirectory(release)

# REQ-BUILD-008: the consumer gate — install this build into a scratch prefix,
# then build and run tests/consumer against it via find_package and pkg-config
# for every variant built (consumer/check_consumer.cmake). Native builds only:
# the consumer must run. CMAKE_PREFIX_PATH travels with `|` for `;`.
if(NOT CMAKE_CROSSCOMPILING)
  string(REPLACE ";" "|" _ehem_prefix_path "${CMAKE_PREFIX_PATH}")
  add_test(NAME consumer_interface
    COMMAND ${CMAKE_COMMAND}
      "-DEHEM_BUILD_DIR=${PROJECT_BINARY_DIR}"
      "-DEHEM_SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/consumer-gate"
      "-DEHEM_CONSUMER_SRC=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
      -DEHEM_VERSION=${PROJECT_VERSION}
      "-DEHEM_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
      -DEHEM_SHARED=${EHEM_BUILD_SHARED}
      -DEHEM_STATIC=${EHEM_BUILD_STATIC}
      "-DEHEM_GENERATOR=${CMAKE_GENERATOR}"
      "-DEHEM_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DEHEM_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DEHEM_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      "-DEHEM_PREFIX_PATH=${_ehem_prefix_path}"
      -DEHEM_SANITIZE=${EHEM_SANITIZE}
      -DEHEM_CRYPTO_BACKEND=${EHEM_CRYPTO_BACKEND}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/consumer/check_consumer.cmake)
  set_tests_properties(consumer_interface PROPERTIES LABELS unit)

  # REQ-BUILD-011: the out-of-tree consumer gate — tests/consumer/subdir adds
  # this source tree as a subdirectory (tests and the shared variant off, the
  # test support on), links the static SDK + ehem::test-support, reaches a
  # hidden seam, drives a request through the fake transport, and runs; the
  # SDK must bring no test or program target along
  # (consumer/check_subdir_consumer.cmake).
  add_test(NAME subdir_consumer
    COMMAND ${CMAKE_COMMAND}
      "-DEHEM_SDK_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DEHEM_SCRATCH=${CMAKE_CURRENT_BINARY_DIR}/subdir-gate"
      "-DEHEM_CONSUMER_SRC=${CMAKE_CURRENT_SOURCE_DIR}/consumer/subdir"
      "-DEHEM_GENERATOR=${CMAKE_GENERATOR}"
      "-DEHEM_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DEHEM_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DEHEM_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      "-DEHEM_PREFIX_PATH=${_ehem_prefix_path}"
      -DEHEM_SANITIZE=${EHEM_SANITIZE}
      -DEHEM_CRYPTO_BACKEND=${EHEM_CRYPTO_BACKEND}
      -DEHEM_DEPS_LINK=${EHEM_DEPS_LINK}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/consumer/check_subdir_consumer.cmake)
  set_tests_properties(subdir_consumer PROPERTIES LABELS unit)
endif()
# Disruptive live tests: always declared, gated at run time (EHEM_TEST_URL +
# EHEM_ALLOW_DISRUPTIVE=1). Until M13 they hid behind the hem-tool target and
# would have vanished silently with it (ARCHITECTURE.md §11 M13, plan risk 5).
add_subdirectory(disruptive)
