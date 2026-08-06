---
id: STEP-M9-040
title: "API reference docs + README for the 1.0 surface"
milestone: M9
implements: ["REQ-API-007"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M9-010", "STEP-M9-030"]
evidence:
  commits: ["b2a7420"]
  tests: ["tests/unit/check_docs_coverage.cmake (verifies: REQ-API-007 — CTest `docs_coverage`, every public symbol indexed; negative-tested)"]
  notes: >
    docs/API-GUIDE.md: conventions (error model, ownership, abi_size,
    auth modes, scope model, TLS, recoveries, timeouts), worked examples
    (list + sign/verify), per-header symbol index covering ALL public
    symbols (99 EHEM_API functions + typedefs + EHEM_* macros; header
    guards excluded). Completeness gate = CTest `docs_coverage` (cmake
    -P script like the export/header gates) — green on the guide,
    exit 1 on an empty docs file (negative-proven). README rewritten to
    the 1.0 surface: status paragraph, Documentation section, hem-tool
    section (auth grouping, --mobile exits 13/14, default URL), stale
    M1/Argon2/M2 text removed; docs/ added to Layout. ./dev ci green
    (41 CTest units incl. docs_coverage, GCC+Clang). REQ-API-007 →
    verified (rev 2 format decision recorded earlier same day).
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
- [x] Format decision recorded in REQ-API-007 (rev 2, user decision
      2026-08-06: headers-as-reference + docs/ guide + gate).
- [x] Reference complete per the decided format; conventions
      documented in one place (docs/API-GUIDE.md).
- [x] Completeness check scripted as CTest `docs_coverage` (runs in
      every ./dev test / ci), proven to fail on missing symbols
      (empty-docs negative test, exit 1).
- [x] README rewritten to the 1.0 surface, linking the reference
      and COVERAGE.md; stale early-development text removed.
- [x] Existing gates stay green — ./dev ci 41/41 GCC+Clang.
