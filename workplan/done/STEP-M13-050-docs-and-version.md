---
id: STEP-M13-050
title: "Docs and process after the split — README, docs/API-GUIDE.md, docs/RELEASING.md, tests/README.md, KNOWN-ISSUES.md, BACKLOG.md, ARCHITECTURE (TLDR, §3, §8, §9, §10, §12), REQUIREMENTS-MANAGEMENT §8; project(VERSION 1.2.1)"
milestone: M13
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#10-directory-layout"]
depends_on: ["STEP-M13-010", "STEP-M13-020", "STEP-M13-030", "STEP-M13-040"]
evidence:
  commits: ["59fd337"]   # start; the implementation commit follows this edit; then the done commit + TRACE
  tests:
    - "2026-11-03: `cmake -B build-m13` after `project(VERSION 1.2.1)` — encedo-hem-config-version.cmake `PACKAGE_VERSION \"1.2.1\"`, encedo-hem.pc `Version: 1.2.1`; `ctest -L unit` 38/38 (test_version checks ehem_version() against the project version; docs_coverage green on the edited guide; consumer_interface and subdir_consumer pass EHEM_VERSION 1.2.1 through). Trace generator: broken anchors none (the §8 heading `#8-hem-tool-cli` and §10 kept their anchors), 75 tagged ids, no orphan."
    - "2026-11-03: `grep -rn -i hem-tool README.md docs KNOWN-ISSUES.md tests/README.md` → 72 lines, every one a pointer to encedo-hem-tool, a dated 'until 1.2.0 / since 1.2.1' sentence, or a FIXED-in-1.2.0 history record that now opens with a pointer note; no instruction tells a reader to build or run hem-tool from this repository."
  notes: "README: Status block (1.2.1 in development; latest release 1.2.0; the split, the examples, ehem::test-support, tls-probe; the 1.2 summary condensed to the SDK), Documentation list (examples/, hem-tool → encedo-hem-tool), Dependencies (no qrcodegen), Build options (EHEM_TOOL_LINK / EHEM_BUILD_TOOL rows gone; EHEM_BUILD_EXAMPLES and EHEM_BUILD_TEST_SUPPORT added; the Debug default is top-level only), disruptive-label paragraph (the three current tests), `./dev` block (no `tool`, no `--tool-link`), Download (the 8 assets, 'Which one do I want?' for the two packages + the CLI pointer, the 'until 1.2.0' note), `## hem-tool` → `## Examples and hem-tool`, Layout (examples/, tests/release, tests/consumer/subdir, tests/support as ehem::test-support; no src/tools), License paragraph. docs/RELEASING.md rewritten: 'Releasing the SDK packages', the two variants through package.sh, 8 assets, what the job proves (nothing to attend beyond a look at the set), the version rule's patch clause, timings of run 41670085883, the prefix table, the pin-bump note for encedo-hem-tool. tests/README.md: attended-only row, the switch legs (no tool leg), the forced-mismatch paragraph, the new sections on the subdirectory gate / ehem::test-support and the release-job helpers, the integration and disruptive paragraphs. KNOWN-ISSUES.md: the clock workaround names ehem_system_checkin first; keymgmt/update and the wipe section point at encedo-hem-tool's REQ-TOOL ids; the Windows-QR section is a pointer; the ext-pair QR and the foreign-libwolfssl sections open with a pointer note (SDK half kept). BACKLOG.md: items 6–9, 11, 12 → 'Moved to encedo-hem-tool' stubs with their notes; item 10 → done (M13 + T1); item 2 noted as the tool's. ARCHITECTURE.md: TLDR (the examples/CLI bullet, the release sentence), §1 (the -shared package links wolfSSL dynamically), §2 (hem-tool = encedo-hem-tool), §3 (diagram node, table rows hem-tool + examples/), §8 → a summary + the link under the same heading, §9 (attended-only wording; two new bullets: ehem::test-support + the subdir gate; the release-job proof), §10 (the tree), §12 risk 1 (no executable published here; the tripwires guard the packages). REQUIREMENTS-MANAGEMENT.md: the TOOL area row and the §8 sources table row for encedo-hem-tool. docs/API-GUIDE.md: the three remaining hem-tool mentions (QR rendering, mobile: lines, the backend paragraph) name encedo-hem-tool; docs/COVERAGE.md: 'Re-checked for 1.2.1 (M13): no change'. CMakeLists.txt: VERSION 1.2.1. The §11 M13 gate record and README's release date come with STEP-M13-060."
reopened: []
cancelled: null
---

**Goal:** every document describes the SDK-only repository and points at
encedo-hem-tool for the CLI; the version is 1.2.1.

**Notes:** chore (`implements: []`) — documentation and process. README:
the Status block (1.2.1, "hem-tool now lives in encedo-hem-tool"), the
Documentation list, `## Download` → the 8 assets + a link to the tool
releases, `## hem-tool` → a short pointer, the Layout. `docs/API-GUIDE.md`
(the intro lines naming hem-tool; the section STEP-M13-040 wrote).
`docs/RELEASING.md`: title "Releasing the SDK packages", the variant
table, the rc procedure with 8 assets and nothing to attend beyond the
consumer gates and the classifier, the version rule without the hem-tool
clause, timings. `tests/README.md` (labels, the tool tests gone, the
`ehem::test-support` target, the `subdir_consumer` gate).
`KNOWN-ISSUES.md`: the Windows-QR section → a pointer; the two mixed
sections keep their SDK parts, the tool bullets get a pointer.
`ARCHITECTURE.md`: TLDR; §3 diagram and table ("hem-tool — separate
repository"); §8 → a 10-line summary + the link (keep the heading so the
REQ anchors `#8-hem-tool-cli` stay valid); §9 (the attended-only class
without the tool commands; the test-support target); §10 (`examples/`,
`tests/release/`, `tests/support/CMakeLists.txt`, `tests/consumer/subdir/`;
no `src/tools/`); §12 risk 1 (the licensing tripwire is now the SDK's only
— no published binary carries wolfSSL). `BACKLOG.md`: items 6–9, 11, 12 →
"moved to encedo-hem-tool" stubs; item 10 → done (M13).
`REQUIREMENTS-MANAGEMENT.md` §8 table: a row for encedo-hem-tool
(authoritative for hem-tool behaviour and the REQ-TOOL ids; superseded
REQs here keep their frozen text). `CMakeLists.txt`: `project(VERSION
1.2.1)`; `docs_coverage` and the other gates stay green.

**Definition of done**
- [x] README, API-GUIDE, RELEASING, tests/README, KNOWN-ISSUES, BACKLOG
      updated as above; `grep -rn -i hem-tool README.md docs KNOWN-ISSUES.md
      tests/README.md` shows pointers only *(and dated history records that
      open with a pointer; 2026-11-03)*.
- [x] ARCHITECTURE.md TLDR, §3, §8, §9, §10, §12 updated; every REQ and
      step anchor still resolves (generator: broken anchors none).
      *(2026-11-03; §1 and §2 sentences too.)*
- [x] REQUIREMENTS-MANAGEMENT.md §8 row added. *(2026-11-03; the TOOL area
      row annotated as well.)*
- [x] `project(VERSION 1.2.1)`; `cmake -B build` shows 1.2.1 in the package
      version file; `./dev ci` green. *(2026-11-03: `./dev ci` gcc + clang
      38/38 (wolfSSL), `./dev ci --openssl` gcc + clang 38/38 — logs in the
      scratchpad, ci-m13-050.log / ci-m13-050-openssl.log.)*
