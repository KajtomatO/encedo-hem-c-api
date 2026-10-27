---
id: STEP-M12-030
title: "Expired-cert classifier: drop wolfSSL -150 (ASN_BEFORE_DATE_E = not yet valid), pin the not-yet-valid case with a unit test (REQ-NET-005 rev 2)"
milestone: M12
implements: ["REQ-NET-005"]
traces:
  architecture: ["ARCHITECTURE.md#7-transport", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: []
evidence:
  commits: ["31f723b", "c7a48a6"]   # start; implementation (the done commit follows)
  tests:
    - "tests/unit/test_transport_curl.c test_tls_expired_classifier (verifies: REQ-NET-005) — the pure verdict ehem_curl_tls_expired(): expired by code (10, -151) and by text incl. the measured wolfSSL-libcurl wording; NOT expired: -150, 9, 'certificate is not yet valid', 'certificate not valid yet.', 'current date before', and 9/-150 win over any text; self-signed / hostname / unknown CA / non-TLS codes unclassified"
    - "tests/unit/test_checkin.c test_not_yet_valid_no_recovery (verifies: REQ-NET-005) — one request, the original EHEM_ERR_NETWORK with its text, tls_expired 0, no check-in leg; the existing recovery tests (auto, opt-out, non-expired, failure, old-cert, recursion) unchanged and green"
    - "scripts/release/check-expired-classifier.sh — extended with the not-yet-valid case (make-expired-cert.py mints future-cert.pem): passed 2026-10-13 on the dev build (libcurl 8.5.0 + OpenSSL 3.0.13) and on a Release build against the pinned wolfSSL 5.7.2 + libcurl 8.10.1 prefix (the 2026-10-09 build-deps prefix), both cases each; the release job runs it"
    - "./dev ci 51/51 gcc + clang; ./dev test asan 51/51; MinGW -fsyntax-only clean for transport_curl.c, test_transport_curl.c, test_checkin.c (Linux cmocka.h on the include path); shellcheck clean"
  notes: |
    The classifier moved out of curl_send into the pure, hidden
    ehem_curl_tls_expired(CURLcode, long verify_result, const char *detail)
    next to ehem_curl_map_error in transport_curl.h (the plan's suggested
    name was ehem__curl_classify_tls_failure — the existing ehem_curl_*
    naming won). Verdict: TLS verification codes only (PEER_FAILED_VERIFICATION,
    SSL_CONNECT_ERROR); 10 / -151 → expired; 9 / -150 → NOT expired before
    any text is consulted; else the texts "expired", "date error, current
    date after", "ASN_AFTER_DATE". The implements: REQ-NET-005 tag now sits on
    the classifier itself.
    MEASURED (scratchpad vr.c against openssl s_server endpoints): libcurl
    8.10.1 + wolfSSL 5.7.2 report CURLE_PEER_FAILED_VERIFICATION with
    CURLINFO_SSL_VERIFYRESULT 0 for BOTH an expired and a not-yet-valid
    leaf, texts "server verification failed: certificate has expired." /
    "... certificate not valid yet." — so on the release pin the TEXT
    decides and the old `-150` branch could never fire there; the bug was
    latent (a libcurl surfacing wolfSSL's raw code would have hit it).
    libcurl 8.5.0 + OpenSSL 3.0.13: 10 / 9 with "SSL certificate problem:
    certificate has expired" / "... is not yet valid".
    Release check: a second openssl s_server on port+1 serves the
    not-yet-valid leaf; the script requires the EXPIRED diagnosis to be
    ABSENT and the https probe to fail with EHEM_ERR_NETWORK on the date
    wording (OpenSSL / wolfSSL-libcurl / wolfSSL texts). First wolfSSL run
    failed only on my regex (it lacked "not valid yet"), fixed before the
    commit.
    Bonus from the same Release build (pinned prefix): configure printed
    "crypto wolfSSL 5.7.2 (dynamic), transport libcurl 8.10.1 (static)",
    unit 51/51 against 5.7.2, and the binary WITHOUT its companion library
    on this host (system wolfSSL 5.6.6) refused with the REQ-TOOL-024
    message and exit 12 instead of crashing — a dev-build preview of the
    M12 gate demo (recorded in REQ-API-010 / REQ-TOOL-024; the gate's demo
    with the published binary stays open).
reopened: []
cancelled: null
---

**Goal:** the REQ-NET-005 trigger classifies *only* expired certificates
again: `src/transport_curl.c` no longer treats wolfSSL verify result
`-150` as "expired" (it is `ASN_BEFORE_DATE_E`, certificate not yet valid,
in wolfSSL 5.6.6 and 5.7.2 alike), and a unit test pins the not-yet-valid
case for both TLS stacks (wolfSSL `-150`, OpenSSL `9`
`X509_V_ERR_CERT_NOT_YET_VALID`) as NOT recoverable — original error
returned, no check-in, `tls_expired` false — while the positive cases
(`-151` `ASN_AFTER_DATE_E`, OpenSSL `10`, the texts) stay green.
REQ-NET-005 returns from `needs-reverify` to `verified` at the next §4.3.

**Notes:** The classifier is at `src/transport_curl.c:353`
(`vr == 10 || vr == -151 || vr == -150 || strstr "expired" ||
"ASN_AFTER_DATE"`); the comment above it (lines 345-346) repeats the
wrong "-150 in older releases" record — fix both. REQ-BUILD-005 rev 4's
record was corrected at the decomposition (2026-10-12); this step makes
the code agree. Unit test: `tests/unit/test_transport_curl.c` (or
test_checkin.c, where the `verifies: REQ-NET-005` tag lives) — check at
start how the verify result reaches the classifier in a unit test; if no
seam exists, expose the classification as a hidden
`ehem__curl_classify_tls_failure(CURLcode, long verify_result, const char
*text)` and test it directly: `-150`/`9` → not expired; `-151`/`10`/the
texts → expired. `scripts/release/check-expired-classifier.sh` (positive
case, EXPIRED leaf) must still pass — run it locally against the
wolfSSL-backed release prefix if one is at hand, otherwise it runs in the
release job on the next push. Tag the new test `verifies: REQ-NET-005`.
Quarter-day step; no public API change.

**Definition of done**
- [x] `vr == -150` removed from the classifier; the comment states that
      `-150` is `ASN_BEFORE_DATE_E` (not yet valid) in 5.6.x/5.7.x.
- [x] Unit: verify result `-150` and `9` → original TLS error, no check-in
      leg, `tls_expired` false; `-151`, `10`, "certificate has expired",
      "ASN_AFTER_DATE" → classified expired (regression-pinned);
      `verifies: REQ-NET-005` tag present.
- [x] `check-expired-classifier.sh` still passes (locally with the release
      prefix, or in the release job — run id recorded).
- [x] `./dev ci` gcc + clang + ASan green; MinGW syntax check clean.
- [x] REQ-NET-005 rev 2 criterion (not-yet-valid) satisfied → status
      restored at the next §4.3 run.
