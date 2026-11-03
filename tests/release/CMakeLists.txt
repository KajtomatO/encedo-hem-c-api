# Release-job helpers that are not tests themselves.

# tls-probe — the end-to-end driver of the REQ-NET-005 expired-certificate
# classifier: scripts/release/check-expired-classifier.sh runs it against local
# expired and not-yet-valid endpoints under the build's TLS backend, in the
# release job for both backends (REQ-BUILD-010). Public API only; linked to the
# static SDK (the private archive when the static variant is off), so it runs
# from the build tree with nothing but the libraries the build itself needs.
# Built whenever the tests are (BUILD_TESTING) — CI compiles it on every leg —
# but NOT registered as a CTest: it needs the endpoints the script serves.
add_executable(tls-probe tls_probe.c)
target_link_libraries(tls-probe PRIVATE ${EHEM_STATIC_TARGET})
target_compile_options(tls-probe PRIVATE ${EHEM_WARNING_FLAGS})
