---
id: REQ-BUILD-002
title: CI builds and runs unit tests on Linux and Windows
status: implemented
priority: must
revision: 1
source: ARCHITECTURE.md §1 (GitHub Actions; integration stays local, user decision 2026-07-15)
depends_on: ["REQ-BUILD-001", "REQ-TEST-002"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed"]
---

# CI builds and runs unit tests on Linux and Windows

Continuous integration SHALL build the library and run the unit-labeled
test suite on Linux and on Windows (MinGW) for every push and pull
request.

**Rationale:** Cross-platform breakage (MinGW especially — §12 risk 5) must
surface at the commit that causes it. CI runs only the `unit` label:
integration tests need the dev-machine device and stay local until CI
gains device access (user decision 2026-07-15); REQ-TEST-002's gating is
what keeps CI green without one.

**Acceptance criteria:**
- [x] `.github/workflows/ci.yml` triggers on push and pull_request.
- [x] Linux job: build (GCC) + `ctest -L unit`, failing the workflow on
      any test failure. *(matrix gcc+clang; green)*
- [x] Windows job: MSYS2/MinGW build + `ctest -L unit`. *(green)*
- [x] Integration and dangerous labels are never executed in CI. *(only
      `-L unit` invoked; asserted by offline YAML check)*
