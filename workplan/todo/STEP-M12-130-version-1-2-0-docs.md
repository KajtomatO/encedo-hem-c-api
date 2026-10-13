---
id: STEP-M12-130
title: "Version 1.2.0 + user-facing docs (API-GUIDE, README, COVERAGE, KNOWN-ISSUES, RELEASING, tests/README) + v1.2.0-rc1 pre-release"
milestone: M12
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: ["STEP-M12-020", "STEP-M12-030", "STEP-M12-040", "STEP-M12-050", "STEP-M12-060", "STEP-M12-070", "STEP-M12-080", "STEP-M12-090", "STEP-M12-100", "STEP-M12-110", "STEP-M12-120"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the project reads 1.2.0 everywhere observable (`project(VERSION
1.2.0)`, `ehem_version()`, shared library `libencedo-hem.so.1.2.0` with
SONAME 1 kept, CMake package version, `encedo-hem.pc` `Version`); the
user-facing documentation describes 1.2.0 before anything is tagged —
the checklist of docs/RELEASING.md "Final release" that the M10 gate
missed; an annotated `v1.2.0-rc1` tag (the user tags) publishes a
pre-release carrying every asset of the REQ-BUILD-009 table.

**Notes:** Chore (`implements: []`): the version bump and the docs are
the milestone's, not one REQ's; STEP-M12-140 then verifies. Docs: 
`docs/API-GUIDE.md` — `ehem_build_info_get` + `EHEM_ERR_BACKEND_MISMATCH`
(the docs gate already forces the symbol; the error-model row and the
"backend compatibility" paragraph are prose); `README.md` — Status block
(1.2.0, date, scope, next milestone), hem-tool section (`--version`,
`keys types`, `init-wizard`, exit codes 12 and 6), build section (the
CMake options, `EHEM_CRYPTO_BACKEND`, `./dev build --openssl`),
"Download" (the asset table from STEP-M12-120), the Documentation list
and the Layout (shim split, `tests/consumer`, `tests/support/crypto_ref`);
`docs/COVERAGE.md` — M12 binds no new endpoint: re-check and say so in
its header; `KNOWN-ISSUES.md` — the foreign-libwolfssl crash and the
Windows 0xC0000135 entries become "fixed in 1.2.0: refused with exit 12 /
reported by hem-tool", the "Residual notes" (options.h decides layouts)
stay as the reason the self-test exists; `docs/RELEASING.md` — variants,
both prefixes, attended smoke per variant (STEP-M12-120 started this —
finish it); `tests/README` — the openssl legs, the consumer gate, the
tests-backend-free gate. `tests/unit/exports_baseline_1.0.txt` stays the
1.0 freeze (additions are legal; note the 1.2 addition in REQ-API-008's
record if not already). The release text for rc1 comes from the README
Status block + ARCHITECTURE §11 M12 (RELEASING "Final release"). The user
tags and pushes (`git tag -a v1.2.0-rc1 --cleanup=whitespace -F …`);
record the run id and the asset count in REQ-BUILD-009's tag-path
criterion; delete the rc afterwards or keep it — user's call. CI must be
green on `development` first.

**Definition of done**
- [ ] 1.2.0 everywhere observable: CMake project version, `ehem_version()`
      (test_version), SONAME 1 kept, package version file, `.pc` Version;
      `./dev ci` green.
- [ ] docs/API-GUIDE.md: new API indexed and the error model / backend
      compatibility prose added (docs gate green).
- [ ] README.md: Status block, hem-tool commands + exit codes, build
      options and backend switch, Download table, Documentation list,
      Layout — all at 1.2.0.
- [ ] docs/COVERAGE.md re-checked (no new endpoints — stated);
      KNOWN-ISSUES.md foreign-libwolfssl + 0xC0000135 entries closed as
      fixed in 1.2.0; docs/RELEASING.md and tests/README complete.
- [ ] CI green on `development` (all legs incl. openssl and the switch
      legs); `workflow_dispatch` release run green with every asset.
- [ ] `v1.2.0-rc1` suggested to the user; the pre-release carries every
      REQ-BUILD-009 asset with annotation + checksums (run id and count
      recorded in REQ-BUILD-009).
