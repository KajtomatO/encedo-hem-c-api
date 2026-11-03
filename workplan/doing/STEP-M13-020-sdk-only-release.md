---
id: STEP-M13-020
title: "tls-probe and the SDK-only release — check-expired-classifier.sh on tls-probe, package.sh as the SDK-only orchestrator, package-static.sh deleted, release.yml with 8 assets; REQ-BUILD-010 implemented, REQ-NET-005 re-verified"
milestone: M13
implements: ["REQ-BUILD-010", "REQ-NET-005"]
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#1-decisions-fixed"]
depends_on: ["STEP-M13-010"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** a release dispatch on `development` builds, tests, asserts and
packages the SDK libraries only — 8 assets — with the REQ-NET-005
classifier proven end to end by `tls-probe` under both backends, and no
hem-tool anywhere in the release path.

**Notes:** `tests/release/tls_probe.c` (~60 lines, public API only):
`tls-probe <url> <cacert>` creates a context with the CA, calls
`ehem_system_status`, prints `rc=… tls_expired=… detail=…`; built when
`BUILD_TESTING`, not registered as a CTest.
`scripts/release/check-expired-classifier.sh <tls-probe> [port]` serves the
expired leaf and the not-yet-valid leaf (`make-expired-cert.py` as today)
and greps `tls_expired=1` for the first, `tls_expired=0` plus the date text
for the second. `package.sh <prefix> <os> dist [static]` = configure +
build Release with tests, `ctest -L unit`, the classifier check, then
`package-wolfssl.sh` (dynamic flavour only) + `package-lib.sh [static]`,
`write_sums`; `package-static.sh` deleted; the release workflow's step
names and texts follow; the packages' `README.txt` point at
encedo-hem-tool for the CLI. Retag the release scripts and the workflow
`implements: REQ-BUILD-010` (REQ-BUILD-005/009 are superseded). The
dispatch on `development` is the user's. Linux job time should drop by the
one-file build (1.2.0: 2:44 Linux, 10:26 Windows).

**Definition of done**
- [ ] `tls-probe` builds on both backends; `check-expired-classifier.sh`
      passes locally against it (expired → `tls_expired=1`; not-yet-valid
      → `tls_expired=0` + the date text) on the dev build and on a Release
      build against a pinned prefix (REQ-NET-005's new criterion).
- [ ] `package-static.sh` deleted; `package.sh` is the SDK-only
      orchestrator; `grep -rn hem-tool scripts/release
      .github/workflows/release.yml` → only the README pointer texts.
- [ ] Release dispatch on `development` (the user) green on both platforms:
      unit suites, link asserts, consumer gates, both classifier checks;
      artifacts = the 8 assets; no `hem-tool` file in any archive listing
      (run id recorded under REQ-BUILD-010).
- [ ] REQ-BUILD-010's first four criteria ticked; REQ-NET-005 out of
      needs-reverify; TRACE regenerated (REQ-BUILD-005/009 superseded rows
      carry no live code tag any more).
