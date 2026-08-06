---
id: STEP-M8-080
title: Attended real-phone validation — pair via terminal QR, approve, reject, timeout
milestone: M8
implements: []
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M8-070"]
evidence:
  commits: ["dbe2bb5", "06c4b29"]
  tests: []
  notes: "ATTENDED session 2026-08-05/06, user + Encedo app on SM-S938B (Android). ALL legs green: ext pair via TERMINAL QR completed registration (phone label hits the (Android) protected classifier — [PROTECTED] in ext list, exactly the TOOL-005 design); ext login approve/reject/timeout = exit 0/4/3 BACK-TO-BACK. THE BIG FIND: the user's second login 401'd at event/new — the broker validates the authreq iat against ITS clock ('Cannot handle token prior to (iat …)') and the device RTC runs ~8% fast, so mobile login broke within minutes of the last sync; FIXED with the drift-gated single-checkin re-fire in ehem_ext_confirm_begin (REQ-AUTH-009 rev 2, unit-proven + live-proven by the back-to-back run; local clock NTP-verified before trusting drift math). Broker shapes captured VERBATIM (REQ-AUTH-008 rev 3/4): session {epk, exp≈24h, paired}; sessions server-cached; event/new 200 {eventid, sentcnt, ipinfo_you — broker GEOLOCATES the caller and forwards it}; event/check 202 / 200+authreply (replayable read) / 200 {\"deny\":true} / 404 unknown-or-expired. Phone-app finding: authreply exp = iat+900 → real-phone bearers live 15 min regardless of scope. Pairing KEPT on the device (user decision) for future mobile testing. Chore step (implements []): its output is the criteria closures across REQ-AUTH-008/009/010, REQ-TOOL-016, REQ-TEST-006."
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
- [x] Attended session run with the user; all three login terminals
      demonstrated via `ext login` exit codes (0/4/3)
- [x] Broker shapes (approved/deny/expired→404) recorded in REQ-AUTH-008;
      REQ-AUTH-009/-010 and REQ-TOOL-016 attended criteria checked off
- [x] Pairing disposition decided + executed: KEPT (protected label
      guards it from bulk rm); device state recorded in ext list output
- [x] Evidence filled (incl. the drift-recovery fix dbe2bb5 this step
      surfaced)
