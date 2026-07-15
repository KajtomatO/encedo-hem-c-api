---
id: REQ-API-006
title: Shared library exports only ehem_-prefixed symbols
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §4 (ABI/versioning); §1 (artifact name and prefix, user decision 2026-07-15)
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#1-decisions-fixed"]
---

# Shared library exports only ehem_-prefixed symbols

Shared-library builds SHALL export only symbols carrying the `ehem_`
prefix.

**Rationale:** A PKCS#11 module embedding this SDK gets loaded into
arbitrary host processes; leaking internal or vendored symbols (cJSON,
Argon2) invites collisions. Visibility is hidden by default on GCC/Clang;
MinGW/Windows uses an export macro. The `ehem_` prefix is fixed to avoid
clashing with the `hem_*` seam inside encedo-pkcs11.

**Acceptance criteria:**
- [ ] Linux: `nm -D --defined-only` on the shared library lists only
      `ehem_*` symbols (scripted check in the unit suite or CI).
- [ ] Windows (MinGW): the DLL export table lists only `ehem_*` symbols.
- [ ] Vendored cJSON symbols are not exported.
- [ ] `ehem_version()` is exported and returns the runtime version string.
