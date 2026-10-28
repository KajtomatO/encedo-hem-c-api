---
id: STEP-M13-040
title: "examples/status.c and examples/sign.c, EHEM_BUILD_EXAMPLES compiled in CI; docs/API-GUIDE.md '## Examples and hem-tool'; REQ-API-007 re-verified"
milestone: M13
implements: ["REQ-API-007"]
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: ["STEP-M13-010"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the SDK ships runnable examples of its own now that hem-tool has
left: `examples/status.c` (context, system trust, `ehem_system_status`,
error detail) and `examples/sign.c` (passphrase login from the
environment, `keys list` lookup by label, `ehem_sign`, hex output), built
by default (`EHEM_BUILD_EXAMPLES`, ON when top level) and compiled on every
CI leg; the API guide points at them and at encedo-hem-tool.

**Notes:** public API only (the examples are consumers); no device needed
to build them; they are not CTests. `docs/API-GUIDE.md`'s `## hem-tool`
section becomes `## Examples and hem-tool`: the two examples explained in
a few lines each, then the pointer to encedo-hem-tool for the
full-featured client. The `docs_coverage` gate must stay green (the guide's
symbol index is unchanged). REQ-API-007's new criterion closes here.

**Definition of done**
- [ ] `examples/status.c`, `examples/sign.c`, `examples/CMakeLists.txt`;
      `EHEM_BUILD_EXAMPLES` option; both compile on gcc, clang and MinGW
      (ci.yml run id) under both backends.
- [ ] `examples/status.c` run against the dev device under system trust
      prints the status and exits 0; `examples/sign.c` signs with an
      `EHEMTEST`-prefixed key created and removed for the run (attended
      once, recorded here).
- [ ] `docs/API-GUIDE.md` "Examples and hem-tool" written; `docs_coverage`
      green.
- [ ] REQ-API-007 out of needs-reverify; TRACE regenerated.
