---
id: STEP-M9-040
title: "API reference docs + README for the 1.0 surface"
milestone: M9
implements: ["REQ-API-007"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M9-010", "STEP-M9-030"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** The REQ-API-007 reference exists in the user-decided format,
its completeness check runs via `./dev` (a new public symbol without
docs fails it), and the README is rewritten to the 1.0 surface (SDK
usage, hem-tool with the new help/auth model, links to the reference
and docs/COVERAGE.md).

**Notes:** FIRST ACTION: get the format decision (REQ-API-007 open
criterion) and record it in the REQ (revision bump). Proposed at
decomposition: headers stay the per-symbol reference; add a
hand-written `docs/` guide — overview, conventions (error model,
ownership/`*_free`, abi_size discipline, auth modes, scope model, TLS
modes, automatic recoveries), per-header symbol index, worked examples
— plus a scripted completeness gate (every `EHEM_API` symbol reachable
from the docs). Depends on M9-010 (COVERAGE.md feeds the docs; sweep
may have adjusted REQ records) and M9-030 (README documents the final
tool UX).

**Definition of done**
- [ ] Format decision recorded in REQ-API-007 (revision bump).
- [ ] Reference complete per the decided format; conventions
      documented in one place (the REQ's criterion list).
- [ ] Completeness check scripted, wired into `./dev` (and the check
      suite), and proven to fail on an undocumented public symbol
      (negative-tested like the header gates).
- [ ] README rewritten to the 1.0 surface, linking the reference.
- [ ] Existing gates stay green (export/header checks, unit suite).
