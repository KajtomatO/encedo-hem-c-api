---
id: STEP-M3-060
title: "hem-tool keys rm — selection, prompts, protected-key guard, exit codes"
milestone: M3
implements: ["REQ-TOOL-006"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M3-050", "STEP-M3-020"]
evidence:
  commits: []
  tests:
    - "tests/unit/test_keys.c — keys rm (11 cases): --all deletes exactly the non-protected keys (TLS pair not touched); --dry-run no DELETE exit 0; no-selection / --all+--label-prefix / no-passphrase → exit 2 (no I/O); partial protected prefix → warn+skip (regular sibling still deleted); exact protected label → per-key prompt, only literal YES deletes ('yes' declines); --yes skips bulk but protected still prompts (declined); one failed delete → rest processed, exit 1; bulk decline w/o protected → exit 1. Scripted stdin + captured out via tmpfile()."
    - "tests/integration/test_keys_rm_live.c — live: create 2 EHEMTEST ED25519 keys → hem_keys_rm_run(--label-prefix EHEMTEST --yes) → both deleted → verified gone via list_all (REQ-TOOL-006 / REQ-TEST-003)"
  notes: >
    keys rm lives in hem-tool-core (keys.c) with wipe_keys.py semantics.
    hem_keys_rm_run partitions the repo into regular / protected-exact-target /
    protected-partial-skipped (protected keys via REQ-TOOL-005 hem_key_is_protected;
    a protected key is a target ONLY when a --label-prefix equals its label
    exactly, else warned+skipped; --all silently excludes protected). Prints the
    partition, then (unless --dry-run) deletes regular keys behind ONE bulk prompt
    (--yes skips it) and each protected target behind a literal-"YES" per-key prompt
    (--yes never applies). Prompt input from a caller FILE* (stdin in CLI, tmpfile
    in tests); a failed delete is reported and processing continues. Exit 0
    (success/dry-run/nothing), 1 (user abort or ≥1 delete failed), 2 (usage/env).
    Delete via REQ-KEY-004. main.c: `keys rm` + --all/--label-prefix(repeatable,
    cap 32)/--dry-run/--yes; fprint_key shared with keys list.
    LIVE (my.ence.do, 2026-07-16):
    - test_keys_rm_live: created EHEMTEST-*-0/-1, `keys rm --label-prefix EHEMTEST
      --yes` → "2 deleted, 0 failed" → both gone from list.
    - `./dev tool keys rm --all --dry-run` → "6 key(s) total — ALL keys (excluding
      protected device keys); regular targets: 3" (Encedo OIDC, it-ecdh-1/2); the
      TLS pair + SM-S938B (Android) EXCLUDED; "dry-run: no keys deleted", exit 0.
    Unit 17/17 gcc+clang + ASan/LSan clean; MinGW cross-compile of keys.c+main.c
    clean; export/header gates green; full integration 9/9.
reopened: []
cancelled: null
---

**Goal:** `hem-tool keys rm` in hem-tool-core with wipe_keys.py
semantics (REQ-TOOL-006): selection `--all` (non-protected only,
protected silently excluded) XOR repeatable `--label-prefix`; partition
report (regular / protected / partial-match-skipped) before acting;
`--dry-run` stops after the report; bulk prompt for regular targets
skippable with `--yes`; protected keys only via exact-label prefix +
per-key literal uppercase `YES` prompt, `--yes` ignored for them; exit
codes 0 / 1 (abort or ≥1 failure) / 2 (usage/env).

**Notes:** Prompt input from a caller-supplied FILE* (stdin in the CLI)
so unit tests script it with portable tmpfile(); only the literal `YES`
proceeds at a protected prompt — `y`, `yes`, empty, EOF all skip. A
failed delete is reported and processing continues (exit 1 at the end).
Partition logic shares the M3-050 classifier. Deletion uses REQ-KEY-004;
remember delete is irreversible — the live test only ever touches
EHEMTEST keys (REQ-TEST-003); the protected-guard live demo runs as
`--all --dry-run` at the gate so no protected key is ever at risk.

**Definition of done**
- [x] Unit (fake transport + scripted stdin): `--all` deletes exactly the
      non-protected fixture keys, no prompt for protected; partial
      protected prefix → warn + skip; exact protected label → prompt,
      only literal `YES` deletes; `--yes` skips only the bulk prompt.
      (test_rm_* — 11 cases.)
- [x] Unit: `--dry-run` issues no DELETE, exit 0; no selection → exit 2;
      one failing delete among several → others still processed, exit 1.
      (test_rm_dry_run / _no_selection / _delete_failure_continues.)
- [x] Live: create EHEMTEST keys, `keys rm --label-prefix EHEMTEST --yes`
      removes them; verified gone via list (integration/manual, gated).
      (test_keys_rm_live green; `--all --dry-run` protected-exclusion demo
      recorded in Notes.)
