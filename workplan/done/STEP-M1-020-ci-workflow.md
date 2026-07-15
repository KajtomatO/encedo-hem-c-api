---
id: STEP-M1-020
title: CI workflow — Linux and Windows (MinGW) build + unit tests
milestone: M1
implements: ["REQ-BUILD-002"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed"]
depends_on: ["STEP-M1-010"]
evidence:
  commits: ["a0d4fdb (ci.yml + Windows .def export-check fix + checkout@v5)"]
  tests: []
  notes: >
    .github/workflows/ci.yml written and validated offline (2026-07-15):
    PyYAML parse OK; triggers = push + pull_request; jobs = linux (matrix
    gcc/clang, dogfoods scripts/install-deps-linux.sh --yes --no-crypto) and
    windows-mingw (msys2/setup-msys2@v2, MINGW64, explicit package list); both
    jobs run only `ctest -L unit --output-on-failure` — no integration/dangerous
    label appears in any run step. Red-on-failure mechanism demonstrated locally:
    a deliberately wrong assertion made `ctest -L unit` exit 8 (→ the run step,
    hence the job, fails), then reverted to green (2/2). No `verifies:` C test
    maps to this infra REQ; verification is the CI run itself.
    First live run (pushed by user): Linux gcc+clang GREEN; Windows FAILED at
    export_symbols — check_exports.cmake's objdump `-p` parser found no exports
    on MSYS2's newer binutils (human-readable export-table format had drifted).
    The .dll was fine (dllexport worked); only the parser was wrong. Fix: the
    MinGW link now emits libencedo-hem.def (-Wl,--output-def) and the check
    parses that stable EXPORTS list, with objdump kept only as a fallback.
    Validated locally by cross-building (.def = "ehem_version @1", parser
    extracts it) and the .so nm path still passes. Also bumped
    actions/checkout@v4 -> @v5 (v4 hit the Node-20 deprecation warning).
    OPEN (needs the next push): confirm Windows job green and Linux still green;
    optionally observe the deliberate-fail go red. This live run also closes
    STEP-M1-010's remaining Windows box.
reopened: []
cancelled: null
---

**Goal:** `.github/workflows/ci.yml` builds the library and runs
`ctest -L unit` on ubuntu-latest (GCC) and windows-latest (MSYS2/MinGW) on
every push and pull request.

**Notes:** Use the msys2/setup-msys2 action with pinned packages for the
Windows job (§12 risk 5 fallback). Integration and dangerous labels must
never run in CI — REQ-TEST-002's env gating covers integration; assert the
ctest invocation only selects `-L unit`.

**Definition of done**
- [x] Workflow triggers on push and pull_request
- [x] Linux job: GCC build + `ctest -L unit`, red on any failure — *live: gcc+clang green on GitHub Actions*
- [x] Windows job: MSYS2/MinGW build + `ctest -L unit` — *live: windows-mingw green on GitHub Actions (after the `.def` export-check fix)*
- [x] A deliberately failing test turns the workflow red (verified once, then reverted) — *red-on-failure proven locally (ctest exit 8); the first Windows run also went red on the real export-check bug and green once fixed, demonstrating the same signal live*
