---
id: REQ-TOOL-007
title: hem-tool keys pub — print a key's public material and typed metadata
status: approved
priority: should
revision: 1
source: user decision 2026-07-16 (M4 decomposition: add hem-tool sign and keys pub); ARCHITECTURE.md §8 (thin consumer, subcommands grow with milestones); HEM-SDK-5 (public-key read, living documentation); approved 2026-07-16
depends_on: ["REQ-TOOL-001", "REQ-KEY-003", "REQ-KEY-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool keys pub — print a key's public material and typed metadata

hem-tool SHALL provide a `keys pub <kid>` subcommand that fetches the key
via `ehem_key_get` (REQ-KEY-003) and prints a human-readable summary —
the device type string, the REQ-KEY-006 classification (family, ExDSA/ECDH
modes), the update timestamp, and the public material (`pubkey` or `der`)
as padded base64 — with output options: `--hex` renders the material as
lowercase hex instead, `--raw` writes the raw material bytes alone to
stdout (pipeline-friendly; all prose suppressed). A symmetric key (no
material on the wire) prints its metadata with an explicit "no public
material" note and still exits 0 — the get succeeded.

The subcommand is strictly read-only. Connection and credentials follow
the existing tool conventions (`EHEM_URL`/`--url`,
`EHEM_PASSPHRASE`/`--passphrase`); exit 0 on success, 2 on
usage/environment errors (missing/malformed kid, missing URL or
passphrase, conflicting format flags), 1 on runtime failure (auth, scope,
key not found, device/network) with an actionable message. The formatting
logic lives in `hem-tool-core` (shared with the unit tests, like the
other subcommands).

**Rationale:** user decision at M4 decomposition (2026-07-16). Makes the
M3/M4 public-key-read path (HEM-SDK-5) drivable by hand — the operator's
way to grab a pubkey for local verification, and living documentation for
`ehem_key_get` + the REQ-KEY-006 classifier. `--raw` exists so the M4
gate demo can pipe device material straight into local tooling.

**Acceptance criteria:**
- [ ] Against the fake transport: an asymmetric fixture key prints type,
      classified family/modes, updated timestamp, and base64 material;
      `--hex` switches the encoding; `--raw` emits exactly the material
      bytes and nothing else (unit tests on the hem-tool-core function).
- [ ] A CERT/DER_PKEY fixture prints its `der` material; a symmetric
      fixture prints metadata + "no public material", exit 0 (unit tests).
- [ ] Malformed/missing kid or missing URL/passphrase → exit 2 with usage,
      no I/O; key not found (device 406) → exit 1 with a message naming
      the kid; read-only — no request besides auth + get (unit tests
      asserting the request sequence).
- [ ] Live (M4 gate): `keys pub` of an `EHEMTEST` ED25519 key prints its
      32-byte pubkey; `--raw` output feeds the local wolfCrypt verify in
      the gate demo (manual/integration).
