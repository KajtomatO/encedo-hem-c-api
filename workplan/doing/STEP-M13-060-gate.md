---
id: STEP-M13-060
title: "M13 gate — ci.yml 10 legs, release dispatch with 8 assets, v1.2.1-rc1 (the user tags), docs checklist, TRACE, README Status 1.2.1, then v1.2.1"
milestone: M13
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M13-050"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** SDK 1.2.1 ships without hem-tool: CI green, a release candidate
published from the user's annotated `v1.2.1-rc1` with the 8 assets, the
consumer gates and both classifier checks green in its run, the docs
checklist re-verified, TRACE with no `needs-reverify` and no orphan tag;
then `development` → `main` and the user's `v1.2.1`.

**Notes:** milestone gate (chore). Nothing to attend beyond the workflow
runs: the packages are consumed by the consumer gates in the release job,
and encedo-hem-tool's package-mode CI leg against this release's plain
package is the external smoke (a pin bump there follows as its own
step). The docs checklist of docs/RELEASING.md "Final release" applies:
README Status block with version and date, Documentation list, Layout;
`docs/COVERAGE.md` (unchanged — no endpoint bound); KNOWN-ISSUES. The
release run and release ids are recorded under REQ-BUILD-010; the final
tag's are added after the release (as REQ-BUILD-009 rev 12 did). The
release notes are drafted from the README and the §11 entry, Markdown,
tagged with `--cleanup=whitespace -F`.

**Definition of done**
- [ ] ci.yml 10/10 on `development` (run id); the release dispatch's
      assets = 8, consumer gates and classifier checks green (STEP-M13-020's
      run or a fresh one).
- [ ] The user's annotated `v1.2.1-rc1` → pre-release with 8 assets,
      `SHA256SUMS` re-verified in the job (run id, release id — REQ-BUILD-010
      tag criterion); the published plain Linux package smoke-tested here
      (checksum, `find_package` consumer build, `--version`-less: the
      config version file reads 1.2.1).
- [ ] Docs checklist re-verified; TRACE regenerated — no `needs-reverify`,
      no orphan tag, no open M13 step but this one.
- [ ] ARCHITECTURE §11 M13 marked gate-passed; README Status 1.2.1;
      `v1.2.1` suggested to the user (merge `development` → `main`, annotated
      tag from the drafted notes).
