---
id: STEP-M12-060
title: "CMake switches — EHEM_BUILD_SHARED / EHEM_BUILD_STATIC / EHEM_TOOL_LINK / EHEM_DEPS_LINK, static-wolfSSL licensing WARNING, private test archive, ./dev flags, ci.yml legs"
milestone: M12
implements: ["REQ-BUILD-006"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the build offers `EHEM_BUILD_SHARED` (ON), `EHEM_BUILD_STATIC`
(ON), `EHEM_TOOL_LINK=static|shared`, `EHEM_DEPS_LINK=default|static|shared`
with today's behaviour as every default; invalid combinations fail at
configure naming the option; a wolfSSL that resolves to a static archive
triggers a `message(WARNING)` naming the licensing consequence (§12 risk
1); the unit suite builds under every combination; `./dev build` exposes
the switches; `ci.yml` runs the non-default legs.

**Notes:** Today (CMakeLists.txt): both `encedo-hem` SHARED and
`encedo-hem-static` unconditional, `hem-tool-core` STATIC →
`encedo-hem::encedo-hem-static`, `find_package(CURL)`,
`pkg_check_modules(WOLFSSL REQUIRED IMPORTED_TARGET wolfssl)`,
`-Wl,--exclude-libs,ALL`, options `EHEM_SANITIZE`, `EHEM_BUILD_TOOL`.
Plan: an OBJECT library feeds both variants and a private, non-installed
static target the tests link when `EHEM_BUILD_STATIC=OFF` (the tests
reach hidden seams, so they cannot link the shared library alone);
`EHEM_TOOL_LINK=shared` links hem-tool-core/hem-tool to `encedo-hem` — on
Windows that needs `EHEM_USING_SHARED` on the consumer, so land the
shared target's `INTERFACE` compile definition here (STEP-M12-070 then
asserts it in the installed export). `EHEM_DEPS_LINK=static`:
`CMAKE_FIND_LIBRARY_SUFFIXES` archives first, `pkg_check_modules` static
flags (`WOLFSSL_STATIC_LIBRARIES`/`_LDFLAGS`), `CURL_USE_STATIC_LIBS`
(check the project's `cmake_minimum_required` supports it, else set the
suffix list before `find_package(CURL)`); a dependency with no archive is
a configure error naming it — never a silent fall-back. Archive
detection for the warning: inspect the resolved wolfSSL library path's
extension (`.a`/`.lib`), any `EHEM_DEPS_LINK`; the WARNING text names
§12 risk 1 and says no published package of this project does this; the
release scripts' refusal (REQ-BUILD-005 link-mode assertion) stays. Also
emit a compile definition for the resolved crypto link mode
(`EHEM_CRYPTO_LINK_STATIC`) — STEP-M12-020's `--version` prints the word.
`./dev build` flags (names at the step, e.g. `--shared-only`,
`--static-only`, `--tool-link shared`, `--deps static`). `ci.yml` legs:
shared-only + `EHEM_TOOL_LINK=shared`; static-only — Linux, and Windows
where MSYS2 packages allow. Resolve the REQ's OPEN criterion first:
whether Ubuntu `libwolfssl-dev`/`libcurl4-*-dev` and MSYS2 `wolfssl`/
`curl` ship static archives at all (`dpkg -L` on the dev host, the MSYS2
package listing) — decides whether an `EHEM_DEPS_LINK=static` leg can run
in `ci.yml` or only against the release prefix (`release.yml`,
STEP-M12-120); record in the REQ (rev bump). Gotcha: on the dev host
`EHEM_TOOL_LINK=shared` resolves libwolfssl from the system — fine for
development, but no release conclusions from it (memory: the system
wolfSSL masks clean-runner failures). The default configure must produce
exactly today's targets, install set and link lines — record the
before/after diff of `cmake --install` listings and `ldd`/`objdump -p
hem-tool`.

**Definition of done**
- [ ] Four options with the documented defaults; default configure
      unchanged (install listing + hem-tool `ldd`/`objdump -p` diffs
      recorded in the REQ or notes).
- [ ] Negative configures recorded: both variants OFF; tool shared without
      the shared variant; `EHEM_DEPS_LINK=static` with a dependency lacking
      an archive — each fails naming the option.
- [ ] `EHEM_TOOL_LINK=shared`: hem-tool links `libencedo-hem` (ldd/import
      table), suite green; `EHEM_BUILD_SHARED=OFF` and `EHEM_BUILD_STATIC=OFF`
      each build + pass the suite (private test archive) — ci.yml legs
      green on both platforms where the distro packages allow.
- [ ] Static-wolfSSL WARNING fires against the REQ-BUILD-005 release prefix
      with `EHEM_DEPS_LINK=static` and is absent otherwise (recorded).
- [ ] `./dev build` exposes the switches (REQ-BUILD-004 doc updated);
      shared target carries `INTERFACE EHEM_USING_SHARED`;
      `EHEM_CRYPTO_LINK_STATIC` definition emitted.
- [ ] REQ-BUILD-006 OPEN criterion (distro static archives) resolved and
      recorded (rev bump); `implements: REQ-BUILD-006` tag in
      CMakeLists.txt / ci.yml; `./dev ci` gcc + clang + ASan green on the
      default configure.
