---
id: STEP-M9-010
title: "Doc-repo conformance sweep + DISCREPANCIES reconciliation"
milestone: M9
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#6-protocol-bindings"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] `docs/COVERAGE.md` exists and dispositions every .md page of the
      doc repo (and every endpoint within multi-endpoint pages).
- [ ] Field-level check done for every covered endpoint (request and
      response fields vs the SDK structs/params); differences recorded
      in the owning REQ (revision bump) or confirmed already recorded.
- [ ] Every entry of the three `discrepancies/DISCREPANCIES-*.md`
      files is mapped to its REQ/KNOWN-ISSUES record or newly recorded.
- [ ] Deliberate exclusions (diag, auth/init, provisioning, wipeout,
      upgrade→M10, stream/*) recorded with reasons in COVERAGE.md and,
      where an owning REQ exists, in that REQ.
- [ ] Any must-bind gap presented to the user as draft REQ(s); user
      decisions recorded (or "no gaps" recorded in evidence.notes).
