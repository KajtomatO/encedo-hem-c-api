---
id: STEP-M13-040
title: "examples/status.c and examples/sign.c, EHEM_BUILD_EXAMPLES compiled in CI; docs/API-GUIDE.md '## Examples and hem-tool'; REQ-API-007 re-verified"
milestone: M13
implements: ["REQ-API-007"]
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: ["STEP-M13-010"]
evidence:
  commits: ["86cf314"]   # start; the implementation commit follows this edit; the done commit after the CI run
  tests:
    - "2026-11-03, compile matrix on this host: gcc + wolfSSL (build-m13), clang + wolfSSL (build-clang), gcc + OpenSSL (build-openssl), clang + OpenSSL (build-clang-openssl) — `ehem-example-status` and `ehem-example-sign` build warning-free under -Wall -Wextra -Werror, linking the shared SDK (the examples link `encedo-hem::encedo-hem` when the build has it, else the static alias). Gates on build-m13 after the guide edit: docs_coverage, ascii_strings, public_headers_dep_free, subdir_consumer 4/4 (the consumer gate's target check now also refuses `ehem-example-*`; the examples default OFF below the top level). MinGW: ci.yml's 10 legs compile them — run id recorded at the close (the user pushes)."
    - "2026-11-03 (UTC 2026-11-04 06:55 device time), attended against the dev device from this host, hem.env sourced: `examples/status` under system trust (no --cacert) → exit 0, printed uptime 668631 s, temp 36.0 C, state 'no errors (fls_state 0, ctx 0)', the device time, two storage entries (this firmware sends no hostname/inited/https/tts). `examples/sign` with a wrong EHEM_PASSPHRASE → 'sign: ehem_key_list_all: EHEM_ERR_AUTH_FAILED - /api/auth/token: HTTP 401' (the first authenticated call, as documented). A scratch helper (scratchpad mkkey/, public API, not committed) created the Ed25519 key `EHEMTEST-example-sign` (kid 47e0e99a00d40bed6faf88c949b9d94c); `examples/sign EHEMTEST-example-sign 'hello from STEP-M13-040'` → exit 0: key line (type ATT,PKEY,ExDSA,ED25519), alg Ed25519, msg 23 bytes, a 64-byte signature in hex; `examples/sign EHEMTEST-no-such-key` → exit 1, 'no key labelled … among the 6 on the device'; the helper deleted the key (REQ-TEST-003 hygiene: EHEMTEST prefix, removed in the same run; the user's keys untouched)."
  notes: "examples/status.c (URL from argv, EHEM_URL or the default; system trust; the fields printed; a refused ehem_global_init explained from ehem_build_info_get; failures as ehem_rc + message + HTTP status, plus the expired-certificate line when tls_expired) and examples/sign.c (EHEM_PASSPHRASE required; ehem_login → ehem_key_list_all → the label → ehem_key_type_parse picks the selector per family: Ed25519, Ed448, SHA256/384/512WithECDSA; the device decides whether a NIST key may sign; ehem_sign with no context; hex output). examples/CMakeLists.txt: ehem_add_example() — targets ehem-example-<name>, OUTPUT_NAME <name>, EHEM_WARNING_FLAGS, shared SDK if built else the static alias. Root CMakeLists.txt: option EHEM_BUILD_EXAMPLES, default ON when CMAKE_SOURCE_DIR == PROJECT_SOURCE_DIR (top level), OFF for an add_subdirectory consumer; add_subdirectory(examples) before tests/support. tests/consumer/check_subdir_consumer.cmake: `ehem-example` added to the forbidden-target regex. docs/API-GUIDE.md: the intro's hem-tool sentence now names examples/ and encedo-hem-tool; `## hem-tool` → `## Examples and hem-tool` (both examples explained by the shape they show, the build/run commands, hem-tool's pointer); the guide's title still says 1.2 — STEP-M13-050 bumps it with the version. Not run by the build (they need a device); the %ld/%lu casts keep MinGW's printf checking quiet. README (Layout, the examples mention) and ARCHITECTURE §10: STEP-M13-050."
reopened: []
cancelled: null
---

**Goal:** the SDK ships runnable examples of its own now that hem-tool has
left: `examples/status.c` (context, system trust, `ehem_system_status`,
error detail) and `examples/sign.c` (passphrase login from the
environment, `keys list` lookup by label, `ehem_sign`, hex output), built
by default (`EHEM_BUILD_EXAMPLES`, ON when top level) and compiled on every
CI leg; the API guide points at them and at encedo-hem-tool.

**Notes:** public API only (the examples are consumers); no device needed
to build them; they are not CTests. `docs/API-GUIDE.md`'s `## hem-tool`
section becomes `## Examples and hem-tool`: the two examples explained in
a few lines each, then the pointer to encedo-hem-tool for the
full-featured client. The `docs_coverage` gate must stay green (the guide's
symbol index is unchanged). REQ-API-007's new criterion closes here.

**Definition of done**
- [ ] `examples/status.c`, `examples/sign.c`, `examples/CMakeLists.txt`;
      `EHEM_BUILD_EXAMPLES` option; both compile on gcc, clang and MinGW
      (ci.yml run id) under both backends.
- [x] `examples/status.c` run against the dev device under system trust
      prints the status and exits 0; `examples/sign.c` signs with an
      `EHEMTEST`-prefixed key created and removed for the run (attended
      once, recorded here). *(2026-11-03, from this host — the evidence
      above.)*
- [x] `docs/API-GUIDE.md` "Examples and hem-tool" written; `docs_coverage`
      green. *(2026-11-03.)*
- [ ] REQ-API-007 out of needs-reverify; TRACE regenerated.
