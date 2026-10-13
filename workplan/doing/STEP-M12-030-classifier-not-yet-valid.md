---
id: STEP-M12-030
title: "Expired-cert classifier: drop wolfSSL -150 (ASN_BEFORE_DATE_E = not yet valid), pin the not-yet-valid case with a unit test (REQ-NET-005 rev 2)"
milestone: M12
implements: ["REQ-NET-005"]
traces:
  architecture: ["ARCHITECTURE.md#7-transport", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] `vr == -150` removed from the classifier; the comment states that
      `-150` is `ASN_BEFORE_DATE_E` (not yet valid) in 5.6.x/5.7.x.
- [ ] Unit: verify result `-150` and `9` → original TLS error, no check-in
      leg, `tls_expired` false; `-151`, `10`, "certificate has expired",
      "ASN_AFTER_DATE" → classified expired (regression-pinned);
      `verifies: REQ-NET-005` tag present.
- [ ] `check-expired-classifier.sh` still passes (locally with the release
      prefix, or in the release job — run id recorded).
- [ ] `./dev ci` gcc + clang + ASan green; MinGW syntax check clean.
- [ ] REQ-NET-005 rev 2 criterion (not-yet-valid) satisfied → status
      restored at the next §4.3 run.
