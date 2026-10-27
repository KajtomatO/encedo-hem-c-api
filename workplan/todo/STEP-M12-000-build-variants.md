---
id: STEP-M12-000
title: "M12 placeholder: build variants, multi-package releases & hem-tool UX (foreign-wolfSSL refusal, link-mode / backend switches, OpenSSL backend, library packages, key types, init wizard)"
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
cancelled: "M12 decomposed 2026-10-12 (user-approved, §5.3) — replaced by STEP-M12-010..140; scope carried by REQ-API-010/011, REQ-BUILD-006..009, REQ-TOOL-024..028"
---

**Goal:** Placeholder for milestone M12 (REQUIREMENTS-MANAGEMENT.md §5.3
step 5). Early draft 2026-10-09; **planned 2026-10-12** — the user settled
the scope and every question the draft had deferred (ARCHITECTURE.md §11,
M12 entry, is the authoritative scope). One milestone, one release
(1.2.0), five parts:
(A) foreign-library crash & diagnostics — the wolfSSL version check +
guard-banded known-answer self-test at `ehem_global_init` (user decision
2026-10-10), no init failure masked as `EHEM_ERR_NOMEM`, hem-tool calling
`ehem_global_init` up front, `hem-tool --version`, the Windows missing-DLL
message (BACKLOG item 5, moved in 2026-10-12), and the REQ-NET-005 `-150`
classifier fix; (B) build system — link-mode switches, the static-wolfSSL
CMake WARNING, the CMake package config fix, a static-consumer define for
Windows, and the `encedo-hem.pc` file (user decision 2026-10-12);
(C) library packages & the release matrix — SDK library packages, a static
hem-tool and a static library package (on OpenSSL), asset-name suffixes,
`ci.yml` legs for the switches/backends; (D) the OpenSSL 3.x backend (user
decision 2026-10-12) behind `EHEM_CRYPTO_BACKEND=wolfssl|openssl`;
(E) hem-tool UX — key types (`keys gen -h` lists all 23, a separate
command explains each; user request 2026-10-10) and the initialisation
wizard as a separate command (`init-device` stays scripted; shape decided
2026-10-12).

**Notes:** Chore placeholder (`implements: []`). It is cancelled and
replaced by detailed steps when M12 is decomposed (§5.3); the REQ drafts
are written at that point. Answers to the questions the draft deferred
(user, 2026-10-12): alternative library = OpenSSL 3.x; no split (one
M12); the static variants ship in M12, on OpenSSL, never wolfSSL; ship the
`.pc` file; macOS and MSVC out (BACKLOG candidates); the full-dynamic
hem-tool dropped; today's hem-tool asset name stays and new variants get a
suffix; `ci.yml` exercises the switches and both backends, `release.yml`
builds the published variants. Left for the decomposition: option names,
the asset suffix scheme, the new error code's name, the wizard's command
name, what "static" means on Linux (expected: third-party libraries
static, glibc dynamic), proving the MinGW delay-load mechanism. Expected
§6.2 runs: REQ-API-008 (`.pc`), REQ-API-003 (new error code), REQ-NET-005
(`-150`), REQ-BUILD-005 (variants, asset names), REQ-TOOL-009 (`keys gen`
help). Facts to carry into the decomposition: M10's release scripts
(`scripts/release/build-deps.sh`, `package.sh`, `package-wolfssl.sh`)
already assert the link mode and publish the wolfSSL companion asset the
wolfSSL flavour reuses; the `public_headers_dep_free` gate and
`src/crypto_shim.c` being the only wolfCrypt includer are what make the
backend switch tractable; the SDK allocates six wolfCrypt structs itself
(`Hmac`, `WC_RNG`, `DecodedCert`, `Aes`, `ed25519_key`, `ed448_key`) —
the self-test's subjects; the shim's ECDSA/EdDSA verify and AES-CBC
functions are test/simulator-only.

**Definition of done**
- [ ] Never completed as such — cancelled at M12 decomposition (§5.2) and
      replaced by STEP-M12-010… .
