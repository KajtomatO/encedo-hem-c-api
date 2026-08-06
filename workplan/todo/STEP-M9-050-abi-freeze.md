---
id: STEP-M9-050
title: "ABI freeze — version 1.0.0"
milestone: M9
implements: ["REQ-API-008"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M9-040"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** The project version is 1.0.0 everywhere it is observable
(`ehem_version()`, CMake package/pkg-config, shared-lib
VERSION/SOVERSION → 1), the 1.x ABI promise is documented (ehem.h + the
API reference), and the export gate is frozen as a baseline that fails
on symbol removal or change, not just unexpected additions.

**Notes:** Runs after all surface-touching work (M9-040 chain) so the
frozen baseline is the release surface. The ehem.h "0.x until
full-spec conformance" comment and ARCHITECTURE §4's "decided before
1.0" wording get updated — the §4 edit alters a section REQs trace to,
so run the §6.2 impact analysis first (expected outcome: wording
resolution only, no reverify — but let the procedure say so).
Growth disciplines being ratified (not invented): ehem_options
abi_size + append-only; ehem_rc append-only; library-allocated output
structs may grow; symbol additions = minor.

**Definition of done**
- [ ] Version 1.0.0: CMake project version, `ehem_version()` unit
      test, SOVERSION 1, pkg-config/CMake package version all agree.
- [ ] 1.x ABI promise documented in ehem.h and the API reference;
      ARCHITECTURE §4 updated (via §6.2 — report + user approval
      recorded).
- [ ] Export-check baseline frozen: gate fails on removal/change of a
      1.0 symbol (negative-tested).
- [ ] Unit: ehem_options with a smaller-than-current abi_size still
      yields working defaults (backward-compat direction) — exists or
      added.
- [ ] Full unit suite + gates green GCC+Clang+ASan; MinGW cross-syntax
      clean.
