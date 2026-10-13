---
id: STEP-M12-040
title: "hem-tool key types — one table in hem-tool-core, keys gen -h lists all 23 creatable types, new keys types [TYPE] explains them"
milestone: M12
implements: ["REQ-TOOL-027"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: ["2b2327a", "88542b5"]   # start; implementation (the done commit follows)
  tests:
    - "tests/unit/test_keytypes.c (verifies: REQ-TOOL-027) — 23 rows classify through ehem_key_type_parse to their declared distinct families (family_str round-trips the name); purpose groups vs the classifier's sig_max_len / pubkey_len; hem_keys_gen_default_mode == HEM_KEYS_GEN_NIST_MODE for exactly the NIST rows; every name as a whole token in `keys gen -h` and in `keys types`; ED25519 one entry; NOPE exit 2 with the names on stderr and nothing on stdout; registry entry auth none"
    - "tool_startup_cli (check_tool_startup.cmake, verifies: REQ-TOOL-024/025/027) — on the real main.c with the environment cleared: `keys types` / `keys types ED25519` exit 0 under the forced version / probe refusal, no URL notice; NOPE exit 2; an extra argument exit 2"
    - "tests/unit/test_registry.c (unchanged, green) — completeness walk covers the new entry; top page 44 lines (≤ 45); test_keys_gen.c (unchanged, green) — the NIST default body and note still byte-identical"
    - "./dev ci 52/52 gcc + clang; ./dev test asan 52/52; MinGW -fsyntax-only clean for keytypes.c, keys.c, registry.c, main.c, test_keytypes.c; keytypes.c also compiled -O3 -Wall -Wextra -Werror (format-truncation guard)"
  notes: |
    Table = src/tools/hem-tool/keytypes.{h,c} in hem-tool-core: {type
    string, family, purpose group} x 23 — nothing the SDK knows is copied:
    sizes and signability come from ehem_key_type_parse at render time,
    the NIST --mode default from keys.h (new HEM_KEYS_GEN_NIST_MODE +
    hem_keys_gen_default_mode(), which keys gen itself now calls instead of
    its private family_is_nist_ecc + literal; the registry's keys gen
    texts concatenate the macro). Seven purpose groups (NIST-P/K sign +
    agreement, Edwards sign, Montgomery agreement, AES encrypt/wrap, HMAC
    MAC, ML-KEM KEM, ML-DSA PQC signature), each with the matching hem-tool
    commands / SDK calls as the "use:" hint (hem-tool has no ecdh / hmac /
    cipher / KEM commands, so those name the SDK functions).
    `keys gen -h`: the registry gained an optional per-command hook
    (`hem_command.more`, appended field; every other entry NULL) that
    renders the grouped list after the details; the details text now says
    "listed below" instead of "e.g. ED25519, SECP256R1, AES256". The
    keys gen "needs a key type" error also points at `keys types`.
    `keys types [TYPE]`: exact, case-sensitive match (types go to the
    device verbatim); unknown → exit 2 (HEM_KEYTYPES_UNKNOWN) naming it and
    printing the grouped list. main.c dispatches it in the pre-init block
    (after help / --version, before the auth-class check and
    hem_tool_global_init), so it needs no URL, credentials or working
    crypto library — the REQ-TOOL-024 `keys types` criterion is closed
    (rev 4). Top-level help: 44 lines (init-wizard takes the 45th).
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
- [x] `keytypes.{h,c}` table of exactly 23 rows; `keys gen -h` lists them
      grouped by purpose and points at `keys types`.
- [x] `keys types` / `keys types TYPE` registered (auth none) with
      per-command help; unknown TYPE → exit 2 naming it + the valid names,
      zero I/O.
- [x] Unit test: distinct known family per row via `ehem_key_type_parse`;
      every row in both renderings; single-entry and unknown cases;
      `verifies: REQ-TOOL-027` tag; test_registry_complete and
      test_top_page_shape (≤ 45) green.
- [x] NIST `--mode` default and the ExDSA/ECDH facts come from the same
      constants REQ-TOOL-009 / REQ-KEY-006 use (reviewed, noted in the REQ).
- [x] Offline smoke recorded: `hem-tool keys types` without URL/passphrase
      (and under a forced mismatch when the seam is available).
- [x] `./dev ci` gcc + clang + ASan, ASCII gate, MinGW syntax check green;
      `implements: REQ-TOOL-027` tag.
