---
id: REQ-TOOL-019
title: hem-tool auth-requirement transparency in the command listing
status: approved
priority: should
revision: 1
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11)
depends_on: ["REQ-TOOL-018"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool auth-requirement transparency in the command listing

hem-tool's top-level command listing SHALL group commands by their
authentication requirement — **none**, **any bearer** (passphrase or
`--mobile`), and **passphrase-only**.

- Current membership (derived from device facts, finalized in code):
  - **none:** `status`, `checkin`;
  - **any bearer, mobile-capable:** `keys list/pub/gen/rm/update`,
    `sign`, `random`, `logs list/get/key`, `selftest`, `cert-install`,
    `reboot`, `tls-recover`, `ext list`, `ext login` (mobile by
    definition);
  - **passphrase-only:** `ext pair` (the device demands `sub="U"` for
    the pairing trio, REQ-AUTH-006 — mobile bearers carry
    `sub=base64(kid)` and cannot manage pairings).
- The grouping is **data**, not prose: each command's auth class lives
  in the shared command registry in hem-tool-core (the same single
  source of truth REQ-TOOL-020's per-command help renders), so the
  listing can never drift from what the commands actually enforce.

**Rationale:** the user asked (2026-08-06) for the tool to be honest
about which commands need which access — today a user discovers
credential requirements only by hitting an auth error. Grouping the
listing makes the tool's security model legible at a glance and
documents the one real device constraint (pairing is passphrase-only).

**Acceptance criteria:**
- [ ] Top-level help renders the three groups with one-line headers
      that say what the group means (e.g. "needs a bearer — passphrase
      or --mobile").
- [ ] Unit: every registered command carries an auth class; the
      rendered listing places each command under its class; adding a
      command without a class fails the test (registry completeness).
- [ ] The auth class shown for a command matches what its
      implementation enforces (spot-checked in the unit test via the
      shared login helper's mode for at least one command per group).
