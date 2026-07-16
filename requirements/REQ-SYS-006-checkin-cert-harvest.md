---
id: REQ-SYS-006
title: Expose the cloud-delivered TLS certificate chain from the check-in flow
status: approved
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

**Acceptance criteria:**
- [ ] Happy path: scripted leg-2 reply with a `newcrt` claim → result
      carries the exact base64 chain; leg-3 body remains the verbatim leg-2
      response (fake-transport unit test).
- [ ] Absent claim / unparseable payload → `newcrt_chain` NULL, check-in
      still succeeds (tolerant; unit tests).
- [ ] `ehem_checkin_result_free` releases the new field; NULL-safe;
      ASan/LSan clean.
- [ ] OPEN (M2 gate): live check-in against the dev device yields a chain
      whose leaf parses as CN=my.ence.do (compare with the 2026-07-16
      harvest: serial C173D2A9…, valid to 2026-10-06).
