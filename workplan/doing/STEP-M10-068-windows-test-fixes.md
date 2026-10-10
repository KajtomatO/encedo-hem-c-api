---
id: STEP-M10-068
title: "Fixes from the first Windows test of the release binary: init JWT header (REQ-AUTH-011 rev 3), ASCII-only text (REQ-API-009)"
milestone: M10
implements: ["REQ-AUTH-011", "REQ-API-009"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M10-020"]
evidence:
  commits: ["925e831"]   # the user's "Fixes after testing hem-tool on windows" (code + tests; no [STEP-…] prefix — the step was approved after the commit)
  tests:
    - "verifies: REQ-AUTH-011 — tests/unit/test_init.c: the posted init JWT's header is byte-exact EHEM_EJWT_HEADER and carries alg + ecdh (unit 47/47 gcc+clang, 2026-10-10)"
    - "verifies: REQ-API-009 — tests/unit/check_ascii_strings.c via CTest ascii_strings: green on all of src/ + include/; on a negative sample it flags only the string literal (past an escaped quote), ignores // and /* */ comments, exits 2 on an unreadable file (2026-10-10)"
  notes: >
    Created 2026-10-10 (user: "I approve steps") from the user's first test of
    the Windows release binary (run 38046929806 artifacts) on the WIPED dev
    device. (1) `init-device` → HTTP 401 "init JWT rejected": the init JWT
    carried the header {"ecdh":"x25519"}, copied from the Manager's CALL SITE
    (build.js:743-746) — but its jwt_generate_hs256 adds alg/typ
    (build.js:1981-1987), and the firmware's jwt_verify_head needs `alg`
    (libjwt jwt.c:512; JWT_ALG_INVAL → 401 at api_auth.c:424-430, before any
    signature check). Login always sent the full header, so only init was
    hit; the rev-2 unit test pinned the wrong header (self-consistency
    fixture). Fix: one EHEM_EJWT_HEADER constant (src/ejwt.h, comment cites
    the firmware lines) for login and init; test_init asserts it. The 401
    came before the cfg was read — the device stayed uninitialised (RTC set).
    (2) "ÔÇö" in the Windows console: 88 em dashes + 1 arrow in SDK and
    hem-tool string literals → ASCII (a comment-aware lexer rewrote only
    literals); new unit gate `ascii_strings` (tests/unit/check_ascii_strings.c,
    a portable C lexer so it runs in Windows CI too); REQ-API-009 drafted and
    approved. The packaging findings of the same test (Universal-CRT
    assertion, MSYSTEM in BUILD-CONFIG.txt, missing-DLL note, ASCII README
    texts) belong to REQ-BUILD-005 / STEP-M10-065.
reopened: []
cancelled: null
---

**Goal:** a device initialised by `hem-tool init-device` (REQ-TOOL-021 over
REQ-AUTH-011) on the first try — the init JWT header is what the Manager
sends and the firmware accepts — and every message the SDK and hem-tool
produce renders correctly in any console (REQ-API-009, enforced by a unit
gate).

**Notes:**
- Chosen over reopening STEP-M10-020 (§6.2 allows either): 020's history
  and evidence stay as they were; this step carries the correction.
- REQ-AUTH-011 went to `needs-reverify` (rev 3); the attended init below
  restores it at the next §4.3 run.
- Text that comes FROM the device (labels, names, hostnames) is passed
  through unchanged — REQ-API-009 covers only the sources' own literals.

**Definition of done**
- [x] The init JWT header is the full eJWT header, shared with login;
      test_init asserts the bytes and the `alg` / `ecdh` fields.
- [x] Every non-ASCII string literal in `src/` replaced; the release
      archives' README.txt texts are ASCII too.
- [x] `ascii_strings` gate in the unit suite, proven to fail on a bad
      sample and to ignore comments.
- [ ] Unit suite incl. `ascii_strings` green on Linux AND Windows CI (the
      next push / tag).
- [ ] Attended: `hem-tool init-device` succeeds on the wiped dev device
      with the rebuilt binary; evidence (date, `instanceid`,
      `reboot_required`, then login + `status`) recorded in REQ-AUTH-011
      and REQ-TOOL-021. The same sitting may serve the M10 gate's init leg.
- [ ] Attended: hem-tool output in a Windows console (legacy code page)
      shows no mojibake.
