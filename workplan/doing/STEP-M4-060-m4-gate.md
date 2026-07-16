---
id: STEP-M4-060
title: "M4 gate — live sign → local wolfCrypt verify; §12 risk 3 closed; trace regen"
milestone: M4
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M4-010", "STEP-M4-020", "STEP-M4-030", "STEP-M4-040", "STEP-M4-050"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** The M4 milestone gate demonstrated live against the dev device:
create EHEMTEST SECP256R1 (mode ExDSA) + ED25519 keys → sign via the SDK
→ signatures verify locally with wolfCrypt → keys deleted; the tool path
demonstrated by hand (`hem-tool sign` piped/checked against
`keys pub --raw` material); classifier size constants cross-checked
against the live material. Bookkeeping: §12 risk 3 marked RESOLVED with
the recorded scope-probe facts; REQ open criteria recorded; TRACE.md
regenerated per §4.3.

**Notes:** Chore step (implements: []) — gate + trace regen, mirrors
STEP-M2-070/M3-070. Gate criterion per ARCHITECTURE §11 M4: "signature
produced via the SDK verifies locally with wolfCrypt". Integration suite
must be fully green (`./dev test it`), unit suites green on gcc/clang +
ASan; Windows CI green. Record any device/doc divergences found on the
way in the affected REQs (device > doc).

**Definition of done**
- [ ] Live gate run green: create → sign → local verify → delete for both
      families; `hem-tool sign` + `keys pub` demo performed and noted in
      evidence.
- [ ] ARCHITECTURE §12 risk 3 rewritten RESOLVED (exact per-KID scope for
      crypto ops; get/sign token sharing; probe results).
- [ ] All REQ-OPS-001 / REQ-KEY-006 / REQ-TOOL-007 / REQ-TOOL-008 open
      criteria recorded (checked or explicitly deferred with reason).
- [ ] TRACE.md regenerated (§4.3); coverage report clean of new
      violations; summary reported in chat.
