---
id: STEP-M12-000
title: "M12 placeholder: build variants & multi-package releases (link-mode / backend switches, static-wolfSSL warning, variant release matrix)"
milestone: M12
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M10-070"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Placeholder for milestone M12 (REQUIREMENTS-MANAGEMENT.md §5.3
step 5) — the early draft in ARCHITECTURE.md §11 (user request
2026-10-09): (1) CMake link-mode switches (which SDK variants to build; how
hem-tool links the SDK; whether third-party dependencies are linked
statically or dynamically — none of this exists today: both SDK variants
are always built, hem-tool always links the static SDK, deps link however
pkg-config / FindCURL resolve them); (2) a crypto/TLS backend switch
(`EHEM_CRYPTO_BACKEND=wolfssl|<tbd>`, the alternative library to be chosen
by the user — static packages use it, never wolfSSL); (3) a CMake WARNING
when a build resolves wolfSSL as a static archive (licensing, §12 risk 1);
(4) a multi-variant GitHub release — {SDK library package, hem-tool} ×
{dynamic, static} × {linux, windows, …}, including a full-dynamic hem-tool
(dynamic libcurl as well — user note 2026-10-09) and the library packages
(headers + libs + CMake config; fix the missing wolfSSL `find_dependency`
in `cmake/encedo-hem-config.cmake.in`; decide the `.pc` file); (5) a wolfSSL
library check + self-test at `ehem_global_init` — version check plus a
guard-banded known-answer test of the SDK's own wolfCrypt structs, so a
foreign `libwolfssl` (e.g. the host's 5.6.6 found 2026-10-10 — "stack
smashing detected") is refused with a clear error (user decision
2026-10-10: M12, not M10); (6) hem-tool
key types — `keys gen -h` lists every supported type and a separate command
explains each (signing, ECDH, encryption, defaults — user request
2026-10-10). Also carries a NOTE (user decision 2026-10-10): an
"initialisation creator" that hand-holds the user through device init —
not designed yet.

**Notes:** Chore placeholder (`implements: []`). It is cancelled and
replaced by detailed steps when M12 is decomposed (§5.3); the REQ drafts
(BUILD area, next free numbers from 006) are written at that point, once
the user has answered the deferred questions: the alternative library;
whether any static variant ships before that backend lands; the `.pc`
file; macOS / MSVC; the asset naming scheme; whether `ci.yml` or
`release.yml` carries the variant matrix. Facts to carry into the
decomposition: M10's release scripts (`scripts/release/build-deps.sh`,
`package.sh`, `package-wolfssl.sh`) already assert the link mode and
publish the wolfSSL companion asset the dynamic variants reuse; the
`public_headers_dep_free` gate and `src/crypto_shim.c` being the only
wolfCrypt includer are what make the backend switch tractable.

**Definition of done**
- [ ] Never completed as such — cancelled at M12 decomposition (§5.2) and
      replaced by STEP-M12-010… once the deferred questions are answered.
