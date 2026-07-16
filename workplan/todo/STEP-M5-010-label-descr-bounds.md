---
id: STEP-M5-010
title: "Label/descr boundary probe (32/33, 64/65) + relax SDK label bound 31→32"
milestone: M5
implements: ["REQ-KEY-005"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Live probe run and results recorded in REQ-KEY-005 (32 ok / 33
      rejected; 64 ok / 65 rejected — or the device's actual behavior if
      it diverges from source; device wins and is recorded).
- [ ] SDK label bound updated to the probed limit; unit tests cover both
      sides of the boundary; `./dev ci` green (gcc+clang) + asan clean.
- [ ] Descr-cap decision recorded in REQ-KEY-005 and implemented if
      adopted (with boundary unit tests).
- [ ] REQ-KEY-005 open criterion checked off, revision bumped.
