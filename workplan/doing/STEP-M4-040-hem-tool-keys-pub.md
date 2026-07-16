---
id: STEP-M4-040
title: "hem-tool keys pub — public material + typed metadata by KID"
milestone: M4
implements: ["REQ-TOOL-007"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M4-010"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `hem-tool keys pub <kid>` fetches via `ehem_key_get` and prints
type string, REQ-KEY-006 classification (family, modes), updated
timestamp, and the material (`pubkey`/`der`) as padded base64; `--hex`
switches encoding; `--raw` writes only the raw material bytes to stdout
(pipeline-friendly for the gate demo). Symmetric key → metadata + "no
public material", exit 0. Exit 0/1/2 per tool conventions.

**Notes:** Read-only thin consumer; formatting logic in `hem-tool-core`
(shared with unit tests, like keys list). Credentials via
`EHEM_URL`/`--url`, `EHEM_PASSPHRASE`/`--passphrase`. `--raw` must write
bytes exactly (fwrite, no trailing newline) and suppress all prose on
stdout (notices → stderr) so pipes stay clean.

**Definition of done**
- [ ] Unit (fake transport): asymmetric fixture prints type +
      classification + b64 material; `--hex` and `--raw` switch encoding
      (`--raw` = material bytes only); CERT/DER_PKEY prints der;
      symmetric prints "no public material" exit 0.
- [ ] Unit: malformed/missing kid or missing URL/passphrase → exit 2, no
      I/O; 406 → exit 1 naming the kid; request sequence is auth + get
      only (read-only assert).
- [ ] Live: `keys pub` of an EHEMTEST ED25519 key shows its 32-byte
      pubkey (manual or integration; feeds the M4 gate demo).
- [ ] MinGW clean (no POSIX-only I/O — see reference_mingw_printf /
      M2-070 portability gotchas); ASan/LSan clean.
