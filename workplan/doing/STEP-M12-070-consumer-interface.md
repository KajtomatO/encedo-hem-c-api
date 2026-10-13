---
id: STEP-M12-070
title: "Installed consumer interface — CMake package config per backend, encedo-hem.pc, INTERFACE EHEM_USING_SHARED in the export, consumer-smoke CTest (find_package + pkg-config)"
milestone: M12
implements: ["REQ-BUILD-008"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-060"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** an installed SDK is consumable through `find_package(encedo-hem
CONFIG)` and `pkg-config encedo-hem` with the selected backend's
dependencies resolved by the package itself: the config declares
`find_dependency(CURL)` plus wolfSSL (PkgConfig) or OpenSSL per build,
exports only the variants built, the shared target advertises
`EHEM_USING_SHARED`; `encedo-hem.pc` is generated and installed; a CTest
installs into a scratch prefix and builds + runs a minimal consumer both
ways for every variant built. wolfSSL backend here — STEP-M12-090 adds
the OpenSSL variants and runs this gate on that backend.

**Notes:** `cmake/encedo-hem-config.cmake.in` today holds only
`find_dependency(CURL)`, so the static target's `INTERFACE_LINK_LIBRARIES`
(wolfSSL imported target) cannot resolve in a consumer — the M2-era gap.
Configure a `@EHEM_CONFIG_CRYPTO_DEPENDENCY@` block per backend:
`find_dependency(PkgConfig)` + `pkg_check_modules(WOLFSSL REQUIRED
IMPORTED_TARGET wolfssl)` / `find_dependency(OpenSSL)`; export only the
built variants (STEP-M12-060 options). `encedo-hem.pc.in` →
`${libdir}/pkgconfig/encedo-hem.pc`: `Name`, `Version` = project version,
`Cflags: -I${includedir}`, `Libs: -L${libdir} -lencedo-hem`,
`Requires.private: libcurl wolfssl` (or `libcurl libcrypto`), so
`--static` yields a complete line; installed only when a variant it
describes is built. Export macro: STEP-M12-060 put `INTERFACE
EHEM_USING_SHARED` on the shared target — here assert it appears in the
installed `encedo-hem-targets.cmake` export and that a Windows consumer
of the DLL builds without the auto-import linker note (CI log). Static
consumers need no define (ehem.h lines 25-32 already expand `EHEM_API`
to nothing — the planning text's premise was corrected at the
decomposition, ARCHITECTURE §11 B). Consumer gate: `tests/consumer/`
(CMakeLists.txt + `consumer.c`: `ehem_version()`, `ehem_options_init()`,
`ehem_ctx_create` against `http://127.0.0.1:1` — no traffic needed, the
link is the test) driven by a CTest script (label `unit`): `cmake
--install --prefix <scratch>`, configure the consumer with
`CMAKE_PREFIX_PATH`, build, run; second pass with `PKG_CONFIG_PATH` and
`pkg_check_modules(EHEM REQUIRED encedo-hem)`; shared and static where
built. MSYS2 has pkg-config; the `.pc` `Libs` works for the import lib.
`pkg-config --libs --static` with a fully static dependency set is proven
against the OpenSSL release prefix in STEP-M12-120 (REQ criterion 4) —
here, do it against the dev host's wolfSSL only if its archive exists
(STEP-M12-060's finding), else record "pending 120". REQ-API-008 rev 2
(the `.pc` annotation) and the ARCHITECTURE correction were applied at
the decomposition — nothing to edit there.

**Definition of done**
- [ ] Fresh `cmake --install` of the default build installs
      `lib/cmake/encedo-hem/encedo-hem-config.cmake` with the wolfSSL
      dependency lookup and `lib/pkgconfig/encedo-hem.pc` (contents
      recorded in the REQ); a shared-only / static-only install exports
      only what was built and installs the `.pc` accordingly.
- [ ] Consumer gate (`tests/consumer`, CTest label `unit`) passes on Linux
      gcc + clang and Windows MSYS2 for shared and static targets via
      find_package AND pkg-config (CI logs recorded).
- [ ] Installed shared target's `INTERFACE_COMPILE_DEFINITIONS` carry
      `EHEM_USING_SHARED`; the Windows consumer against the DLL shows no
      auto-import note (CI log).
- [ ] `pkg-config --libs --static encedo-hem` consumer links cleanly where
      a static wolfSSL exists locally, else recorded as proven by
      STEP-M12-120 against the OpenSSL prefix.
- [ ] `./dev ci` green incl. the new CTest; `implements: REQ-BUILD-008` tag
      in CMakeLists.txt / the config template; tests/README lists the gate.
