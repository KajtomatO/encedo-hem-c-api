---
id: REQ-SYS-006
title: Expose the cloud-delivered TLS certificate chain from the check-in flow
status: verified
priority: must
revision: 1
source: user decision 2026-07-16 (cert-install tooling); approved 2026-07-16; REQ-SYS-003 root-cause finding (fw v1.2.2 discards newcrt; manual install needs the chain); encedo-hem-api-doc system/checkin.md (newcrt claim = base64 DER)
depends_on: ["REQ-SYS-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings"]
---

# Expose the cloud-delivered TLS certificate chain from the check-in flow

The check-in binding (REQ-SYS-003) SHALL make the certificate the cloud
delivers available to callers: the leg-2 reply's `checked` value is a JWT
whose payload (base64url, not encrypted) may carry a `newcrt` claim — the
base64 DER certificate chain for the device's hostname. The SDK SHALL
parse it out (split segments, base64url-decode the payload, JSON-parse,
read `newcrt`) and expose it on the check-in result as an optional field
(`ehem_checkin_info.newcrt_chain`, NULL when the cloud sent none), leaving
the verbatim leg-3 relay untouched.

**Rationale:** firmware v1.2.2 acknowledges `newcrt` but never installs it
(REQ-SYS-003 root cause), so the only working rotation is harvesting the
chain client-side and installing it via REQ-SYS-004. The chain arrives
over the mandatorily TLS-verified cloud leg (REQ-SYS-003 security
constraint), which is what makes client-side harvesting trustworthy.

**Naming note:** the existing `ehem_checkin_info.newcrt` field carries the
device's leg-3 status string ("OK"/"ERROR") per the wire format; the new
field is distinct and documented against confusion.

**Implementation note (2026-07-16):** in addition to `newcrt_chain`, the SDK
also harvests the device's CURRENT certificate serial from the leg-1 `check`
JWT's `csn` claim (base64 of the loaded cert's serial per system/checkin.md),
normalized to uppercase hex on `ehem_checkin_info.current_serial`. This is the
companion skip-if-current input for REQ-TOOL-003 (compare against the harvested
leaf's serial), harvested via the same base64url+JSON technique and freed by
`ehem_checkin_result_free`. Both leg-1 and leg-2 payloads are read best-effort;
only an allocation failure fails the check-in.

**Acceptance criteria:**
- [x] Happy path: scripted leg-2 reply with a `newcrt` claim → result
      carries the exact base64 chain; leg-3 body remains the verbatim leg-2
      response (fake-transport unit test — test_checkin.c
      `test_checkin_harvests_chain_and_serial`, also asserting `current_serial`).
- [x] Absent claim / unparseable payload → `newcrt_chain` NULL, check-in
      still succeeds (tolerant; test_checkin.c `test_checkin_harvest_absent`,
      `test_checkin_harvest_unparseable_payload`).
- [x] `ehem_checkin_result_free` releases the new fields; NULL-safe;
      ASan/LSan clean (`./dev test asan` green; test_checkin free-null case).
- [x] RESOLVED (live, 2026-07-16, test_checkin_live under `-L integration`):
      the SDK harvests `current_serial=C173D2A9148ECD6225A3DFE5299B82CE` from
      the real device (matches the 2026-07-16 remediation serial C173D2A9…,
      valid to 2026-10-06). **Finding:** the broker only DELIVERS a `newcrt`
      chain when it deems the device stale — with the device already current it
      returned `newcrt_chain` absent, exercising the tolerant path. When a chain
      IS delivered its leaf parses as CN=my.ence.do (proven offline against a
      frozen fixture in test_cert.c; the live delivery is broker-gated on the
      device being stale, e.g. after the next expiry). The public helper
      `ehem_cert_inspect()` reads the leaf serial + validity used by both the
      comparison and the cert-install summary.
