---
id: STEP-M3-070
title: "M3 gate: goal.txt tool milestone live demo; TRACE regen"
milestone: M3
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M3-030", "STEP-M3-040", "STEP-M3-060"]
evidence:
  commits: []
  tests:
    - "ctest -L unit: 17/17 green on GCC + Clang (./dev ci); ASan/LSan clean (./dev test asan); export/header gates green (./dev check); MinGW cross-compile of proto_keymgmt.c + keys.c + main.c clean"
    - "ctest -L integration: 9/9 green against my.ence.do — system/checkin/config/auth live + the full keymgmt round-trips (list, create→list→delete, search, get, keys rm)"
  notes: >
    M3 gate (chore, implements: []): verified REQ-KEY-001..005 + REQ-TOOL-004..006
    + REQ-TEST-003, no new scope. goal.txt tool milestone demonstrated live on
    my.ence.do (2026-07-16):
    - `hem-tool keys list` → 'TLS PrivateKey'/'TLS Certificate'/'SM-S938B (Android)'
      marked [PROTECTED]; summary "N key(s), 3 protected".
    - EHEMTEST create + `keys rm --label-prefix EHEMTEST --yes` → "2 deleted,
      0 failed" (test_keys_rm_live), keys gone from list.
    - `keys rm --all --dry-run` → regular targets EXCLUDE the TLS pair + Android
      phone; "dry-run: no keys deleted" (guard refuses bulk removal). No protected
      key is ever actually deleted on the device.
    Open criteria resolved/carried: REQ-KEY-002 no-match = HTTP 200 empty list on
    fw v1.2.2 (not 404; firmware api_keymgmt.c); REQ-KEY-003 scope probe =
    keymgmt:get + keymgmt:gen both accepted (recorded in ARCHITECTURE §12 risk 3);
    REQ-KEY-005 label/descr max-size probe explicitly deferred to M5; also found
    GET omits descr on this firmware (REQ-KEY-003). Source implements:/verifies:
    tags updated to cover all M3 REQs; TRACE.md regenerated (§4.3) — 34 verified,
    4 implemented, 0 approved; coverage report clean (no unimplemented, no orphan
    tags, no broken anchors). REQ-TEST-003 hygiene: 0 EHEMTEST keys left on the
    device after the suite (audited: every live delete path is EHEMTEST-only).
    Windows MinGW CI runs on the user's push (local cross-compile clean).
reopened: []
cancelled: null
---

**Goal:** Milestone gate (chore step — verification, no new REQ): the
goal.txt tool milestone demonstrated live against the dev device —
`hem-tool keys list` shows the repo with the TLS pair marked
`[PROTECTED]`; create EHEMTEST keys, `keys rm --label-prefix EHEMTEST
--yes` removes them; `keys rm --all --dry-run` demonstrably excludes the
protected keys from bulk removal. `ctest -L integration` fully green
with EHEM_TEST_URL + EHEM_TEST_PASSPHRASE (now including the keymgmt
create→list→search→get→delete round-trips). Open criteria resolved or
explicitly carried: REQ-KEY-002 no-match status code, REQ-KEY-003 scope
probe (recorded in §12 risk 3), REQ-KEY-005 label/descr limits (may
carry to M5). Regenerate TRACE.md (§4.3).

**Notes:** Chore/gate step, `implements: []` per §5.1 — it verifies
REQ-KEY-001..005 + REQ-TOOL-004..006 + REQ-TEST-003 rather than adding
scope. No protected key is ever deleted on the dev device — the guard
demo is dry-run only. Verify no EHEMTEST leftovers remain on the device
after the full suite (REQ-TEST-003 hygiene). After the gate: TRACE
regeneration once the user has committed the M3 work.

**Definition of done**
- [x] Live demo recorded: keys list with protected marks; EHEMTEST
      create + keys rm removal; --all --dry-run excludes protected keys.
      (Recorded in Notes; keys list + rm demos green.)
- [x] `ctest -L integration` green (unit suites green on GCC + Clang;
      ASan/LSan clean; CI green on Linux + Windows MinGW). (Integration 9/9;
      unit 17/17 gcc+clang; ASan clean; MinGW cross-compile clean — the
      Windows CI job runs on push, like the M3-010 %zu fix.)
- [x] REQ open criteria resolved with live facts or explicitly deferred
      with a note (KEY-002 404 fact, KEY-003 scope probe → §12 risk 3,
      KEY-005 limits); no EHEMTEST leftovers on the device. (KEY-002 =
      200-empty; KEY-003 scope probe in §12 risk 3; KEY-005 → M5; 0 EHEMTEST
      keys remain.)
- [x] TRACE.md regenerated (§4.3); coverage report clean for M3 REQs.
      (34 verified / 4 implemented / 0 approved; no unimplemented/orphan/
      broken-anchor findings.)
