---
id: REQ-AUTH-004
title: Automatic check-in recovery on clock-drift login failure
status: approved
priority: must
revision: 1
source: user decision 2026-07-17 (M7 decomposition; raised from the M6-010 finding — device RTC runs ~8% fast, logins 401 once drift exceeds the requested TTL, `hem-tool checkin` resyncs the clock; KNOWN-ISSUES.md "Device clock runs ~8% fast"); encedo_firmware api_auth.c api_post_auth_token (fw v1.2.2 — rejects an eJWT whose `exp` is already past device time with 401)
depends_on: ["REQ-AUTH-001", "REQ-SYS-003", "REQ-NET-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#7-transport"]
---

# Automatic check-in recovery on clock-drift login failure

When the login token POST fails with HTTP 401 **and** the challenge
metadata indicates device-clock drift beyond the eJWT skew margin, the
session engine SHALL run a single check-in (`ehem_checkin_run`) and retry
the login exactly once, honoring the existing `no_auto_checkin` opt-out.

- **Drift detection:** the challenge GET returns the device-side submit
  deadline `exp` (≈ device_now + 60 s, firmware gen_auth_token). If
  |`exp` − 60 − local_now| exceeds the 60 s skew margin, the device clock
  has drifted and a 401 on the subsequent POST is treated as
  drift-induced. Without drift evidence, a 401 stays
  `EHEM_ERR_AUTH_FAILED` immediately — a wrong passphrase MUST NOT
  trigger a check-in round-trip.
- **Single-shot, composed:** at most one check-in + one login retry per
  `ehem_auth_ensure_token` call; guarded by the existing `in_checkin`
  recursion flag; composes with — never multiplies — the challenge-GET
  403 recovery (M2-030) and the expired-cert recovery (REQ-NET-005).
- Check-in resynchronizes the device RTC as a firmware side effect of the
  3-leg relay (observed live 2026-07-17, M6-010 evidence); the SDK relies
  on that side effect and does not set the clock itself.
- If the retry also fails, the second failure maps as usual
  (`EHEM_ERR_AUTH_FAILED`); the check-in failure itself is reported only
  via last-error detail, never as the primary rc.

**Rationale:** the dev device's RTC gains ~8%; after ~12 h of uptime the
drift exceeds the requested token TTL (3600 s) and every login 401s
because the requested `exp` is already past device-side. A single
unauthenticated check-in heals it at a predictable moment. This is the
recovery half of the session-start check-in idea; REQ-AUTH-005 is the
proactive half. Keying the trigger on measured drift (not on bare 401)
keeps wrong-passphrase latency unchanged and avoids hammering the cloud
relay on genuine auth failures.

**Acceptance criteria:**
- [ ] Unit (fake transport, pinned clock): drifted challenge `exp` +
      401 POST → exactly one check-in, one challenge re-GET, one POST
      retry, success; second 401 after recovery → `EHEM_ERR_AUTH_FAILED`
      with no further check-in.
- [ ] Unit: 401 with un-drifted challenge `exp` (wrong passphrase
      scenario) → `EHEM_ERR_AUTH_FAILED` with zero check-in calls.
- [ ] Unit: `no_auto_checkin` set → no check-in, plain failure; recovery
      composes with the 403-RTC-unset and expired-cert paths (≤ 1
      check-in total per ensure_token call, transport-call count
      asserted).
- [ ] Live: login green against my.ence.do; if the device happens to be
      drifted at run time, the recovery path is exercised and logged
      (opportunistic — drift cannot be fabricated remotely; the unit
      seam carries the proof).
