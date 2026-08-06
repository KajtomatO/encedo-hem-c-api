---
id: STEP-M9-015
title: "Sweep defect fixes: conditional blv, dead status repo_stats"
milestone: M9
implements: ["REQ-SYS-001", "REQ-SYS-002"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M9-010"]
evidence:
  commits: ["6dbde91", "be7c702"]
  tests: ["tests/unit/test_system.c (test_version_no_bootloader_footer, test_version_missing_required now hwv-keyed, test_status_full tolerant-ignore of repo_stats)"]
  notes: >
    ./dev ci green (GCC+Clang, 38 CTest units incl. the new blv-less
    case inside test_system; export/header gates) + ./dev test asan
    clean. LIVE regression: hem-tool status against my.ence.do parses
    status+version (bootloader triple present on the dev device).
    §6.2 applied for the REQ-SYS-002 criterion rework: affected set =
    REQ-SYS-002 (rev 2, needs-reverify -> verified at the post-step
    regen), parse_version, test_system.c version cases; STEP-M1-070
    stays in done/ (rework carried here, user-approved insertion).
    REQ-SYS-001 rev 2 is a record-only touch (criteria already aligned
    with the current doc). Public-struct removal (ehem_repo_stats)
    is pre-1.0-legal; no consumer existed (grep-proven). MinGW leg =
    CI-on-push (no new format strings / identifiers; cross-compile not
    run locally this step).
reopened: []
cancelled: null
---

**Goal:** The two SDK defects found by the STEP-M9-010 conformance sweep
(user decision 2026-08-06: "Fix both now") are fixed before the M9-050
ABI freeze: (1) `ehem_system_version` no longer hard-requires `blv` —
firmware emits `blv`/`blk`/`bls` only when the bootloader footer's
publisher matches (`if (bldr != NULL)`, fw api_system.c), so a legal
response must parse with `blv` NULL; (2) the status parser's dead
`repo_stats` block and the public status-side surface
(`ehem_repo_stats`, `has_repo_stats`) are removed — firmware emits
`repo_stats` only from the selftest handler and with different key
names (`fragmented`/`freeslots`), so the status fields could never
populate on any real firmware and nothing consumes them.

**Notes:** Mid-milestone insertion (M7-072/074 precedent). The blv
relaxation reworks a REQ-SYS-002 acceptance criterion → §6.2 applied:
affected set = REQ-SYS-002 (criterion edit, rev 2, needs-reverify until
the regen), src/proto_system.c parse_version, tests/unit/test_system.c
version cases; STEP-M1-070 stays in done/ (rework carried by this
step). REQ-SYS-001 needs only a rationale/record touch (rev 2) — its
criteria reference "the documented response fields" and the current doc
excludes repo_stats from status, so the removal aligns the code WITH
the REQ. Public-struct removal is legal pre-1.0 (0.1.0); this is
exactly the pre-freeze cleanup window. The status unit fixture keeps a
`repo_stats` object to prove tolerant-ignore.

**Definition of done**
- [x] parse_version treats `blv` (and `blk`/`bls`, already optional) as
      optional; a blv-less fixture parses with `blv == NULL`; `hwv`/`fwv`
      stay required (unit tests).
- [x] `ehem_repo_stats`, `has_repo_stats`, and the status parse block
      are gone; the status fixture's `repo_stats` object is tolerantly
      ignored (unit test); grep shows no remaining consumer.
- [x] REQ-SYS-002 criterion reworded (rev 2, §6.2 recorded);
      REQ-SYS-001 rationale updated (rev 2); docs/COVERAGE.md defect
      bullets marked fixed.
- [x] `./dev ci` + ASan green; export/header gates green; MinGW leg =
      CI-on-push (no new format strings/identifiers; recorded in notes).
- [x] TRACE regenerated; REQ-SYS-002 restored to verified from fresh
      evidence.
