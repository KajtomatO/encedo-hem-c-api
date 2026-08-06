---
id: REQ-SYS-002
title: Binding for GET /api/system/version
status: verified
priority: must
revision: 2
source: encedo-hem-api-doc system/version.md (fetched 2026-07-15); ARCHITECTURE.md §11 M1; rev 2 = STEP-M9-015 blv relaxation (user decision 2026-08-06; fw api_system.c `if (bldr != NULL)` — the blv/blk/bls triple is conditional)
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
- [ ] `ehem_system_version(ctx, &out)` populates a typed struct with
      required `hwv`, `fwv`; the bootloader triple `blv`/`blk`/`bls` is
      CONDITIONAL (rev 2 — the firmware emits it only when the bootloader
      footer's publisher matches, `if (bldr != NULL)` in api_system.c, so
      a blv-less response is legal and parses with the triple NULL); the
      documented optional fields are NULL when absent; freed by its
      `ehem_*_free()` (unit tests with canned JSON via fake transport,
      incl. a blv-less fixture).
- [ ] Unknown JSON fields are ignored; missing required fields yield
      `EHEM_ERR_PROTOCOL`.
- [x] RESOLVED (M1 gate, 2026-07-15): verified against the real dev-machine
      HEM. Live `GET /api/system/version` returned
      `{hwv, fwv, fwk, fws, blv, blk, bls, uis}`. Required `hwv`/`fwv`/`blv`
      present; optional `fwk`/`fws`/`blk`/`bls`/`uis` present and parsed;
      `sd_csd`/`sd_cid` absent (require a token). `fwv` = "Encedo nGINE FW
      v1.2.2-DIAG" (diagnostic firmware). Matches the SDK struct; no change
      needed.
