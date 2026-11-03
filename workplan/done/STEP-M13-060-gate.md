---
id: STEP-M13-060
title: "M13 gate — ci.yml 10 legs, release dispatch with 8 assets, v1.2.1-rc1 (the user tags), docs checklist, TRACE, README Status 1.2.1, then v1.2.1"
milestone: M13
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M13-050"]
evidence:
  commits: ["557efd8", "58da33b"]   # start; the rc1 fix; the done commit (this edit + TRACE) follows — the release records come after the user's v1.2.1
  tests:
    - "2026-11-03, the push of 557efd8: CI run 41685611197 (development) and 41685599284 (the tag) — 10/10 legs each."
    - "2026-11-03, `v1.2.1-rc1` on 557efd8 — release run 41685599368 FAILED: Windows x86_64 green (both package steps), Linux x86_64 red in step 8 (the -shared package): package-lib.sh's shared build (`-DEHEM_BUILD_STATIC=OFF -DBUILD_TESTING=OFF`, EHEM_BUILD_EXAMPLES defaulting ON at top level since STEP-M13-040) built examples/status and examples/sign against libencedo-hem.so.1.2.1 without `-rpath-link` to the prefix, so ld could not resolve the library's wolfSSL references (`libwolfssl.so.42 … not found`, 100+ undefined wolfSSL_*/wc_* symbols). The full build in package.sh has `-Wl,-rpath-link,<prefix>/lib` and linked the same examples fine; this host's system libwolfssl.so.42 (5.6.6) had masked the gap in the local dry run (the known trap of this host). Fix: package-lib.sh configures with `-DEHEM_BUILD_EXAMPLES=OFF` — the package build holds the library alone. Verified locally as an rc2 tag build: both variants green, build-lib has no example target or executable, build-release's examples still link with -rpath-link. The publishing job was skipped, so nothing was published under rc1; the candidate is replaced by `v1.2.1-rc2`."
    - "2026-11-03, `v1.2.1-rc2` on 58da33b (the rc1 tag deleted by the user): CI runs 41689668960 (development) and 41689764914 (the tag) 10/10 each; release run 41689764961 green in all three jobs — Linux 3:30 (prefixes cold 0:37 + 1:37, package steps 0:14 / 0:18), Windows 8:22 (1:08 + 4:57, 0:21 / 0:24), publish 0:22 — pre-release 433053084 with the 8 assets and the annotation + SHA256SUMS text. Independent verification from the public assets on this host: checksums OK (7 archives), every listing libraries-only (no program; the Windows -shared bin/ holds the DLL alone), version files 1.2.1 in all four packages, README pointers present; tests/consumer/check_consumer.cmake package mode OK against the unpacked plain package (static consumers, the pkg-config --static closure) and the -shared package + companion (both libraries resolved from the package); examples/status.c linked with `pkg-config --static` against the published plain package (no dynamic curl/ssl/crypto dependency) ran against the dev device under system trust, exit 0 — the REQ-BUILD-010 tag-path criterion (5) ticked, rev 3."
  notes: "Gate record so far: the docs checklist of docs/RELEASING.md 'Final release' was written in STEP-M13-050 (README Status '1.2.1 in development' — the date comes with the release; COVERAGE re-checked; KNOWN-ISSUES); TRACE after STEP-M13-050: 72 verified / 4 implemented / 0 approved / 0 needs-reverify / 30 superseded, no orphan, this step the only open M13 step. Release notes drafted (scratchpad release-notes-1.2.1.md)."
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
- [x] ci.yml 10/10 on `development` (run id); the release dispatch's
      assets = 8, consumer gates and classifier checks green (STEP-M13-020's
      run or a fresh one). *(CI 41689668960 on 58da33b; the dispatch
      41670085883 (ef63648) and the rc2 tag run 41689764961 on the final
      code.)*
- [x] The user's annotated `v1.2.1-rc1` → pre-release with 8 assets,
      `SHA256SUMS` re-verified in the job (run id, release id — REQ-BUILD-010
      tag criterion); the published plain Linux package smoke-tested here
      (checksum, `find_package` consumer build, `--version`-less: the
      config version file reads 1.2.1). *(rc1 failed and was replaced:
      `v1.2.1-rc2` → run 41689764961, pre-release 433053084; smoke-tested
      as the evidence records, both Linux packages and the status example.)*
- [x] Docs checklist re-verified; TRACE regenerated — no `needs-reverify`,
      no orphan tag, no open M13 step but this one. *(2026-11-03: README
      Status/Documentation/Layout, COVERAGE's 1.2.1 re-check, KNOWN-ISSUES
      pointers — STEP-M13-050's set re-read; TRACE 72 verified / 4
      implemented / 0 approved / 0 needs-reverify / 30 superseded, no
      orphan, no broken anchor.)*
- [x] ARCHITECTURE §11 M13 marked gate-passed; README Status 1.2.1;
      `v1.2.1` suggested to the user (merge `development` → `main`, annotated
      tag from the drafted notes). *(2026-11-03; the release records —
      REQ-BUILD-010 criterion 6 and its status, the README date, the §11
      'Released' line, TRACE — follow the user's tag as a records commit.)*
