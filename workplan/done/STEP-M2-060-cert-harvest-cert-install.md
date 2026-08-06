---
id: STEP-M2-060
title: "Check-in cert harvest (newcrt_chain) + hem-tool cert-install"
milestone: M2
implements: ["REQ-SYS-006", "REQ-TOOL-003"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M2-050"]
evidence:
  commits:
    - "08e4915 — check-in cert harvest (newcrt_chain/csn) + hem-tool cert-install"
  tests:
    - "tests/unit/test_checkin.c — harvest: chain+serial, absent, unparseable payload"
    - "tests/unit/test_cert.c — ehem_cert_inspect: leaf fields, bad base64, non-cert, args"
    - "tests/unit/test_cert_install.c — full flow, skip, already-current-no-chain, --force, 8 failure modes"
    - "tests/integration/test_checkin_live.c — live harvest (REQ-SYS-006), current_serial=C173D2A9148ECD6225A3DFE5299B82CE"
    - "tests/disruptive/test_cert_install_live.c — disruptive-gated live cert-install (deferred)"
  notes: >
    REQ-SYS-006: ehem_checkin_info gained newcrt_chain (leg-2 checked JWT
    `newcrt`) + current_serial (leg-1 check JWT `csn`, normalized hex);
    harvested in ehem_checkin_run best-effort (only NOMEM fails check-in).
    Public ehem_cert_inspect()/ehem_cert_info_free() read a base64 DER chain's
    leaf (serial + CN + validity) via crypto_shim ehem_cert_parse_leaf
    (wolfCrypt wc_ParseCert; ehem_serial_hex drops a leading 0x00 sign byte so
    both serial producers agree). REQ-TOOL-003: hem-tool cert-install in
    hem-tool-core static lib (shared by CLI + tests); skip-if-current honors
    both the broker suppressing the chain when csn is current AND an explicit
    serial match; distinct exit code per failure mode. disruptive CTest label +
    EHEM_ALLOW_DISRUPTIVE=1 gate + tests/disruptive/ wired. Live: hem-tool
    cert-install exited 0 "already current" (no reboot); full install path
    deferred to next expiry (2026-07-16 python remediation = reference).
    Unit 17/17 gcc+clang + asan green; live integration 4/4; export/header
    gates green.
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
- [x] Harvest unit tests: present/absent/unparseable `newcrt` claim;
      leg-3 relay stays verbatim; `_free` NULL-safe; ASan/LSan clean.
      (test_checkin.c; also test_cert.c for the leaf parse.)
- [x] cert-install unit tests (fake transport): full sequence, skip path,
      `--force`, every failure mode exits nonzero with distinct message.
      (test_cert_install.c — 13 cases; 8 distinct nonzero exit codes.)
- [x] `disruptive` label + `EHEM_ALLOW_DISRUPTIVE` gate wired; the live
      cert-install integration test is excluded from `-L integration`.
      (tests/disruptive/test_cert_install_live.c; verified skip-77 without the
      gate and label-excluded from `-L integration`.)
- [x] Live disruptive run explicitly DEFERRED to the next cert expiry (the
      device is current, so the full install+reboot path would pointlessly
      reboot it); the 2026-07-16 python-script remediation is the cited
      reference behavior. A NON-disruptive live run was recorded instead:
      `hem-tool cert-install` against my.ence.do exited 0 "already current"
      (harvest + skip-if-current proven end to end, no reboot).
