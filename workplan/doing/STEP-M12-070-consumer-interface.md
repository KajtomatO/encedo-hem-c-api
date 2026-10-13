---
id: STEP-M12-070
title: "Installed consumer interface — CMake package config per backend, encedo-hem.pc, INTERFACE EHEM_USING_SHARED in the export, consumer-smoke CTest (find_package + pkg-config)"
milestone: M12
implements: ["REQ-BUILD-008"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-060"]
evidence:
  commits: ["74b2508"]   # start — the implementation, the CI run and the done commit follow
  tests:
    - "tests/consumer/check_consumer.cmake (CTest consumer_interface, label unit; verifies: REQ-BUILD-008): cmake --install into <build>/tests/consumer-gate/prefix; asserts the installed config (the wolfSSL lookup only with the static variant), the export (only the variants built; the shared target's INTERFACE_COMPILE_DEFINITIONS EHEM_USING_SHARED; libcurl exported as CURL::libcurl, never an aliased CURL::libcurl_static/_shared) and encedo-hem.pc (relocatable prefix, Version, Requires.private: libcurl wolfssl, Cflags, Libs); then configures tests/consumer against the install (both lookups must resolve inside it), builds (no 'auto-import' in the output) and runs every consumer: find_package shared/static, pkg-config shared/static — each prints the SDK version and its EHEM_USING_SHARED state, compared"
    - "Local 2026-10-13, all green: default (55/55), shared-only + EHEM_TOOL_LINK=shared (55/55), static-only (54/54), clang (55/55), ASan (55/55; the consumer gets the sanitizer flags) and the Release build against the REQ-BUILD-005 prefix exactly as package.sh configures it (55/55). The static pkg-config consumer: the full pkg-config --static closure against the release prefix; the archive + shared libcurl/wolfSSL on Ubuntu (libcurl's private libraries have no linkable files there) — the form is printed in the test log"
    - "./dev ci 55/55 gcc + clang; ./dev test asan 55/55"
  notes: |
    PENDING for done: the CI legs (Linux gcc + clang + the two switch legs,
    Windows MSYS2 default + the two switch legs) run consumer_interface in
    `ctest -L unit` — their first green run closes the Windows boxes (the
    gate natively on MSYS2: find_package + pkg-config, the --static closure
    with MSYS2's packages, no auto-import note); the next release.yml
    dispatch runs it in both release configurations too.
    Found while building the gate: (1) an exported link interface records a
    bare ALIAS by its aliased name — the release prefix's curl package
    config makes CURL::libcurl an alias of CURL::libcurl_static, which
    FindCURL's module mode never defines, so a consumer with another curl
    lookup could not import the static target. The static target now
    links $<BUILD_INTERFACE:CURL::libcurl> + $<INSTALL_INTERFACE:$<1:CURL::libcurl>>
    (INSTALL_INTERFACE content alone is unwrapped and still resolved). (2)
    curl 8.10.1's CMake-written libcurl.pc leaks the compiler's implicit
    libraries (-lgcc -lgcc_s -lc) into Libs/Libs.private — linkable by name,
    so the consumer treats them like system libraries; its Windows build (the
    STEP-M12-100 cross prefix) lists -lmingw32 -lmoldname -lmingwex too, added
    to the same list before the release job runs the gate. (3) CMake
    function/macro gotchas in the gate script: a macro re-expands ${...}
    inside its arguments (the .pc's literal ${pcfiledir}), and ARGN splits
    any argument containing `;` (a path list) — expect_in() is a function,
    and esc() escapes `;` before step() forwards a command. (4) CI
    38428646903 failed the gate on all three Windows legs: a list element
    cannot end in `\` (with the list's next `;` it reads as an escaped `;`),
    and MSYS2's PATH ends in ...\WindowsPowerShell\v1.0\, so
    `cmake -E env PATH=...` swallowed the executable ("no command given").
    PATH and PKG_CONFIG_PATH now reach the children through the script's
    environment, the prefix path in forward slashes; replayed on Linux (the
    old gate fails exactly so, the new one passes).
    Design: the config's dependency block is generated per build
    (EHEM_CONFIG_DEPENDENCIES — STEP-M12-090 adds the OpenSSL one); the .pc
    is relocatable through ${pcfiledir} (one `..` per libdir component +
    pkgconfig; absolute install dirs give absolute paths) — the library
    packages of STEP-M12-110/120 unpack anywhere. pkg-config consumers of
    the DLL on Windows define EHEM_USING_SHARED themselves (the .pc cannot:
    its Cflags serve the static library too) — API-GUIDE's EHEM_API row
    says so; README "Install" shows the pkg-config line.
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
- [x] Fresh `cmake --install` of the default build installs
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
- [x] `pkg-config --libs --static encedo-hem` consumer links cleanly where
      a static wolfSSL exists locally, else recorded as proven by
      STEP-M12-120 against the OpenSSL prefix.
      (The full --static closure linked and ran against the REQ-BUILD-005
      prefix; the dev host's libwolfssl.a exists but libcurl's closure has
      no linkable files on Ubuntu — the all-static install: STEP-M12-120.)
- [x] `./dev ci` green incl. the new CTest; `implements: REQ-BUILD-008` tag
      in CMakeLists.txt / the config template; tests/README lists the gate.
