---
id: STEP-M10-068
title: "Fixes from the first Windows test of the release binary: init JWT header (REQ-AUTH-011 rev 3), ASCII-only text (REQ-API-009), no recovery hints in init-device / wipe-device (REQ-TOOL-021 rev 3, REQ-TOOL-022 rev 2)"
milestone: M10
implements: ["REQ-AUTH-011", "REQ-API-009", "REQ-TOOL-021", "REQ-TOOL-022"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M10-020"]
evidence:
  commits: ["925e831", "6204c13"]   # the user's "Fixes after testing hem-tool on windows" (header + ASCII code and tests) and "Additonal post test cleanup" (recovery hints removed, attended evidence); no [STEP-…] prefixes
  tests:
    - "verifies: REQ-AUTH-011 — tests/unit/test_init.c: the posted init JWT's header is byte-exact EHEM_EJWT_HEADER and carries alg + ecdh (unit 47/47 gcc+clang, 2026-10-10)"
    - "REQ-TOOL-021 / REQ-TOOL-022 rev 3 / rev 2 — output/help text only, no assertion on it in test_init_tool.c / test_wipe.c; ./dev ci 47/47 gcc + clang, all legs green (2026-10-10)"
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

    2026-10-10, user report from the v1.1.0-rc4 Windows binary on the wiped
    dev device: "device initialized, key created, text signed, recovery run
    [not needed], device wiped". The attended boxes below stay open until
    the console output (instanceid, reboot_required, status) is recorded.
    Afterwards (read-only, rc4 Linux binary + bundled wolfSSL): `status` →
    `inited: no`, `https: yes`, and the HTTPS certificate still verifies
    under system trust — this wipe did NOT remove the TLS material, unlike
    the 2026-07-22 one that the init/wipe "next: ... recovery" hints were
    written from. Linux the same day (user: "I done tests on linux
    evrything works") after the libwolfssl placement fix; the device was
    wiped again afterwards (status: inited no, https yes).

    2026-10-10, user decision "No, remove any hints about recovery" (my
    proposal of a conditional HTTPS check was declined): the init-device
    "next:" line keeps only the reboot + passphrase-login hint, the
    wipe-device "next:" line names init-device only, and both help texts
    drop "then `recovery`". §6.2 (in chat): REQ-TOOL-021 rev 3 and
    REQ-TOOL-022 rev 2 → draft (re-approval pending); STEP-M10-040 /
    STEP-M10-030 (done/) stay closed — this step carries the rework; no
    test asserts the hint text; the SDK header notes on ehem_tls_recover
    (auth.h, system.h — REQ-AUTH-011 / REQ-SYS-014) are API documentation,
    not hem-tool hints, and are unchanged.
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
- [x] Unit suite incl. `ascii_strings` green on Linux AND Windows CI (the
      next push / tag). *(CI run 38052272594 — Linux gcc, Linux clang,
      Windows (MinGW) "Unit tests" green — and the Release build's unit
      suite in run 38052272610, both platforms; v1.1.0-rc4, 339af96,
      2026-10-10.)*
- [x] Attended: `hem-tool init-device` succeeds on the wiped dev device
      with the rebuilt binary; evidence (date, `instanceid`,
      `reboot_required`, then login + `status`) recorded in REQ-AUTH-011
      and REQ-TOOL-021. The same sitting may serve the M10 gate's init leg.
      *(2026-10-10, v1.1.0-rc4, Windows and Linux — user reports; recorded
      in REQ-AUTH-011 / REQ-TOOL-021 / REQ-TOOL-022 / REQ-SYS-014.)*
- [x] init-device and wipe-device print no recovery hint, in output or
      help (REQ-TOOL-021 rev 3, REQ-TOOL-022 rev 2); `./dev ci` green.
      *(2026-10-10, 47/47 gcc + clang.)*
- [x] REQ-TOOL-021 rev 3 and REQ-TOOL-022 rev 2 approved by the user.
      *(2026-10-10, "OK".)*
- [x] Attended: hem-tool output in a Windows console (legacy code page)
      shows no mojibake.
