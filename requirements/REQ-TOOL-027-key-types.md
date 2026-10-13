---
id: REQ-TOOL-027
title: hem-tool key-type documentation — keys gen -h lists every creatable type, keys types explains each, from one table checked against the classifier
status: approved
priority: should
revision: 1
source: user request 2026-10-10 ("key types in hem-tool": keys gen -h lists all types + a separate command explaining them), moved into M12 the same day; ARCHITECTURE.md §11 M12 part E (command name settled at decomposition: `keys types`); REQ-TEST-004 (the 23 fw v1.2.2 create types), REQ-KEY-006 (classifier + size table), REQ-TOOL-009 (keys gen; its NIST default); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-TOOL-009", "REQ-KEY-006", "REQ-TOOL-020"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool key-type documentation

hem-tool SHALL document the device's key types from **one table in
hem-tool-core**: `keys gen -h` lists every type the firmware creates (the
23 of REQ-TEST-004: `SECP256R1/384R1/521R1/256K1`, `ED25519/448`,
`CURVE25519/448`, `AES128/192/256`, `SHA2-256/384/512`, `SHA3-256/384/512`,
`MLKEM512/768/1024`, `MLDSA44/65/87`) and a new local command
`keys types [TYPE]` explains each one.

- **Per type** the table holds: the device type string; the family; what
  the key is for — *sign* (ExDSA: NIST-P/K, Ed25519, Ed448), *key
  agreement* (ECDH: NIST-P/K, X25519, X448), *encrypt / wrap* (AES),
  *MAC* (HMAC-SHA2/SHA3), *KEM* (ML-KEM), *PQC signature* (ML-DSA);
  the public-key and signature sizes from REQ-KEY-006 where the family
  has them; and the tool's default for `--mode` (`ECDH,ExDSA` for the
  four NIST-P/K types — the device's own default is ECDH-only and cannot
  sign, REQ-TOOL-009) with a one-line hint of the matching hem-tool
  commands (`sign`, `keys pub`, `random --kid`).
- `keys types` prints the whole table (one block per family); `keys
  types TYPE` one entry; auth class **none** — no traffic, works
  offline and before the SDK init (REQ-TOOL-024); unknown `TYPE` → exit
  2 naming it and listing the valid names.
- `keys gen -h` replaces today's "e.g. ED25519, SECP256R1, AES256 — the
  device validates the type" with the full list grouped by purpose, and
  points at `keys types`. `TYPE` stays verbatim on the wire — no
  tool-side allowlist (REQ-TOOL-009 unchanged); the list is
  documentation.
- The table is checked, not trusted: a unit test classifies every row
  with `ehem_key_type_parse` (REQ-KEY-006) and asserts a known family,
  that the 23 rows cover each creatable family exactly once, and that
  every row's name appears in the rendered `keys gen` help.
- Top-level help: one new line (`keys types`) under the no-credentials
  group; the page stays ≤ 45 lines (REQ-TOOL-020).

**Rationale:** the user's request: today an operator has to know the
firmware vocabulary to use `keys gen`, and nothing in the tool says
which types can sign, agree or wrap. The classifier already knows the
families; the tool makes that knowledge readable, and the unit test
keeps the prose from drifting from the code.

**Acceptance criteria:**
- [ ] Unit: the table has exactly 23 rows; each classifies to a distinct
      family ≠ UNKNOWN; the rendered `keys gen` help contains every row's
      type string; `keys types` output mentions every row; `keys types
      ED25519` prints one entry; `keys types NOPE` exits 2 with zero I/O.
- [ ] Unit: `keys types` is registered with auth class none and renders
      per-command help (test_registry_complete); top-level help ≤ 45
      lines.
- [ ] The NIST `--mode` default and the ExDSA/ECDH facts in the table
      agree with REQ-TOOL-009 / REQ-KEY-006 (the same constants, not
      copies — reviewed at the step).
- [ ] Offline smoke: `hem-tool keys types` runs without `EHEM_URL`,
      without a passphrase and under a forced REQ-API-010 mismatch.
