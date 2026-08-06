---
id: REQ-API-008
title: 1.0 ABI freeze and versioning
status: approved
priority: must
revision: 1
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11 "ABI freeze and the 1.0 release"); ARCHITECTURE.md §4 (semver, size/version discipline decided before 1.0)
depends_on: ["REQ-API-003", "REQ-API-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#11-milestones"]
---

# 1.0 ABI freeze and versioning

Release 1.0.0 SHALL freeze the public ABI: from 1.0 on, exported
symbols, public struct layouts, and enum values change only under
semantic-versioning rules (breaking change ⇒ major bump).

- **Version:** CMake `project(VERSION 1.0.0)` (from 0.1.0);
  `ehem_version()` returns `"1.0.0"` (already wired via
  `EHEM_VERSION_STRING`); shared-library `SOVERSION` becomes `1` and
  stays `1` for all of 1.x.
- **The growth disciplines are already in place — 1.0 ratifies and
  documents them as the contract:**
  - `ehem_options`: `abi_size` stamped by `ehem_options_init()`,
    append-only fields, zero = default for every scalar (ehem.h §
    "Context options") — new fields keep landing in 1.x minors without
    breaking either ABI direction;
  - `ehem_rc`: append-only, never renumbered (ehem.h comment);
  - output structs (`ehem_*_info`): library-allocated, freed by their
    `ehem_*_free` — they may grow in minors because callers never
    size them;
  - exported symbols: additions = minor; removals/signature changes =
    major. The export-check gate's symbol list is the enforcement
    point.
- Header text saying "0.x until full-spec conformance" (ehem.h,
  ARCHITECTURE §4) is updated to state the 1.x promise.
- Scope is the ABI/versioning act itself; the human-readable reference
  is REQ-API-007.

**Rationale:** encedo-pkcs11 links this SDK into arbitrary host
processes; it needs a stable ABI to depend on, and 1.0 is the promise.
The disciplines were designed in from M1 (abi_size, append-only enum,
hidden visibility + export macro) — the release makes them normative.

**Acceptance criteria:**
- [ ] Version reads 1.0.0 everywhere it is observable: `ehem_version()`
      (unit test), pkg-config/CMake package version, shared-lib
      SOVERSION/VERSION properties.
- [ ] The 1.x ABI promise (the four bullets above) is documented in
      ehem.h and the API reference; ARCHITECTURE §4's "decided before
      1.0" wording is resolved to "decided" (§6.2 impact analysis if
      the edit alters traced meaning).
- [ ] The export check's expected-symbol list is frozen as the 1.0
      baseline: the gate fails on symbol *removal or change*, not just
      on unexpected additions (extend the existing check if needed).
- [ ] Unit: `ehem_options_init` + an abi_size smaller than
      sizeof(ehem_options) still yields working defaults (the
      backward-compat direction of the discipline) — test exists or is
      added.
