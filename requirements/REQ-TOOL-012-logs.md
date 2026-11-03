---
id: REQ-TOOL-012
title: hem-tool logs — audit log listing, download, and signing-key display
status: superseded
priority: should
revision: 2   # 2026-10-28 (M13 decomposition, §6.2 approved with the hem-tool split plan 2026-10-27): superseded — hem-tool moved to encedo-hem-tool at 1.2.0; the same id there is authoritative; text frozen at rev 1.
source: user decision 2026-07-17 (M7 decomposition tool set); REQ-SYS-009; approved 2026-07-17
depends_on: ["REQ-SYS-009"]
supersedes: null
superseded_by: "KajtomatO/encedo-hem-tool requirements/REQ-TOOL-012"
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool logs — audit log listing, download, and signing-key display

> **Superseded 2026-10-28 (M13 decomposition).** hem-tool moved to
> [encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool) at the SDK's 1.2.0 (user decision
> 2026-10-27); this requirement continues there under the same id and is
> maintained there. The text below is frozen at the revision it had when it
> left (rev 1); the code and tests that implemented it leave this repository
> in STEP-M13-010.

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
- [x] Unit (hem-tool-core, fake transport, 2026-07-18): list pagination
      walk (two-page fixture, offsets 0 → 2); get bytes verbatim; key
      prints three labeled base64 lines; id guards (non-hex, "list",
      NULL) → usage with no wire traffic (tests/unit/test_tool_m7.c).
- [x] Live demo (PPA device, 2026-07-18): `logs list` printed 62 ids;
      `logs get` of the first returned the pipe-delimited log file
      ("# Encedo nGINE FW v1.2.2-DIAG" header); `logs key` printed the
      real Ed25519 triple.
- [x] `--help` updated (the usage text is the CLI reference; the README
      carries no per-subcommand table by design).
