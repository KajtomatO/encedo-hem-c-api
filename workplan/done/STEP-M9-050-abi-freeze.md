---
id: STEP-M9-050
title: "ABI freeze — version 1.0.0"
milestone: M9
implements: ["REQ-API-008"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M9-040"]
evidence:
  commits: ["1d1d3c2"]
  tests: ["tests/unit/check_exports.cmake baseline pass (verifies: REQ-API-006 + REQ-API-008; negative-tested), tests/unit/test_context.c abi_size back-compat cases (pre-existing), tests/unit/test_version.c"]
  notes: >
    project VERSION 0.1.0 -> 1.0.0; SONAME libencedo-hem.so.1 (real name
    .so.1.0.0); ehem_version() -> "1.0.0" (hem-tool banner shows it).
    exports_baseline_1.0.txt freezes the 101 exported symbols (generated
    from nm of the built lib); check_exports.cmake gained the baseline
    pass — removal/rename = FATAL (fabricated-symbol negative test exit
    1), additions legal. 1.x ABI promise documented at ehem_version() in
    ehem.h; ARCHITECTURE §4 "decided before 1.0" resolved to "decided
    and shipped" (recording, not a decision change — mini §6.2 in chat:
    no §4-tracing REQ references the old wording; no reverify). abi_size
    back-compat direction already unit-pinned (test_context.c old-ABI
    block) — no new test needed. ./dev ci 41/41 GCC+Clang green with
    version 1.0.0. MinGW = CI-on-push (the .def-based export check runs
    there with the same baseline).
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
- [x] Version 1.0.0: CMake project version, `ehem_version()` unit
      test, SONAME .so.1, CMake package version all agree (no .pc file
      is shipped; the CMake package config is the consumption path).
- [x] 1.x ABI promise documented in ehem.h (at ehem_version) and the
      API guide; ARCHITECTURE §4 updated — recording-only edit, mini
      §6.2 reported in chat (no decision altered, no reverify).
- [x] Export-check baseline frozen (exports_baseline_1.0.txt, 101
      symbols): gate fails on removal/change (negative-tested, exit 1).
- [x] Unit: ehem_options with a smaller-than-current abi_size still
      yields working defaults — already existed (test_context.c).
- [x] Full unit suite + gates green GCC+Clang (41/41) + ASan; MinGW
      leg = CI-on-push.
