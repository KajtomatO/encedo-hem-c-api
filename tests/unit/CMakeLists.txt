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

# REQ-TEST-006: simulated ExtAuth authenticator — shim AES-128-CBC vector,
# scheme-A codec (round-trip, firmware-construction cross-check, tamper),
# HS256 JWT build/open/peek (M8).
ehem_add_unit_test(test_ext_sim SOURCES test_ext_sim.c)

# REQ-AUTH-006: ExtAuth pairing bindings init/validate/mac (M8).
ehem_add_unit_test(test_ext SOURCES test_ext.c)

# REQ-AUTH-008: notification-broker client (M8).
ehem_add_unit_test(test_notify SOURCES test_notify.c)

# REQ-AUTH-009: mobile confirmation engine — pollable + blocking wait (M8).
ehem_add_unit_test(test_confirm SOURCES test_confirm.c)

# REQ-AUTH-010: mobile login mode via the ensure-token chokepoint (M8).
ehem_add_unit_test(test_mobile SOURCES test_mobile.c)

# REQ-TOOL-003: hem-tool cert-install orchestration via the fake transport.
# Links hem-tool-core (the shared tool code) alongside the test support lib.
if(TARGET hem-tool-core)
  ehem_add_unit_test(test_cert_install SOURCES test_cert_install.c)
  target_link_libraries(test_cert_install PRIVATE hem-tool-core)

  # REQ-TOOL-004/005: protected-key classifier + `keys list` (hem-tool-core).
  ehem_add_unit_test(test_keys SOURCES test_keys.c)
  target_link_libraries(test_keys PRIVATE hem-tool-core)

  # REQ-TOOL-007: `keys pub` — material formats, classification, exit codes.
  ehem_add_unit_test(test_keys_pub SOURCES test_keys_pub.c)
  target_link_libraries(test_keys_pub PRIVATE hem-tool-core)

  # REQ-TOOL-008: `sign` — inputs, default-alg lookup, formats, exit codes.
  ehem_add_unit_test(test_sign_tool SOURCES test_sign_tool.c)
  target_link_libraries(test_sign_tool PRIVATE hem-tool-core)

  # REQ-TOOL-009: `keys gen` — body bytes, NIST mode default, exit codes.
  ehem_add_unit_test(test_keys_gen SOURCES test_keys_gen.c)
  target_link_libraries(test_keys_gen PRIVATE hem-tool-core)

  # REQ-TOOL-010: `random` — transient-key lifecycle, formats, exit codes.
  ehem_add_unit_test(test_random_tool SOURCES test_random_tool.c)
  target_link_libraries(test_random_tool PRIVATE hem-tool-core)

  # REQ-TOOL-011/012/013: `keys update` / `logs` / `selftest` (M7).
  ehem_add_unit_test(test_tool_m7 SOURCES test_tool_m7.c)
  target_link_libraries(test_tool_m7 PRIVATE hem-tool-core)

  # REQ-TOOL-015: `tls-recover` orchestration (M7).
  ehem_add_unit_test(test_recover SOURCES test_recover.c)
  target_link_libraries(test_recover PRIVATE hem-tool-core)

  # REQ-TOOL-016: `ext` pair/list/login + terminal QR (M8).
  ehem_add_unit_test(test_ext_tool SOURCES test_ext_tool.c)
  target_link_libraries(test_ext_tool PRIVATE hem-tool-core)
endif()

# REQ-SYS-004, REQ-SYS-005: config GET / cert install / reboot bindings.
ehem_add_unit_test(test_config SOURCES test_config.c)

# REQ-KEY-001: keymgmt list binding + full-repository walk (pagination).
ehem_add_unit_test(test_keymgmt SOURCES test_keymgmt.c)

# REQ-KEY-006: key-type classifier (pure — no transport, no crypto backend).
ehem_add_unit_test(test_keytype SOURCES test_keytype.c)

# REQ-OPS-001: exdsa sign binding — body/scope/decode/error mapping.
ehem_add_unit_test(test_sign SOURCES test_sign.c)

# REQ-OPS-003: exdsa verify binding — body/empty-200/scope-share/error mapping.
ehem_add_unit_test(test_verify SOURCES test_verify.c)

# REQ-OPS-004: ecdh binding — peer selection, body bytes, decode, errors.
ehem_add_unit_test(test_ecdh SOURCES test_ecdh.c)

# REQ-OPS-005: hmac hash/verify bindings — flows, bodies, bounds, errors.
ehem_add_unit_test(test_hmac SOURCES test_hmac.c)

# REQ-OPS-006: cipher encrypt/decrypt bindings — bodies, shapes, bounds.
ehem_add_unit_test(test_cipher SOURCES test_cipher.c)

# REQ-OPS-007/008: ML-KEM + ML-DSA bindings incl. the raw-status quirk.
ehem_add_unit_test(test_pqc SOURCES test_pqc.c)

# REQ-OPS-002: ehem_random encrypt-IV harvest — request count, concat, tail.
ehem_add_unit_test(test_random SOURCES test_random.c)

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
ehem_add_unit_test(test_logger_storage SOURCES test_logger_storage.c)
