---
id: STEP-M1-100
title: M1 gate — live device verification, TLS model recorded
milestone: M1
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M1-080", "STEP-M1-090"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] `hem-tool status` prints live data from the dev-machine HEM (output captured in evidence.notes)
- [ ] Device TLS certificate model (self-signed / device CA / public CA) recorded in REQ-NET-003's open criterion, including which trust mode the gate run used
- [ ] Live status/version responses compared to doc; findings recorded in REQ-SYS-001/-002 open criteria
- [ ] `ctest -L integration` green against the device on the dev machine
