---
id: REQ-BUILD-008
title: Installed consumer interface — CMake package config and pkg-config file that resolve the selected backend's dependencies
status: verified
priority: must
revision: 3   # 2026-10-14 (STEP-M12-070 done): the wolfSSL CI evidence recorded under criteria 2 and 3; criterion 3 ticked (backend-independent). No meaning change. Rev 2 = 2026-10-13 (STEP-M12-070): wolfSSL-backend evidence recorded under each criterion; the REQ-API-008 / ARCHITECTURE criterion ticked (applied at the decomposition)
source: user decision 2026-10-12 (M12 planning: ship `encedo-hem.pc`); ARCHITECTURE.md §1 (artifact name — "pkg-config `encedo-hem`"), §11 M12 part B ("CMake package config fix": cmake/encedo-hem-config.cmake.in declares only find_dependency(CURL); the static target's crypto link interface cannot resolve in a consumer); REQ-API-008 criterion record "No separate pkg-config .pc file is shipped" (reversed here, §6.2 at the M12 decomposition); finding at the M12 decomposition 2026-10-12: ehem.h already gives static consumers plain declarations — the real gap is that the shared target does not advertise EHEM_USING_SHARED to its consumers (MinGW auto-import hides it); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-BUILD-001", "REQ-BUILD-006", "REQ-BUILD-007", "REQ-API-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones"]
---

# Installed consumer interface

An installed SDK SHALL be consumable through both CMake
(`find_package(encedo-hem CONFIG)`) and pkg-config (`encedo-hem.pc`) with
the dependencies of the backend it was built with resolved by the
package itself — a consumer links `encedo-hem::encedo-hem` or
`encedo-hem::encedo-hem-static`, or uses `pkg-config --cflags --libs
[--static] encedo-hem`, without naming libcurl, wolfSSL or OpenSSL.

- **CMake package config** (`cmake/encedo-hem-config.cmake.in`,
  configured per build): `find_dependency(CURL)` as today, plus the
  selected crypto backend — `PkgConfig` + `pkg_check_modules(WOLFSSL …
  IMPORTED_TARGET)` for wolfSSL, `find_dependency(OpenSSL)` for OpenSSL
  — so the static target's `INTERFACE_LINK_LIBRARIES` resolve in the
  consumer's configure. Only the variants built (REQ-BUILD-006) are
  exported.
- **Export macro for consumers:** the shared target carries
  `EHEM_USING_SHARED` as an INTERFACE compile definition, so a Windows
  consumer of the DLL gets `__declspec(dllimport)` declarations without
  defining anything (today MinGW's auto-import covers the omission
  silently; MSVC would not). Static consumers already get plain
  declarations from `ehem.h` as it is — no new define is needed (this
  corrects the "static consumers get dllimport" premise of the planning
  text).
- **pkg-config:** `encedo-hem.pc` generated and installed to
  `${libdir}/pkgconfig`: `Name: encedo-hem`, `Version` = the project
  version, `Cflags: -I${includedir}`, `Libs: -L${libdir} -lencedo-hem`,
  `Requires.private: libcurl` + `wolfssl` or `libcrypto` per backend, so
  `--static` yields a complete static link line. Installed only when a
  variant that it describes is built.
- **Consumer gate:** a CTest (label `unit`, both CI platforms) installs
  the build into a scratch prefix and configures, builds and runs a
  minimal consumer (`ehem_version()` + `ehem_options_init()` + a context
  create against the fake-free `http://127.0.0.1:1` URL) twice — via
  `find_package` and via `pkg_check_modules` — for every variant built.

**Rationale:** the CMake config has been incomplete since M2 (a static
consumer must find wolfSSL by itself), the §1 decision names pkg-config
`encedo-hem` and the 1.0 release recorded "no .pc shipped" only because
nothing needed one yet; the M12 library packages (REQ-BUILD-009) make
the installed interface a published artefact, and encedo-pkcs11's
autotools-style builds expect a `.pc`.

**Acceptance criteria:**
- [ ] A fresh `cmake --install` of the default build contains
      `lib/cmake/encedo-hem/encedo-hem-config.cmake` (with the wolfSSL
      dependency lookup) and `lib/pkgconfig/encedo-hem.pc`; an OpenSSL
      build installs the OpenSSL variants of both (file contents
      recorded).
      *(wolfSSL backend, 2026-10-13, STEP-M12-070 — asserted by the
      consumer gate on every configuration: `encedo-hem-config.cmake`
      gains, when the static variant is installed, `find_dependency(CURL)`,
      `find_dependency(PkgConfig)` and `pkg_check_modules(WOLFSSL QUIET
      IMPORTED_TARGET wolfssl)` with a NOT_FOUND message naming
      PKG_CONFIG_PATH; a shared-only install resolves nothing (the shared
      library carries both privately). `encedo-hem.pc` (default install):
      `prefix=${pcfiledir}/../..`, `libdir=${prefix}/lib`,
      `includedir=${prefix}/include`, `Name: encedo-hem`, `Version: 1.1.0`,
      `Requires.private: libcurl wolfssl`, `Cflags: -I${includedir}`,
      `Libs: -L${libdir} -lencedo-hem` — relocatable (one `..` per
      component of the libdir + pkgconfig; absolute install dirs give
      absolute paths). The export names libcurl as `$<1:CURL::libcurl>`:
      a bare name is recorded by its ALIASED target when curl's own
      package config made it an alias (`CURL::libcurl_static` with the
      release prefix — found by the gate), which FindCURL's module mode
      never defines. Shared-only / static-only installs export only their
      target (asserted). The OpenSSL variants: STEP-M12-090.)*
- [ ] The consumer gate passes on Linux (gcc, clang) and Windows (MSYS2)
      for shared and static targets via find_package AND pkg-config, on
      both backends (CI legs).
      *(The gate: `tests/consumer/` + `check_consumer.cmake`, CTest
      `consumer_interface`, label `unit`. Local 2026-10-13, all green:
      default, shared-only + tool shared, static-only, clang, ASan (the
      consumer built with the same sanitizer flags) and the Release build
      against the REQ-BUILD-005 prefix — each consumer prints the SDK
      version and the expected `EHEM_USING_SHARED` state. The static
      pkg-config consumer uses the `pkg-config --static` closure where
      every library of it links (the release prefix); on Ubuntu, libcurl's
      private libraries (nghttp2, idn2, rtmp, ssh, psl, gssapi_krb5, lber,
      ldap, zstd, brotlidec) have no linkable file, so there it links the
      archive with the shared libcurl + wolfSSL named by the .pc's
      Requires.private, and the log says which. CI (wolfSSL), 2026-10-13:
      run 38481008774 — Linux gcc, clang and both switch legs, Windows
      MSYS2 UCRT64 default and both switch legs — and release run
      38481505956's two jobs against the REQ-BUILD-005 prefix. OpenSSL:
      STEP-M12-090.)*
- [x] The installed shared target's `INTERFACE_COMPILE_DEFINITIONS`
      carry `EHEM_USING_SHARED`; the Windows consumer built against the
      DLL shows no auto-import linker note (recorded from the CI log).
      *(The gate asserts the installed `set_target_properties(
      encedo-hem::encedo-hem … INTERFACE_COMPILE_DEFINITIONS
      "EHEM_USING_SHARED"`; the find_package shared consumer reports the
      define, the static and pkg-config ones do not. The note itself
      never appears on x86_64: ld auto-imports a data reference silently
      (control 2026-10-14), so on MinGW the shared consumers link with
      `-Wl,--disable-auto-import` — a reference that needs auto-import
      fails the link. The DLL exports no data symbol. Green on the three
      Windows legs of CI run 38481008774, 2026-10-13.)*
- [ ] `pkg-config --libs --static encedo-hem` on an OpenSSL static-deps
      install links the consumer with no unresolved symbol (release
      prefix, REQ-BUILD-009).
      *(Partial 2026-10-13: against the REQ-BUILD-005 prefix — shared
      wolfSSL, static libcurl whose CMake-written libcurl.pc leaks
      `-lgcc -lgcc_s -lc` — the full `pkg-config --static` closure linked
      and ran. The all-static OpenSSL install: STEP-M12-120.)*
- [x] REQ-API-008's 1.0 record is annotated (rev bump, no status change —
      the ABI promise is untouched) and ARCHITECTURE §11 part B text
      corrected per the finding above.
      *(Applied at the M12 decomposition 2026-10-12: REQ-API-008 rev 2;
      ARCHITECTURE §11 part B "Export macro for consumers — corrected at
      the decomposition"; checked at STEP-M12-070.)*
