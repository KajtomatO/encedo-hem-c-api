---
id: STEP-M12-040
title: "hem-tool key types — one table in hem-tool-core, keys gen -h lists all 23 creatable types, new keys types [TYPE] explains them"
milestone: M12
implements: ["REQ-TOOL-027"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** hem-tool documents the device's key types from one table in
hem-tool-core: `keys gen -h` lists every type the firmware creates (the 23
of REQ-TEST-004, grouped by purpose) and points at the new local command
`keys types [TYPE]`, which prints one block per family or one entry; auth
class none, no traffic, works offline and before the SDK init; unknown
TYPE → exit 2 naming it and the valid names. A unit test classifies every
row with `ehem_key_type_parse` and keeps the table from drifting.

**Notes:** New `src/tools/hem-tool/keytypes.{h,c}` in hem-tool-core: 23
rows — device type string, `ehem_key_family` (one enum value per type —
`include/ehem/keymgmt.h:421`, so "distinct family per row" is exact),
purpose (sign / key agreement / encrypt-wrap / MAC / KEM / PQC signature),
public-key and signature sizes where the family has them (take them from
the SDK's REQ-KEY-006 constants at render time — no copies), the tool's
`--mode` default for the four NIST-P/K types (`ECDH,ExDSA`, REQ-TOOL-009)
and a one-line hint of the matching commands (`sign`, `keys pub`,
`random --kid`). `registry.c`: the `keys gen` entry's details (lines
161-167 today — "e.g. ED25519, SECP256R1, AES256 — the device validates
the type") render from the table; a new `keys types` entry (family keys,
auth `HEM_AUTH_NONE`, section main) with per-command help
(test_registry_complete walks it). `main.c`: dispatch `keys types` in the
pre-init short-circuit with `help`/`--version` (STEP-M12-020; whichever
lands second wires it). Top-level help: +1 line → 44 of 45 (STEP-M12-050
takes the last one; `test_top_page_shape` asserts the cap). `TYPE` on
`keys gen` stays verbatim on the wire — no allowlist (REQ-TOOL-009 rev 2
note). ASCII-only text; `%s` only. Unit `tests/unit/test_keytypes.c`
(`verifies: REQ-TOOL-027`): 23 rows, each → a known family ≠ UNKNOWN, all
distinct; every row's name in the rendered `keys gen` help and in `keys
types` output; `keys types ED25519` one entry; `keys types NOPE` exit 2,
zero I/O. Offline smoke: no `EHEM_URL`, no passphrase, and under a forced
REQ-API-010 mismatch once STEP-M12-010/020 are in (if this step lands
first, the forced-mismatch case is added by STEP-M12-020).

**Definition of done**
- [ ] `keytypes.{h,c}` table of exactly 23 rows; `keys gen -h` lists them
      grouped by purpose and points at `keys types`.
- [ ] `keys types` / `keys types TYPE` registered (auth none) with
      per-command help; unknown TYPE → exit 2 naming it + the valid names,
      zero I/O.
- [ ] Unit test: distinct known family per row via `ehem_key_type_parse`;
      every row in both renderings; single-entry and unknown cases;
      `verifies: REQ-TOOL-027` tag; test_registry_complete and
      test_top_page_shape (≤ 45) green.
- [ ] NIST `--mode` default and the ExDSA/ECDH facts come from the same
      constants REQ-TOOL-009 / REQ-KEY-006 use (reviewed, noted in the REQ).
- [ ] Offline smoke recorded: `hem-tool keys types` without URL/passphrase
      (and under a forced mismatch when the seam is available).
- [ ] `./dev ci` gcc + clang + ASan, ASCII gate, MinGW syntax check green;
      `implements: REQ-TOOL-027` tag.
