---
id: STEP-M1-060
title: libcurl default transport — TLS trust modes, timeouts
milestone: M1
implements: ["REQ-NET-002", "REQ-NET-003", "REQ-NET-004"]
traces:
  architecture: ["ARCHITECTURE.md#7-transport"]
depends_on: ["STEP-M1-050"]
evidence:
  commits: []   # to be recorded at commit time (user runs commits)
  tests: ["verifies: REQ-NET-002, REQ-NET-003, REQ-NET-004 (tests/unit/test_transport_curl.c + tests/unit/check_public_headers.cmake)"]
  notes: >
    src/transport_curl.c implements the vtable with one reused curl easy handle
    per context (connection reuse); non-method options (ERRORBUFFER, NOSIGNAL,
    no redirect, user-agent, TLS) set once at construction, method-sticky
    options reset to a clean GET baseline each send so a reused handle never
    carries a prior request's method/body. Three TLS modes (REQ-NET-003):
    SYSTEM = VERIFYPEER/HOST on; CA_FILE = CAINFO=ca_file; INSECURE = verify
    off (only via the explicit enum). Per-request connect+total timeouts from
    the request struct (REQ-NET-004). Error translation is a pure function
    ehem_curl_map_error(CURLcode, connect_time) — unit-tested directly (no
    network): resolve/connect failure → UNREACHABLE; CURLE_OPERATION_TIMEDOUT
    split by connect_time (0 → UNREACHABLE connect-timeout, >0 → NETWORK
    total-timeout); RECV/SEND/TLS → NETWORK; OOM → NOMEM; URL malformed →
    ARG. ehem_transport_last_detail() exposes curl's error text (new optional
    vtable op; fake gained a matching op + fake_transport_set_detail). Global
    curl_global_init/cleanup now live in the transport backend, called from the
    idempotent ehem_global_init/cleanup so context.c stays libcurl-free; the
    default transport is created (and owned) by ehem_ctx_create when no override
    is set. CMake: find_package(CURL REQUIRED) → CURL::libcurl (PRIVATE to the
    shared lib, PUBLIC/interface on the static lib); config template gained
    find_dependency(CURL). Public headers stay libcurl-free — new CTest
    public_headers_curl_free enforces it, and test_version compiles including
    only <ehem/ehem.h>. Verified on Linux (2026-07-15): `ctest -L unit` 7/7
    green, ASan/LSan clean (no easy-handle leak), `nm -D` still exports only the
    eight ehem_* symbols. Manual HTTPS smoke (scratchpad/smoke.c, GET via the
    real transport):
      https://example.com        → OK: status=200 body_bytes=559 (system trust)
      http://127.0.0.1:1         → EHEM_ERR_UNREACHABLE "Failed to connect ... Couldn't connect to server"
      https://nonexistent.invalid→ EHEM_ERR_UNREACHABLE "Could not resolve host"
    (Live HEM-device TLS-model confirmation is STEP-M1-100, REQ-NET-003's open
    criterion.)
reopened: []
cancelled: null
---

**Goal:** `transport_curl.c` implementing the vtable with one curl easy
handle per context (connection reuse), the three TLS trust modes (system
default / caller CA or pinned cert / explicit insecure), and separate
connect + total-request timeouts, with curl failures translated to
transport-level error codes that map onto `EHEM_ERR_UNREACHABLE` /
`EHEM_ERR_NETWORK`.

**Notes:** Public headers must stay curl-free (compile check without curl
dev headers). Unit tests cover the option plumbing and error translation
via the seam; actual TLS behavior is exercised manually/integration
(STEP-M1-080, -100). Note curl_global_init is already wrapped by
`ehem_global_init` (STEP-M1-040).

**Definition of done**
- [x] Vtable implementation with per-context easy handle; no handle leak (ASan) — *transport_curl.c; ASan/LSan clean*
- [x] TLS modes settable via context options; insecure requires the explicit flag — *SYSTEM/CA_FILE/INSECURE set at handle creation; test_default_transport_per_mode*
- [x] Connect and total timeouts applied per request from the request struct — *curl_send applies CURLOPT_CONNECTTIMEOUT_MS / CURLOPT_TIMEOUT_MS from the request*
- [x] curl error codes translated: connect failure/timeout → unreachable-class, mid-transfer failure/timeout → network-class — *ehem_curl_map_error + test_map_error_classes + smoke*
- [x] Public headers compile in a TU with no libcurl includes available — *public_headers_curl_free CTest; test_version.c includes only <ehem/ehem.h>*
- [x] Manual smoke against any HTTPS endpoint recorded in evidence.notes — *https://example.com → 200; failure paths → UNREACHABLE (above)*
