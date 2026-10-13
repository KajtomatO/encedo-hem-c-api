---
id: REQ-NET-005
title: Automatic certificate recovery via check-in on expired-cert failure
status: needs-reverify
priority: must
revision: 3   # 2026-10-13 (STEP-M12-030): rework done — the classifier no longer accepts -150; criteria recorded against the tests (no meaning change)
source: user decision 2026-07-15 ("Lib should run check-in automatically when needed, e.g. when TLS is expired"); M1 gate finding (device cert expired 2026-04-18); rev 2 — M12 decomposition 2026-10-12 (§6.2 report approved in chat): the classifier of STEP-M10-065 also accepts wolfSSL verify result `-150`, which is `ASN_BEFORE_DATE_E` (certificate NOT YET valid) in wolfSSL 5.6.6 and 5.7.2 alike — not an older spelling of "expired" as REQ-BUILD-005's record claimed — so the "only expired" criterion is violated; an explicit not-yet-valid criterion is added, status → needs-reverify, rework = STEP-M12-030 (user decision 2026-10-12, ARCHITECTURE.md §11 M12 part A)
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
- [x] Expired-cert failure with auto-recovery enabled: check-in runs, the
      original request is retried once on a fresh connection, and the caller
      sees the retried result (unit test via fake transport asserting the
      request sequence).
      *(`tests/unit/test_checkin.c` `test_auto_recovery`: 5 requests — status,
      the three check-in legs, the retry on a fresh connection under the
      normal posture — caller gets the retried result, `ehem_cert_refreshed`
      true.)*
- [x] The trigger classifies **only** expired certificates (TLS verify result
      `certificate has expired` — OpenSSL verify result 10, wolfSSL `-151`
      `ASN_AFTER_DATE_E`, or the stacks' texts); other TLS failures return
      unchanged (unit test).
      *(2026-10-13, STEP-M12-030: the verdict is the pure, hidden
      `ehem_curl_tls_expired()` (`src/transport_curl.c`, declared in
      `transport_curl.h`), unit-tested in `tests/unit/test_transport_curl.c`
      `test_tls_expired_classifier` — expired by code (10, -151) and by text,
      self-signed / hostname / unknown-CA / non-TLS codes unclassified;
      `test_checkin.c` `test_non_expired_failure_no_recovery` pins the
      unchanged return. Measured the same day against local endpoints: libcurl
      8.5.0 + OpenSSL 3.0.13 reports verify result 10 and "SSL certificate
      problem: certificate has expired"; the release pin libcurl 8.10.1 +
      wolfSSL 5.7.2 reports verify result **0** and "server verification
      failed: certificate has expired." — there the text decides.)*
- [x] Not-yet-valid is NOT expired (rev 2, 2026-10-12): a verify result of
      wolfSSL `-150` (`ASN_BEFORE_DATE_E`) or OpenSSL `9`
      (`X509_V_ERR_CERT_NOT_YET_VALID`) returns the original TLS error
      unchanged — no check-in leg, `tls_expired` false (unit test). *(The
      STEP-M10-065 classifier accepted `-150` on a mistaken record; `-150`
      is "not yet valid" in 5.6.6 and 5.7.2 alike. Rework: STEP-M12-030.)*
      *(2026-10-13, STEP-M12-030: `-150` removed; `9` / `-150` now return 0
      before the text fallback, so no wording can turn them into "expired";
      the texts "certificate is not yet valid" (OpenSSL), "certificate not
      valid yet." (libcurl 8.10.1 + wolfSSL 5.7.2, verify result 0) and
      "current date before" are unclassified — `test_tls_expired_classifier`;
      `test_checkin.c` `test_not_yet_valid_no_recovery`: one request, the
      original `EHEM_ERR_NETWORK` with the not-yet-valid text,
      `tls_expired` 0, no check-in. End to end against real handshakes:
      `scripts/release/check-expired-classifier.sh` now also serves a
      not-yet-valid leaf (`make-expired-cert.py` mints `future-*.pem`) and
      requires that `hem-tool recovery` does NOT diagnose it as expired while
      its https probe fails on the date — passed 2026-10-13 on the dev build
      (libcurl 8.5.0 / OpenSSL) and on a Release build against the pinned
      wolfSSL 5.7.2 + libcurl 8.10.1 prefix, both cases each; the release
      job runs it on every build.)*
- [x] `no_auto_checkin` option disables the behavior: no extra requests, the
      original error is returned (unit test).
      *(`test_checkin.c` `test_auto_recovery_optout`: one request, the
      original error, no refresh.)*
- [x] Recovery failure (check-in itself fails): the ORIGINAL TLS error is
      returned with `ehem_last_error` detail noting the failed recovery
      attempt; no recursion (check-in legs never trigger auto-recovery —
      guard unit test).
      *(`test_checkin.c` `test_recovery_failure_reports_original` (detail
      "automatic check-in recovery failed") and `test_recursion_guard` (an
      expired failure inside the check-in: 2 requests, no nested recovery).)*
- [x] A performed refresh is queryable via `ehem_cert_refreshed(ctx)` so
      consumers (hem-tool, REQ-TOOL-002) can inform the user.
      *(`test_auto_recovery` true after an effective refresh;
      `test_device_still_serves_old_cert` false when the retry still fails.)*
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
