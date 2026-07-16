---
id: STEP-M2-060
title: "Check-in cert harvest (newcrt_chain) + hem-tool cert-install"
milestone: M2
implements: ["REQ-SYS-006", "REQ-TOOL-003"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M2-050"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** (1) `ehem_checkin_run` additionally parses the leg-2 `checked`
JWT payload (base64url decode + cJSON) and exposes an optional
`newcrt_chain` (base64 DER chain string) on `ehem_checkin_info`; freed by
`ehem_checkin_result_free`. (2) `hem-tool cert-install`: check-in →
harvest → skip-if-current (leg-1 `csn` serial vs harvested leaf serial;
`--force` overrides) → login (EHEM_PASSPHRASE / --passphrase) → install →
reboot → bounded poll (~2 min) → verify under the ctx TLS mode → summary
with old→new validity dates; distinct nonzero exits per failure mode
(REQ-TOOL-003).

**Notes:** This automates the manual remediation proven 2026-07-16
(fw v1.2.2 cannot apply check-in certs — REQ-SYS-003 root cause).
Skip-if-current serial extraction from the DER leaf: minimal TBS walk or
wolfCrypt `wc_ParseCert` (shim dependency already present from M2-010) —
decide at implementation, record in REQ-TOOL-003's open criterion.
Leg-1 `csn` claim requires peeking into the leg-1 `check` JWT payload
(same base64url+JSON technique as the harvest). First test under the
`disruptive` CTest label: add the label + `EHEM_ALLOW_DISRUPTIVE=1` env
gate (ARCHITECTURE §9) and `tests/disruptive/` wiring with this step.

**Definition of done**
- [ ] Harvest unit tests: present/absent/unparseable `newcrt` claim;
      leg-3 relay stays verbatim; `_free` NULL-safe; ASan/LSan clean.
- [ ] cert-install unit tests (fake transport): full sequence, skip path,
      `--force`, every failure mode exits nonzero with distinct message.
- [ ] `disruptive` label + `EHEM_ALLOW_DISRUPTIVE` gate wired; the live
      cert-install integration test is excluded from `-L integration`.
- [ ] Live disruptive run recorded OR explicitly deferred to next cert
      expiry with the 2026-07-16 python-script run cited as reference
      behavior (REQ-TOOL-003 criterion).
