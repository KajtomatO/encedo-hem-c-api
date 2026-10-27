---
id: REQ-API-008
title: 1.0 ABI freeze and versioning
status: verified
priority: must
revision: 3   # 2026-10-20 (STEP-M12-137): annotation only — `ehem_ext_qr_payload` exported, one more 1.2.0 minor-bump addition; no status change
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11 "ABI freeze and the 1.0 release"); ARCHITECTURE.md §4 (semver, size/version discipline decided before 1.0); rev 2 — annotation 2026-10-12 (M12 decomposition, §6.2 approved in chat; no status change — the ABI promise is untouched): the 1.0 record "no .pc file is shipped" is reversed by REQ-BUILD-008 from 1.2.0, and 1.2.0's additions (`EHEM_ERR_BACKEND_MISMATCH` appended to `ehem_rc`, `ehem_build_info_get` exported, and — rev 3, 2026-10-20, STEP-M12-137 — `ehem_ext_qr_payload` exported) are the minor-bump additions this REQ permits
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
- [x] Version reads 1.0.0 everywhere observable (2026-08-06):
      `ehem_version()` returns "1.0.0" (test_version asserts it matches
      the project version); CMake package version file carries 1.0.0;
      shared lib builds as libencedo-hem.so.1.0.0 with SONAME
      libencedo-hem.so.1. (No separate pkg-config .pc file is shipped —
      the CMake package config is the consumption path. *Rev 2,
      2026-10-12: a 1.0 fact — REQ-BUILD-008 ships `encedo-hem.pc` from
      1.2.0 beside the CMake config; the versioning contract here is
      unchanged.*)
- [x] The 1.x ABI promise is documented at ehem_version() in ehem.h
      and in docs/API-GUIDE.md (abi_size conventions section);
      ARCHITECTURE §4 reworded to "decided and shipped" — a recording
      of the implemented discipline, not a decision change, so no REQ
      reverification followed (mini §6.2: affected set = §4-tracing
      REQs, all verified, no criterion references the old wording;
      reported in chat 2026-08-06).
- [x] tests/unit/exports_baseline_1.0.txt freezes the 101 exported
      1.0 symbols; check_exports.cmake gained the baseline pass —
      removal/rename fails the gate (negative-tested with a fabricated
      baseline symbol, exit 1, 2026-08-06); additions stay legal
      (minor bumps).
- [x] Unit: the backward-compat direction was already pinned —
      tests/unit/test_context.c drives an old-ABI options block whose
      abi_size predates request_pace_ms and asserts defaults apply
      (plus the abi_size==0 rejection case). No new test needed.
