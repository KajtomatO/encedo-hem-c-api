---
id: STEP-M1-100
title: M1 gate — live device verification, TLS model recorded
milestone: M1
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M1-080", "STEP-M1-090"]
evidence:
  commits: []   # to be recorded at commit time (user runs commits)
  tests: ["verifies (live): REQ-SYS-001, REQ-SYS-002, REQ-NET-003 — tests/integration/test_system_live.c green against the dev-machine HEM"]
  notes: >
    M1 gate PASSED against the real dev-machine HEM (https://my.ence.do,
    fw v1.2.2-DIAG) on 2026-07-15.

    hem-tool status (--insecure) live output:
      Device: https://my.ence.do
        uptime:     402 s
        temp:       38.0 C
        storage:    8388607:ro, 234364924:-
        hardware:   PPA rev 2.2
        firmware:   Encedo nGINE FW v1.2.2-DIAG
        bootloader: Encedo Secure Bootloader v2.0.1

    `EHEM_TEST_URL=https://my.ence.do EHEM_TEST_INSECURE=1 ctest -L integration`
    → test_system_live PASSED (live status+version round-trip).

    TLS model (recorded in REQ-NET-003): PUBLIC-CA cert (CN=my.ence.do, issuer
    ZeroSSL ECC Domain Secure Site CA) — not self-signed / not per-device CA —
    but EXPIRED (valid 2026-01-18 → 2026-04-18). System trust correctly failed
    (EHEM_ERR_NETWORK "certificate has expired"); the gate used EHEM_TLS_INSECURE.
    EHEM_TLS_SYSTEM is the intended default once the cert is renewed. This also
    live-confirmed the TLS-failure → NETWORK and the connect-timeout → UNREACHABLE
    (earlier, device offline) error mappings.

    Response shapes (recorded in REQ-SYS-001/-002): the SDK structs match the
    device; required fields all present. Notable: storage elements are
    "<bytes>:<flag>" strings (e.g. "8388607:ro"), differing from the doc's
    "disk0_status" placeholder but parsed fine; ts/time present (RTC set),
    validating their OPTIONAL treatment; optional status fields
    (hostname/inited/https/...) absent on this DIAG firmware, correctly reported
    absent. No requirement rewording needed (no §6.2 change), only open-criterion
    boxes checked with findings.
reopened: []
cancelled: null
---

**Goal:** The M1 gate passed and documented: `hem-tool status` succeeds
against the real dev-machine HEM, the device's TLS certificate model is
identified, and the open acceptance criteria in REQ-NET-003, REQ-SYS-001,
and REQ-SYS-002 are resolved with recorded findings.

**Notes:** Verification chore — `implements` is empty because this step
produces evidence for already-implemented REQs rather than new code
(§5.1). Cross-check the Python client's TLS handling
(encedo-hem-python-api) against what the device presents. If the real
response shapes diverge from the doc so that REQ-SYS-001/-002 acceptance
criteria text must change, run the §6.2 impact-analysis procedure before
editing — checking an open criterion box with findings is not a §6.1
change, rewording a criterion is.

**Definition of done**
- [x] `hem-tool status` prints live data from the dev-machine HEM (output captured in evidence.notes) — *uptime/temp/storage/hw/fw/bootloader printed*
- [x] Device TLS certificate model (self-signed / device CA / public CA) recorded in REQ-NET-003's open criterion, including which trust mode the gate run used — *public CA (ZeroSSL), expired → gate used EHEM_TLS_INSECURE; SYSTEM is the default once renewed*
- [x] Live status/version responses compared to doc; findings recorded in REQ-SYS-001/-002 open criteria — *shapes match structs; storage `<bytes>:<flag>`, ts/time present; recorded*
- [x] `ctest -L integration` green against the device on the dev machine — *test_system_live PASSED against https://my.ence.do*
