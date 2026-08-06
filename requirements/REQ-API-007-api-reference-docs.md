---
id: REQ-API-007
title: API reference documentation for the 1.0 surface
status: approved
priority: must
revision: 1
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11 "API reference docs")
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#11-milestones"]
---

# API reference documentation for the 1.0 surface

The SDK SHALL ship API reference documentation covering every public
symbol (functions, structs, enums, macros) declared in
`include/ehem/*.h` at the 1.0 release.

- **Format (OPEN — user decision at M9 decomposition):** the public
  headers already carry thorough prose documentation for every symbol
  (contracts, ownership, defaults, firmware quirks). Candidate shapes:
  1. *(proposed)* **headers are the reference** + a hand-written
     `docs/` guide: an overview (conventions, error model, auth modes,
     memory/ownership rules), a per-header module index (symbol → one
     line), and worked examples — no duplication of per-symbol
     contracts, which stay in the headers;
  2. retrofit Doxygen markup onto all headers and generate HTML —
     mechanical churn across every public header right before an ABI
     freeze, and a new toolchain dependency;
  3. a fully hand-written per-symbol markdown reference — duplicates
     the headers and will drift.
- Whatever the shape, a completeness check is part of the deliverable:
  every `EHEM_API` symbol and public type is reachable from the docs
  (script-checkable, in the spirit of the export/header gates).
- The README points at the reference; hem-tool remains the runnable
  examples (ARCHITECTURE §8 "living documentation").

**Rationale:** 1.0 means third parties (first among them
encedo-pkcs11) build against this SDK without reading its source; the
contract must be findable without grepping headers. The user named API
reference docs a 1.0 deliverable (2026-08-06).

**Acceptance criteria:**
- [ ] Format decided by the user and recorded here (revision bump).
- [ ] Every `EHEM_API` function and every public struct/enum/macro in
      `include/ehem/*.h` is covered per the decided format; a
      completeness check (scripted, runnable via `./dev`) passes and
      is wired so a new public symbol without docs fails it.
- [ ] The conventions that span symbols are documented in one place:
      error model + `ehem_last_error`, ownership/`*_free` rules,
      `ehem_options` abi_size discipline, auth modes
      (passphrase/mobile), scope model (`keymgmt:use:<kid>`), TLS
      trust modes, and the automatic-recovery behaviors.
- [ ] README updated to the 1.0 surface and linking the reference.
