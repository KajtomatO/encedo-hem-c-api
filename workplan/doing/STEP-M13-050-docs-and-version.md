---
id: STEP-M13-050
title: "Docs and process after the split — README, docs/API-GUIDE.md, docs/RELEASING.md, tests/README.md, KNOWN-ISSUES.md, BACKLOG.md, ARCHITECTURE (TLDR, §3, §8, §9, §10, §12), REQUIREMENTS-MANAGEMENT §8; project(VERSION 1.2.1)"
milestone: M13
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#10-directory-layout"]
depends_on: ["STEP-M13-010", "STEP-M13-020", "STEP-M13-030", "STEP-M13-040"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] README, API-GUIDE, RELEASING, tests/README, KNOWN-ISSUES, BACKLOG
      updated as above; `grep -rn -i hem-tool README.md docs KNOWN-ISSUES.md
      tests/README.md` shows pointers only.
- [ ] ARCHITECTURE.md TLDR, §3, §8, §9, §10, §12 updated; every REQ and
      step anchor still resolves (generator: broken anchors none).
- [ ] REQUIREMENTS-MANAGEMENT.md §8 row added.
- [ ] `project(VERSION 1.2.1)`; `cmake -B build` shows 1.2.1 in the package
      version file; `./dev ci` green.
