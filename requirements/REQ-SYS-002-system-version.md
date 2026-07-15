---
id: REQ-SYS-002
title: Binding for GET /api/system/version
status: approved
priority: must
revision: 1
source: encedo-hem-api-doc system/version.md (fetched 2026-07-15); ARCHITECTURE.md §11 M1
depends_on: ["REQ-API-001", "REQ-NET-001", "REQ-API-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#11-milestones"]
---

# Binding for GET /api/system/version

The SDK SHALL provide a binding for `GET /api/system/version` returning the
device's version information as a typed, caller-owned C struct, callable
without prior authentication.

**Rationale:** Together with status, this is the M1 "hello device" payload
`hem-tool status` prints. Per the API doc (system/version.md) auth is
optional (required only for microSD CSD/CID fields); documented fields
include `hwv`, `fwv`, `blv` (hardware/firmware/bootloader versions), key
and signature blobs (`fwk`, `fws`, `blk`, `bls`), and optional `uis`,
`sd_csd`, `sd_cid`.

**Acceptance criteria:**
- [ ] `ehem_system_version(ctx, &out)` populates a typed struct with at
      least `hwv`, `fwv`, `blv` and the documented optional fields marked
      present/absent; freed by its `ehem_*_free()` (unit test with canned
      JSON via fake transport).
- [ ] Unknown JSON fields are ignored; missing required fields yield
      `EHEM_ERR_PROTOCOL`.
- [ ] OPEN (M1 gate): response shape verified against the real dev-machine
      HEM; divergences recorded here.
