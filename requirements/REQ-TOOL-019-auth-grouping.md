---
id: REQ-TOOL-019
title: hem-tool auth-requirement transparency in the command listing
status: approved
priority: should
revision: 2
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11); rev 2 = user decision 2026-10-07 (M10: "manual recovery" help section; init-device / wipe-device / recovery added) — meaning change, status reset to draft per §3.3, §6.2 reported in chat 2026-10-07; encedo_firmware api_system.c:1060-1066 (config POST demands sub U/M); approved 2026-10-07 (M10 decomposition, user go-ahead)
depends_on: ["REQ-TOOL-018"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool auth-requirement transparency in the command listing

hem-tool's top-level command listing SHALL group commands by their
authentication requirement — **none**, **any bearer** (passphrase or
`--mobile`), and **passphrase-only** — followed by a separate trailing
section **"manual recovery"** that lists `cert-install` and
`tls-recover` (the building blocks `recovery` drives) in place of their
auth group (rev 2, user decision 2026-10-07).

- Membership (derived from device facts, finalized in code; rev 2
  additions marked M10):
  - **none:** `status`, `checkin`, `init-device` (M10 — the passphrase
    it takes is input data, not a credential);
  - **any bearer, mobile-capable:** `keys list/pub/gen/rm/update`,
    `sign`, `random`, `logs list/get/key`, `selftest`, `reboot`,
    `ext list`, `ext login` (mobile by definition);
  - **passphrase-only:** `ext pair` (the device demands `sub="U"` for
    the pairing trio, REQ-AUTH-006 — mobile bearers carry
    `sub=base64(kid)` and cannot manage pairings); M10: `wipe-device`
    and `recovery` (config writes demand `sub` U/M — see the open
    criterion);
  - **manual recovery (trailing section):** `cert-install`,
    `tls-recover` — each still carries its auth class in per-command
    help (REQ-TOOL-020).
- The grouping is **data**, not prose: each command's auth class and
  its section live in the shared command registry in hem-tool-core (the
  same single source of truth REQ-TOOL-020's per-command help renders),
  so the listing can never drift from what the commands actually
  enforce.

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
- [ ] Rev 2 (M10): the listing renders the "manual recovery" section
      after the three auth groups with `cert-install` and `tls-recover`
      in it and nowhere else; `recovery`, `wipe-device`, `init-device`
      appear in their auth groups; the section membership is registry
      data (unit: test_registry.c extended).
- [ ] OPEN — verify before finalizing rev 2's membership: firmware
      `api_system.c:1060-1066` makes every `POST /api/system/config`
      demand token `sub` U or M, which a mobile bearer
      (`sub=base64(kid)`) cannot satisfy. If confirmed (firmware read is
      the source; a live `--mobile cert-install` on an expired cert
      would prove it), `cert-install` and `tls-recover` are in fact
      passphrase-only — rev 1 listed them as mobile-capable — and the
      same check applies to `reboot`. Record the answer here and in
      REQ-TOOL-018.
