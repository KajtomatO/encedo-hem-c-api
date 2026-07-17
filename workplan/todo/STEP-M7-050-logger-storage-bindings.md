---
id: STEP-M7-050
title: "Logger + storage bindings — key/list/get, unlock/lock"
milestone: M7
implements: ["REQ-SYS-009", "REQ-SYS-010"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Five functions + free functions exported across proto_logger/
      proto_storage, tagged `implements: REQ-SYS-009` / `REQ-SYS-010`
- [ ] Unit tests green (gcc+clang+asan): key length enforcement, list
      paging shape, get raw bytes verbatim, storage scope composition
      asserted per disk/mode, EPA-404 → NOT_FOUND, full error mapping,
      ARG pre-validation
- [ ] Live: logger key + shim Ed25519 nonce verification green; list→get
      round-trip (or EPA skip recorded); attended storage
      unlock/lock probe run with results + label decision recorded in
      REQ-SYS-010
- [ ] PPA/EPA determination recorded in REQ-SYS-009 + REQ-SYS-010
- [ ] Export/header gates green; MinGW cross-syntax check run
