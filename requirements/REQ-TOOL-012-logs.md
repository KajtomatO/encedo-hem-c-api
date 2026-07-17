---
id: REQ-TOOL-012
title: hem-tool logs — audit log listing, download, and signing-key display
status: approved
priority: should
revision: 1
source: user decision 2026-07-17 (M7 decomposition tool set); REQ-SYS-009; approved 2026-07-17
depends_on: ["REQ-SYS-009"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool logs — audit log listing, download, and signing-key display

`hem-tool logs list`, `hem-tool logs get <ID> [--out FILE]`, and
`hem-tool logs key` SHALL drive the REQ-SYS-009 bindings through the
public API only.

- `logs list`: walks ALL pages (advancing by each page's returned
  count), prints one id per line plus a `total:` footer to stderr —
  stdout stays machine-consumable.
- `logs get <ID>`: writes the raw log text to stdout by default or to
  `--out FILE` (binary-safe, `_setmode` discipline on Windows like
  `sign`); non-existent id → device 404 → exit 1 with the SDK error on
  stderr.
- `logs key`: prints the base64 Ed25519 signing key, nonce, and
  nonce-signature, one labeled line each. Local signature verification
  stays OUT of the tool (the shim's Ed25519 verify is not public API;
  the tool is public-API-only per ARCHITECTURE §3) — the SDK-level
  integration test carries that proof.
- On an EPA device (list/get unrouted → NOT_FOUND) the tool reports
  "not available on this device (EPA build)" and exits 1; `logs key`
  works on both build variants.
- Exit codes: 0 success, 1 device/SDK error, 2 usage.

**Rationale:** living documentation for the logger group; gives the
device owner a one-command way to pull audit evidence off the device —
the CC-relevant workflow the tester scripts do in PHP.

**Acceptance criteria:**
- [ ] Unit (hem-tool-core, fake transport): list pagination walk
      (multi-page fixture); get to stdout and to `--out` (bytes
      verbatim); key prints three labeled base64 lines; error paths and
      exit codes.
- [ ] Live demo (PPA path): `logs list` shows ≥ 1 id; `logs get` of the
      newest id produces non-empty text; `logs key` prints the triple —
      or the EPA message path is demonstrated instead, matching the
      REQ-SYS-009 probe outcome.
- [ ] README tool table + `--help` updated.
