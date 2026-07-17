---
id: STEP-M5-040
title: "M5 gate — full family matrix live; KEY-005/006 criteria closed; suite reliability; trace regen"
milestone: M5
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M5-010", "STEP-M5-020", "STEP-M5-030", "STEP-M5-035"]
evidence:
  commits:
    - "0f0d066 — M5 gate start"
    - "ddab3ae — M5 gate: TEST-004/TOOL-009/NET-006 verified, TRACE regen"
  tests:
    - "gate run 2026-07-17: ./dev test it 11/11 GREEN (test_system/checkin/config/keymgmt(list/mutate/search/get)/sign/keygen_matrix/auth/keys_rm) with EHEM_TEST_PACE_MS=150 + ctest --repeat until-pass:3 — the exact full-suite scenario that hard-hung the device twice pre-mitigation; device alive throughout, 167s, no retries needed. The gate criterion (all 23 fw types generate→list→sign(ExDSA)→delete) is inside test_keygen_matrix_live (93s of that run)."
    - "unit 22/22 gcc+clang + asan clean; export/header gates green; src/proto_common.c cross-compiles for x86_64-w64-mingw32"
  notes: >
    M5 COMPLETE (all steps M5-010..040 in done/). Gate criterion (ARCHITECTURE
    §11 M5, amended at decomposition): full per-family matrix live —
    generate → list → sign(where ExDSA-capable) → delete for all 23 fw v1.2.2
    key types — GREEN. Bookkeeping done at this gate: (1) three approved → verified
    transitions at the §4.3 regen — REQ-TEST-004 (matrix), REQ-TOOL-009
    (keys gen), REQ-NET-006 (request pacing). (2) REQ-KEY-005 (label/descr
    bounds, rev 2) and REQ-KEY-006 (per-family flag-set vocabulary incl. the
    new PQC token) open criteria closed during M5-010/020. (3) A device
    stall/hang under sustained load was DIAGNOSED this session (NOT PQC / not
    any single op / not connection volume — per-op probes cleared all 23
    families twice + 60 signs; the watchdog is firmware-disabled so a
    non-recovering stall needs a physical reboot) and MITIGATED (REQ-NET-006
    pacing + ctest --repeat stall-retry, STEP-M5-035); recorded in
    KNOWN-ISSUES.md. (4) TRACE.md regenerated: 46 REQs = 41 verified / 4
    implemented / 0 approved / 1 draft (REQ-OPS-002, deferred to M6). Windows
    CI green on push (user-confirmed post-push, like prior gates).
reopened: []
cancelled: null
---

**Goal:** The M5 milestone gate demonstrated live against the dev device:
the full per-family generation matrix green (generate → list → sign where
ExDSA-capable → delete, all 23 fw v1.2.2 types — §11 gate wording per the
2026-07-16 decomposition), plus the `keys gen` tool demo noted.
Bookkeeping: REQ-KEY-005's boundary criterion and REQ-KEY-006's flag-set
vocabulary criterion recorded and checked; REQ-TEST-004's open live
criteria recorded; TRACE.md regenerated per §4.3.

**Notes:** Chore step (implements: []) — gate + trace regen, mirrors
STEP-M2-070/M3-070/M4-060. Gate criterion per ARCHITECTURE §11 M5
(amended at decomposition): the matrix cycle per family on the real
device. Integration suite fully green (`./dev test it`), unit suites
green gcc/clang + ASan, Windows CI green. Record any device/doc
divergences found on the way in the affected REQs (device > doc).
Hardware random is out of M5 scope (no endpoint in fw v1.2.2) — its plan
is REQ-OPS-002 (draft, M6 encrypt-IV harvest); nothing to gate here.

**Definition of done**
- [x] Fresh full integration run green including the matrix test; unit
      gcc+clang + ASan green; Windows CI green on push. (./dev test it
      11/11 green 2026-07-17 with the pacing+retry mitigation.)
- [x] REQ-KEY-006 flag-set vocabulary criterion checked off with the
      recorded live strings; REQ-KEY-005 boundary criterion checked off
      (done in M5-010, still recorded); REQ-TEST-004 live criteria recorded.
- [x] `keys gen` live demo evidence present (M5-030) and referenced.
- [x] Device stall/hang diagnosed + mitigated (REQ-NET-006 + ctest retry,
      M5-035) so the suite runs reliably; recorded in KNOWN-ISSUES.md.
- [x] TRACE.md regenerated (§4.3); coverage report clean of new
      violations (46 REQs = 41 verified / 4 implemented / 0 approved /
      1 draft); summary reported in chat.
