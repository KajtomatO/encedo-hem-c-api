# ehem::test-support — the SDK's test support as a target an out-of-tree
# consumer can link (REQ-BUILD-011): the fake transport (REQ-TEST-001), the
# simulated ExtAuth authenticator (REQ-TEST-006) and the header-only fixtures
# (integration_env.h, ehem_test_keys.h, fixtures/). PUBLIC-includes this
# directory and src/ (the hidden seams' declarations — proto_auth.h,
# proto_ext.h, crypto_shim.h, transport.h) and PUBLIC-links the static SDK:
# the installed static variant, or the private, never-installed archive when
# EHEM_BUILD_STATIC is off (REQ-BUILD-006). A test that lists this target
# alone gets the hidden symbols through the archive and the right static link
# order (test → support → SDK).
#
# Added by the top-level CMakeLists.txt when EHEM_BUILD_TEST_SUPPORT is ON
# (default: BUILD_TESTING); a consumer such as encedo-hem-tool sets it ON while
# leaving BUILD_TESTING off (tests/consumer/subdir is the gated shape).
#
# implements: REQ-BUILD-011

# The live tests' reference computations on the build's crypto library
# (REQ-BUILD-007; crypto_ref.h) — the only test sources that include a backend
# header. A consumer whose tests use none leaves them out.
option(EHEM_TEST_SUPPORT_CRYPTO_REF
  "ehem::test-support includes crypto_ref (needs the crypto backend's headers)"
  ${BUILD_TESTING})

add_library(ehem_test_support STATIC fake_transport.c ext_sim.c)
add_library(ehem::test-support ALIAS ehem_test_support)
if(EHEM_TEST_SUPPORT_CRYPTO_REF)
  target_sources(ehem_test_support PRIVATE crypto_ref_${EHEM_CRYPTO_BACKEND}.c)
endif()
# EHEM_STATIC_TARGET: the static variant, or the private archive when the
# variant is off — either way the hidden seams are reachable.
target_link_libraries(ehem_test_support PUBLIC ${EHEM_STATIC_TARGET})
target_include_directories(ehem_test_support PUBLIC
  ${PROJECT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(ehem_test_support PRIVATE ${EHEM_WARNING_FLAGS})
