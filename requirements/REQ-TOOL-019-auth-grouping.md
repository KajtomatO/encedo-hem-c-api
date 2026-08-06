---
id: REQ-TOOL-019
title: hem-tool auth-requirement transparency in the command listing
status: verified
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
- [x] Top-level help renders the three groups with meaningful headers
      (none / bearer: "passphrase or --mobile" / passphrase-only:
      "the device demands sub=\"U\""), in that order
      (test_registry.c test_auth_classes, 2026-08-06).
- [x] Unit: every registered command carries an auth class and full
      descriptions (test_registry_complete walks the table — a new
      command with missing fields fails); the rendered listing places
      status under none and ext pair under passphrase-only
      (test_registry.c, 2026-08-06).
- [x] The auth class shown matches enforced behavior, one command per
      group: status/checkin never log in (no login call), keys list
      accepts either credential (test_tool_auth.c mobile run), ext pair
      rejects --mobile (test_tool_auth.c) — spot-checks in
      test_registry.c test_auth_classes (2026-08-06).
