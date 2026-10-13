---
id: REQ-BUILD-008
title: Installed consumer interface — CMake package config and pkg-config file that resolve the selected backend's dependencies
status: approved
priority: must
revision: 1
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
- [ ] The consumer gate passes on Linux (gcc, clang) and Windows (MSYS2)
      for shared and static targets via find_package AND pkg-config, on
      both backends (CI legs).
- [ ] The installed shared target's `INTERFACE_COMPILE_DEFINITIONS`
      carry `EHEM_USING_SHARED`; the Windows consumer built against the
      DLL shows no auto-import linker note (recorded from the CI log).
- [ ] `pkg-config --libs --static encedo-hem` on an OpenSSL static-deps
      install links the consumer with no unresolved symbol (release
      prefix, REQ-BUILD-009).
- [ ] REQ-API-008's 1.0 record is annotated (rev bump, no status change —
      the ABI promise is untouched) and ARCHITECTURE §11 part B text
      corrected per the finding above.
