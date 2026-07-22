---
id: STEP-M8-080
title: Attended real-phone validation — pair via terminal QR, approve, reject, timeout
milestone: M8
implements: []
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M8-070"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the real-world proof no simulation can give: with the user
present and the Encedo mobile app on their phone (confirmed available,
user 2026-07-22) — `hem-tool ext pair` scanned from the terminal QR
completes registration; `ext login` demonstrates ALL THREE terminals
live (approve → working bearer; reject → USER_REJECTED exit code;
unanswered → CONFIRM_TIMEOUT exit code); broker terminal shapes
captured verbatim.

**Notes:** chore-style `implements: []` — this step VERIFIES open
acceptance criteria across REQ-AUTH-008/009/010 and REQ-TOOL-016 rather
than implementing a new REQ (the REQ-SYS-008 attended precedent).
Session plan: enable disruptive label; `ext pair` (QR scan) → `ext
list` shows the phone (note whether its label trips the protected
classifier) → `ext login` approve / reject / let-expire → capture
`event/check` responses verbatim (the post-expiry shape especially) →
record everything in REQ-AUTH-008/-009 criteria + KNOWN-ISSUES if
surprising → decide with the user whether the phone pairing stays on
the dev device (if it stays: unattended broker-test etiquette is
already covered by the disruptive gate; if not: `keys rm` unpair,
protected ritual expected). Findings that contradict the approved REQs
trigger §6.2, not silent edits.

**Definition of done**
- [ ] Attended session run with the user; all three login terminals
      demonstrated via `ext login` exit codes
- [ ] Broker shapes (approved/deny/expired) recorded in REQ-AUTH-008;
      REQ-AUTH-009/-010 and REQ-TOOL-016 attended criteria checked off
- [ ] Pairing disposition decided + executed; device state recorded
- [ ] Evidence filled (transcript notes; commits if fixes were needed)
