---
id: STEP-M1-020
title: CI workflow — Linux and Windows (MinGW) build + unit tests
milestone: M1
implements: ["REQ-BUILD-002"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed"]
depends_on: ["STEP-M1-010"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Workflow triggers on push and pull_request
- [ ] Linux job: GCC build + `ctest -L unit`, red on any failure
- [ ] Windows job: MSYS2/MinGW build + `ctest -L unit`
- [ ] A deliberately failing test turns the workflow red (verified once, then reverted)
