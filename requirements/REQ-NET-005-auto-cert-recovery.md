---
id: REQ-NET-005
title: Automatic certificate recovery via check-in on expired-cert failure
status: verified
priority: must
revision: 1
source: user decision 2026-07-15 ("Lib should run check-in automatically when needed, e.g. when TLS is expired"); M1 gate finding (device cert expired 2026-04-18)
depends_on: ["REQ-SYS-003", "REQ-NET-002", "REQ-NET-003", "REQ-API-004"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#7-transport", "ARCHITECTURE.md#12-risks--open-questions"]
---

# Automatic certificate recovery via check-in on expired-cert failure

When a device request fails TLS verification **specifically because the
device's certificate has expired**, the SDK SHALL automatically run the
check-in flow (REQ-SYS-003) to refresh the certificate and then retry the
original request once, on a fresh connection, under the context's normal TLS
mode.

**Rationale:** The device's certificate lapses whenever no check-in has run
for a while (found at the M1 gate); without recovery every SDK consumer hits
an opaque TLS failure. Expiry is the one verification-failure class that is
(a) precisely classifiable from the TLS stack and (b) recoverable by
check-in. Broader failures (self-signed, hostname mismatch) stay hard errors
— auto-relaxing on *any* verification failure would let an active MITM
trigger the relaxed path (user-confirmed trigger scope, 2026-07-15).

**When check-in runs** (research: Encedo Manager runs it at every launch to
refresh the cert, set the RTC, and discover updates):
- **Automatic:** the expired-certificate failure above — enabled by default,
  disabled via a context option (`no_auto_checkin`).
- **Manual:** `ehem_system_checkin()` for session-start refresh, RTC-unset
  recovery, or update discovery. These are documented, not automated (a
  read-only call must not mutate device state implicitly).

**Acceptance criteria:**
- [ ] Expired-cert failure with auto-recovery enabled: check-in runs, the
      original request is retried once on a fresh connection, and the caller
      sees the retried result (unit test via fake transport asserting the
      request sequence).
- [ ] The trigger classifies **only** expired certificates (TLS verify result
      `certificate has expired`); other TLS failures return unchanged (unit
      test).
- [ ] `no_auto_checkin` option disables the behavior: no extra requests, the
      original error is returned (unit test).
- [ ] Recovery failure (check-in itself fails): the ORIGINAL TLS error is
      returned with `ehem_last_error` detail noting the failed recovery
      attempt; no recursion (check-in legs never trigger auto-recovery —
      guard unit test).
- [ ] A performed refresh is queryable via `ehem_cert_refreshed(ctx)` so
      consumers (hem-tool, REQ-TOOL-002) can inform the user.
- [x] RESOLVED (live, 2026-07-15): demonstrated against the real dev-machine
      HEM with its expired certificate. `hem-tool status` under system trust
      triggered the full recovery: expired-cert classification (curl
      CURLE_PEER_FAILED_VERIFICATION + CURLINFO_SSL_VERIFYRESULT==10) →
      3-leg check-in through https://api.encedo.com (succeeded, device
      accepted the cert update) → fresh-connection retry. The retry still
      failed because the device keeps serving the old certificate until
      reboot (see REQ-SYS-003 finding), so the SDK returned the ORIGINAL
      error with the explanatory detail "...check-in completed and the device
      accepted a certificate update, but it still serves the old certificate —
      a device reboot may be required to apply it". Semantics pinned by this
      finding: `ehem_cert_refreshed()` reports true only when the refresh
      took EFFECT (retry verified), not merely when the check-in flow
      completed. Full happy-path (retry succeeds → caller gets the result +
      cert_refreshed) is covered by unit tests; live re-verification of the
      happy path awaits a device whose cert applies immediately or a
      post-reboot run.
- [x] FOLLOW-UP (2026-07-16): a device reboot did NOT apply the cert. Root
      cause is a firmware v1.2.2 bug (see REQ-SYS-003 root-cause finding):
      `api_post_checkin_newcrt_callback` never updates the flash key repo on
      an initialised device (dead code behind a commented-out guard), so
      auto-recovery can never take live effect against fw 1.2.2 — the cloud
      does deliver a fresh cert, and the SDK's behavior (return the ORIGINAL
      error + "still serves the old certificate" detail, cert_refreshed
      false) is confirmed correct. Live happy-path verification is deferred
      until a firmware with fixed newcrt handling, or until the cert is
      installed manually via `POST /api/system/config` `{"tls":{"crt":…}}`
      + reboot (after which the trigger no longer reproduces until the next
      expiry).
