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
  tests: []
  notes: null
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
- [ ] Unit (fake transport + scripted stdin): `--all` deletes exactly the
      non-protected fixture keys, no prompt for protected; partial
      protected prefix → warn + skip; exact protected label → prompt,
      only literal `YES` deletes; `--yes` skips only the bulk prompt.
- [ ] Unit: `--dry-run` issues no DELETE, exit 0; no selection → exit 2;
      one failing delete among several → others still processed, exit 1.
- [ ] Live: create EHEMTEST keys, `keys rm --label-prefix EHEMTEST --yes`
      removes them; verified gone via list (integration/manual, gated).
