---
id: REQ-TOOL-011
title: hem-tool keys update — rename LABEL / set DESCR with protected-key guard
status: approved
priority: should
revision: 1
source: user decision 2026-07-17 (M7 decomposition tool set); REQ-KEY-007; REQ-TOOL-005 (protected-key policy); approved 2026-07-17
depends_on: ["REQ-KEY-007", "REQ-TOOL-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool keys update — rename LABEL / set DESCR with protected-key guard

`hem-tool keys update <KID> --label <LABEL> [--descr <TEXT>]` SHALL
update a key's metadata via the public `ehem_key_update`, refusing to
touch protected keys without the per-key confirmation ritual.

- `--label` required (mirrors the firmware/REQ-KEY-007 rule); `--descr`
  optional UTF-8 text (the tool encodes; binary DESCR stays an SDK-level
  capability).
- **Protected-key guard (REQ-TOOL-005 classifier):** if the CURRENT
  label classifies the key as protected (`TLS PrivateKey`,
  `TLS Certificate`, `(Android)`/`(iPhone)` substrings), the update — 
  which could strip the very label that protects it — requires the
  interactive literal `YES` per key, and `--yes` is ignored, exactly as
  `keys rm`. The tool must therefore `get`/`list` the key first to
  classify it.
- Renaming an unprotected key TO a protected-looking label is allowed
  but warned on stderr (it will make the key protected for later bulk
  operations).
- Exit codes follow the keys-rm convention: 0 success, 1 device/SDK
  error, 2 usage error; refusal at the prompt exits 0 with "skipped".
- Credentials via the standard flags/env (`EHEM_URL`,
  `EHEM_PASSPHRASE`).

**Rationale:** living documentation for REQ-KEY-007 and the first
metadata-mutating tool path; without the guard, a one-line rename could
silently disarm the protected-set convention that keys rm relies on.

**Acceptance criteria:**
- [ ] Unit (hem-tool-core, fake transport): update flows for label-only
      and label+descr; protected current-label → prompt required, `YES`
      proceeds, anything else skips, `--yes` ignored; warn-on-rename-to-
      protected emitted; exit codes 0/1/2 covered.
- [ ] Live demo: EHEMTEST key created → `keys update` renames it (list
      shows new label) → descr set → `keys rm` cleans up.
- [ ] `--help` documents the guard; README tool table updated.
