---
id: STEP-M7-050
title: "Logger + storage bindings — key/list/get, unlock/lock"
milestone: M7
implements: ["REQ-SYS-009", "REQ-SYS-010"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
depends_on: []
evidence:
  commits: ["171afaa"]
  tests: ["verifies: REQ-SYS-009/REQ-SYS-010 (tests/unit/test_logger_storage.c — 8 cases; tests/integration/test_logger_live.c — 2 live green; tests/disruptive/test_storage_live.c — attended probe green ×2, 2026-07-18)"]
  notes: >
    New proto_logger.c + proto_storage.c + public logger.h/storage.h (the
    last two endpoint-group modules). Unit 30/30 gcc+clang + ASan;
    export/header gates green; MinGW cross-syntax OK. LIVE: logger key
    triple fetched and the nonce signature VERIFIED with the shim's
    Ed25519 (device provably holds the log-signing key); list returned
    ALL 62 files in one page (no firmware paging observed at this size);
    get downloaded 645 bytes. FINDING (device > doc, REQ-SYS-009): log
    records are PIPE-DELIMITED ("seq|ts|type|result|…|sig|chain",
    base64url fields) under a "# Encedo nGINE FW" header — NOT the doc's
    "JSON-like record per line"; recorded in logger.h + pinned in the
    live test. STORAGE (attended, user-authorized): unlock-ro 200 →
    status green mid-unlock → lock 200; rw scope variant granted; the
    uninitialized-`sub` UB was benign on this build (upstream filing
    stands); test STAYS disruptive-labeled (UB + USB-exposure side
    effect — decision recorded in REQ-SYS-010 rev2). PPA/EPA: PPA
    confirmed (routes present, consistent with se_state).
reopened: []
cancelled: null
---

**Goal:** proto_logger.c (`ehem_logger_key`/`_list`/`_get` — raw-body
download for get) and proto_storage.c (`ehem_storage_unlock`/`_lock` —
scope composed from disk/mode args) + public headers, live-verified
including the Ed25519 nonce proof and the ATTENDED storage probe.

**Notes:** New proto modules per the §6 one-module-per-group layout
(the last two groups without one). logger get uses
`ehem_proto_request_raw` with `logger:get` scope and returns body bytes
verbatim (binary-safe; short-read vs Content-Length → PROTOCOL — check
what transport_curl exposes; extend the response contract only if
needed). logger key: base64-decode with strict 32/32/64 lengths; live
test verifies nonce_signed with shim `ehem_ed25519_verify` (static-lib
link, test_auth_live precedent). **Storage live probe is
disruptive-gated + attended on first run** — the fw uninitialized-`sub`
bug (REQ-SYS-010) may hard-fault the device (HardFault → self-reset);
probe with the user present, then downgrade the test label per the REQ's
open criterion. PPA/EPA determination lands here (logger list/get +
storage routing) — record in REQ-SYS-009/010 and share with M7-040's
se_state observation. EPA fallback: NOT_FOUND mapping + skip-with-note
paths still unit-tested.

**Definition of done**
- [x] Five functions + free functions exported across proto_logger/
      proto_storage, tagged `implements: REQ-SYS-009` / `REQ-SYS-010`
- [x] Unit tests green (gcc+clang+asan): key length enforcement, list
      paging shape, get raw bytes verbatim, storage scope composition
      asserted per disk/mode, EPA-404 → NOT_FOUND, full error mapping,
      ARG pre-validation
- [x] Live: logger key + shim Ed25519 nonce verification green; list→get
      round-trip green (pipe-delimited format finding pinned); attended
      storage unlock/lock probe green ×2 with results + label decision
      recorded in REQ-SYS-010 rev2
- [x] PPA/EPA determination recorded (PPA — routes present + se_state)
- [x] Export/header gates green; MinGW cross-syntax check run
