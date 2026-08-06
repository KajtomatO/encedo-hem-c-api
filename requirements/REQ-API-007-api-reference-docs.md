---
id: REQ-API-007
title: API reference documentation for the 1.0 surface
status: verified
priority: must
revision: 2
source: user decision 2026-08-06 (M9 scope reshape, ARCHITECTURE.md §11 "API reference docs"); rev 2 = format decided (user decision 2026-08-06): headers stay the per-symbol reference + hand-written docs/ guide + scripted completeness gate
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

- **Format (DECIDED — user decision 2026-08-06, rev 2): headers are
  the per-symbol reference** + a hand-written `docs/` guide: an
  overview (conventions, error model, auth modes, memory/ownership
  rules), a per-header module index (symbol → one line), and worked
  examples — no duplication of per-symbol contracts, which stay in the
  headers. (Rejected: a Doxygen retrofit — churn across every public
  header right before the ABI freeze plus a new toolchain dependency;
  a fully hand-written per-symbol reference — duplicates the headers
  and drifts.)
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
- [x] Format decided by the user and recorded here (rev 2,
      2026-08-06): headers-as-reference + docs/ guide + scripted
      completeness gate.
- [x] Every `EHEM_API` function and every public struct/enum/macro in
      `include/ehem/*.h` is indexed in docs/API-GUIDE.md; the
      completeness check (tests/unit/check_docs_coverage.cmake, CTest
      `docs_coverage`, runs in `./dev test`/`./dev ci`) passes and
      fails on a missing symbol (negative-tested: empty docs → exit 1,
      2026-08-06).
- [x] The conventions that span symbols are documented in one place
      (docs/API-GUIDE.md "Conventions"): error model +
      `ehem_last_error`, ownership/`*_free` rules, `ehem_options`
      abi_size discipline, auth modes (passphrase/mobile), scope model
      (`keymgmt:use:<kid>`), TLS trust modes, and the
      automatic-recovery behaviors — plus worked examples
      (2026-08-06).
- [x] README updated to the 1.0 surface (status, docs section,
      hem-tool auth model incl. --mobile and the default URL, stale
      M1/Argon2 text removed) and linking the reference (2026-08-06).
