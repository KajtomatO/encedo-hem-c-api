---
id: STEP-M9-010
title: "Doc-repo conformance sweep + DISCREPANCIES reconciliation"
milestone: M9
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#6-protocol-bindings"]
depends_on: []
evidence:
  commits: ["c0e5eeb", "11289ae"]
  tests: []
  notes: >
    Analysis/documentation chore — no new behavior, so no verifies: tags.
    Deliverable is docs/COVERAGE.md (every doc-repo page/endpoint
    dispositioned; field-level diffs per group) + 3 discrepancy-registry
    gaps recorded (REQ-KEY-001 rev 2, REQ-SYS-004 rev 2, REQ-AUTH-001
    rev 2, src/ejwt.h + include/ehem/logger.h comments) + 2 stale-comment
    fixes in keymgmt.h. ./dev ci green after the edits (38/38 GCC+Clang,
    export/header gates). NO must-bind gaps for 1.0. Sweep found TWO SDK
    defects, presented to the user for a fix decision (proposed
    STEP-M9-015): version parser hard-requires firmware-conditional
    'blv' (fw api_system.c: if (bldr != NULL)); status parser carries a
    dead repo_stats block with wrong key names (fw emits repo_stats only
    from selftest, keys fragmented/freeslots). tts bool-vs-Number and
    pubkey 66-vs-67 resolved in the SDK's favor from fw source.
reopened: []
cancelled: null
---

**Goal:** Every page and field of encedo-hem-api-doc is reconciled
against the SDK surface, with the result recorded: a coverage record
`docs/COVERAGE.md` (endpoint → SDK symbol | deliberately-unbound |
deferred-M10, with reasons), field-level diffs checked for the covered
pages, and the three `discrepancies/` files each mapped to where we
already recorded the divergence (REQ / KNOWN-ISSUES) or newly recorded.
Anything genuinely uncovered that 1.0 should bind comes back as draft
REQs + inserted steps (user decisions) — that is why this step runs
first.

**Notes:** Chore (`implements: []`) — analysis and documentation of
existing REQs; findings land as REQ revision bumps, not new behavior.
Known non-covered set going in (from decomposition scouting):
`system/upgrade-*` (M10), `system/diag` (DIAG-build-only,
unauthenticated, destructive — record deliberately-unbound),
`auth/init` (one-shot personalisation — propose deliberately-unbound
like `config/provisioning`), `logger/delete` (absent in fw v1.2.2,
already recorded), the `system/config` `wipeout` field, and a
field-level pass of `concepts/device-options` vs `ehem_config_info`.
Precedence for conflicts stays device > Manager > doc (§8); nothing is
resolved by fiat — open items become open criteria or user decisions.

**Definition of done**
- [x] `docs/COVERAGE.md` exists and dispositions every .md page of the
      doc repo (and every endpoint within multi-endpoint pages).
- [x] Field-level check done for every covered endpoint (request and
      response fields vs the SDK structs/params); differences recorded
      in the owning REQ (revision bump) or confirmed already recorded.
- [x] Every entry of the three `discrepancies/DISCREPANCIES-*.md`
      files is mapped to its REQ/KNOWN-ISSUES record or newly recorded.
- [x] Deliberate exclusions (diag, auth/init, provisioning, wipeout,
      upgrade→M10, stream/*) recorded with reasons in COVERAGE.md and,
      where an owning REQ exists, in that REQ.
- [x] Any must-bind gap presented to the user as draft REQ(s); user
      decisions recorded (or "no gaps" recorded in evidence.notes) —
      NO must-bind gaps; the two SDK-defect fix decisions are recorded
      in evidence.notes and presented in chat.
