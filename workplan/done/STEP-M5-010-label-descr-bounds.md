---
id: STEP-M5-010
title: "Label/descr boundary probe (32/33, 64/65) + relax SDK label bound 31→32"
milestone: M5
implements: ["REQ-KEY-005"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
depends_on: []
evidence:
  commits:
    - "52e2263 — label 31->32, descr cap 64; boundary probe findings"
  tests:
    - "test_keymgmt (verifies: REQ-KEY-005): test_create_label_too_long (33 → ARG, 0 req), test_create_label_max_ok (32 → body reaches device), test_create_descr_too_long (65 → ARG, 0 req), test_create_descr_max_ok (64 → 3 req, body sent); ./dev ci 21/21 gcc+clang + asan clean; export/header gates green"
  notes: >
    Live boundary probe 2026-07-16 (scratchpad bounds_probe.c, internal
    request path to bypass client validation; EHEMTEST keys deleted
    immediately): LABEL — 32 bytes accepted (kid returned), 33 → device
    400. DESCR — both 64 and 65 bytes accepted on CREATE (device
    create-side check is broken: isvalid_base64 tests the base64 length
    with the loop counter already decremented to -1, misc.c:577/597), but
    the get-side readback uses a fixed STOREDKEY_DESCR_MAX_LENGTH (64)
    buffer with a clamped copy (repo.c REPO_GetKey_byKID), so >64 truncates
    silently on read. SDK decision: cap descr at 64 client-side
    (EHEM_ERR_ARG) to refuse silent-data-loss keys — matches ARCHITECTURE
    §2 and repo.h:65. SDK label bound relaxed 31→32 (device max). Both
    recorded in REQ-KEY-005 (rev 2, open criterion RESOLVED) and the
    keymgmt.h create-params doc.
reopened: []
cancelled: null
---

**Goal:** REQ-KEY-005's open boundary criterion resolved with live device
facts: label of exactly 32 printable bytes accepted, 33 rejected (400);
descr of 64 bytes accepted, 65 rejected (400) — matching the firmware
source (`isvalid_label(label, 32)`, `isvalid_base64(descr, 64)`,
api_keymgmt.c:808/838, repo.h:64-65). The SDK's client-side label
pre-validation relaxed from python's ≤31 to the device's ≤32 per the
REQ's own written contingency; a client-side descr cap decision made and
recorded (recommended: cap at 64, fail fast — device is ground truth).

**Notes:** Small step. Probe uses EHEMTEST-labeled keys (32-char label
must still carry the EHEMTEST prefix) deleted immediately (REQ-TEST-003).
Record in REQ-KEY-005: the resolved bounds, the python-client divergence
(≤31 over-strict; its ≤128 descr would 400 on this fw). Update
proto_keymgmt.c label check + unit tests (31→32 boundary cases both
sides); if the descr cap is added, unit tests for 64/65. Revision bump on
REQ-KEY-005 — this applies the REQ's stated contingency, not a meaning
change.

**Definition of done**
- [x] Live probe run and results recorded in REQ-KEY-005 (32 ok / 33
      rejected; 64 AND 65 accepted on create — device diverges from the
      simple ≤64 model because its create-side length check is broken, but
      readback truncates at 64; device behavior recorded, device wins).
- [x] SDK label bound updated to the probed limit (31→32); unit tests
      cover both sides of the boundary (33→ARG, 32→sent); `./dev ci` green
      (gcc+clang) + asan clean.
- [x] Descr-cap decision recorded in REQ-KEY-005 and implemented (cap 64,
      EHEM_ERR_ARG) with boundary unit tests (65→ARG, 64→sent).
- [x] REQ-KEY-005 open criterion checked off, revision bumped (rev 2).
