---
id: REQ-BUILD-006
title: Build configuration switches — library variants, hem-tool link mode, dependency link mode, static-wolfSSL warning
status: approved
priority: must
revision: 2   # 2026-10-13 (STEP-M12-060): OPEN criterion (distro static archives) resolved; local evidence recorded. No meaning change — approved status kept
source: ARCHITECTURE.md §11 M12 part B (early draft 2026-10-09, planned 2026-10-12: "link-mode switches — none exist today"; candidate names settled here); §1 (wolfSSL is never statically bundled into a published package — user decision 2026-10-09); §12 risk 1; REQ-BUILD-001 (both variants, one configure); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-BUILD-001", "REQ-BUILD-002", "REQ-API-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
---

# Build configuration switches

The CMake build SHALL offer options that choose which SDK variants are
built, how hem-tool links the SDK, and how third-party dependencies are
linked — with today's behaviour as every default:

| Option | Values (default first) | Meaning |
|---|---|---|
| `EHEM_BUILD_SHARED` | `ON` / `OFF` | build + install `libencedo-hem.so` / `.dll` (`encedo-hem::encedo-hem`) |
| `EHEM_BUILD_STATIC` | `ON` / `OFF` | build + install `libencedo-hem.a` (`encedo-hem::encedo-hem-static`) |
| `EHEM_TOOL_LINK` | `static` / `shared` | hem-tool (and hem-tool-core) link the static SDK, or the shared one |
| `EHEM_DEPS_LINK` | `default` / `static` / `shared` | third-party libraries (libcurl, the crypto library) resolve as the toolchain finds them (today), or prefer static archives, or prefer shared libraries |

- Both variants `OFF` is a configure error; `EHEM_TOOL_LINK=shared` with
  `EHEM_BUILD_SHARED=OFF` likewise. `EHEM_BUILD_TOOL` (existing) is
  unchanged.
- `EHEM_DEPS_LINK=static` prefers `.a` archives (`CMAKE_FIND_LIBRARY_
  SUFFIXES`, `pkg-config --static`); a dependency without a static
  archive is a configure error naming it, never a silent fall-back.
  `shared` prefers `.so` / `.dll.a`.
- **Static-wolfSSL warning:** whenever the resolved wolfSSL library is a
  static archive (any `EHEM_DEPS_LINK` value, wolfSSL backend), configure
  emits `message(WARNING …)` naming the licensing consequence (§12 risk
  1: wolfSSL is GPLv2-or-later/commercial — a binary carrying it must
  comply or hold a commercial licence; no published package of this
  project does). The release scripts' refusal to package such a binary
  (REQ-BUILD-005) stays.
- The unit suite stays buildable under every combination: when the
  static variant is off, the tests that use hidden internal seams link a
  private, non-installed static build of the SDK objects.
- `./dev build` exposes the switches (REQ-BUILD-004); `ci.yml` gains legs
  that configure non-default combinations and run the unit suite +
  gates (at least: shared-only + `EHEM_TOOL_LINK=shared`; static-only).

**Rationale:** today both variants are always built, hem-tool always
links the static SDK, and the dependencies link however pkg-config /
FindCURL resolve them — the release scripts steer by prefix layout, not
by options. M12's release matrix (REQ-BUILD-009) needs the choices to be
explicit and asserted, consumers need a library-only configure, and the
licensing rule needs a tripwire at configure time rather than at
packaging time only.

**Acceptance criteria:**
- [x] The four options exist with the documented defaults; a default
      configure produces exactly today's targets, install set and link
      lines (diff of `cmake --install` listings and `ldd`/`objdump -p`
      of hem-tool before/after — no change).
      *(2026-10-13, STEP-M12-060: `EHEM_BUILD_SHARED` / `EHEM_BUILD_STATIC`
      (options, ON), `EHEM_TOOL_LINK` (cache string, static) and
      `EHEM_DEPS_LINK` (cache string, default) in CMakeLists.txt; the
      default configure on the dev host before vs after (scratch build
      dirs): the 16-file install listing, hem-tool's NEEDED set (libcurl.so.4,
      libwolfssl.so.42, libc.so.6), hem-tool's link line and
      libencedo-hem.so's link line are all IDENTICAL. The one intended
      difference: the exported shared target now carries
      `INTERFACE_COMPILE_DEFINITIONS EHEM_USING_SHARED` (dllimport for
      Windows consumers; nothing elsewhere) — STEP-M12-070 asserts it.)*
- [x] Each invalid combination (both variants OFF; tool shared without
      the shared variant; `EHEM_DEPS_LINK=static` with a dependency that
      has no archive) fails at configure with a message naming the
      option (negative configure runs, recorded).
      *(Recorded 2026-10-13: both OFF → "EHEM_BUILD_SHARED and
      EHEM_BUILD_STATIC are both OFF - nothing would be built";
      `EHEM_BUILD_SHARED=OFF EHEM_TOOL_LINK=shared` → "EHEM_TOOL_LINK=shared
      needs the shared SDK, but EHEM_BUILD_SHARED is OFF"; unknown values of
      the two string options are refused by name; `EHEM_DEPS_LINK=static` on
      the Ubuntu packages → "libcurl (pkg-config libcurl) needs libnghttp2,
      and there is no static archive of it"; against the REQ-BUILD-005
      release prefix → "wolfSSL (pkg-config wolfssl) needs libwolfssl, and
      there is no static archive of it" — that prefix ships wolfSSL shared
      only (§1); `EHEM_DEPS_LINK=shared` against it → "libcurl resolved to a
      static library". A package's OWN library must come from its own
      pkg-config libdir: the first static attempt against the release
      prefix silently picked up the SYSTEM libwolfssl.a (5.6.6) under the
      prefix's 5.7.2 headers — caught and fixed before the commit.)*
- [ ] `EHEM_TOOL_LINK=shared`: hem-tool's `ldd`/import table lists
      `libencedo-hem` and the unit suite passes; `EHEM_BUILD_SHARED=OFF`:
      no shared library built or installed, suite passes;
      `EHEM_BUILD_STATIC=OFF`: no static library installed, suite passes
      (CI legs, both platforms where the distro packages allow).
      *(Local proof 2026-10-13, Linux gcc: shared-only + `EHEM_TOOL_LINK=shared`
      → hem-tool NEEDED libencedo-hem.so.1 (no libcurl/libwolfssl of its
      own), install = the .so set only, unit 54/54 (hem-tool-core-shared for
      the exe; the tests keep a static hem-tool-core on the private
      archive); static-only → install = libencedo-hem.a only, no .so built,
      unit 53/53 (export_symbols needs a shared library and is not
      registered); shared-only with the default tool link → hem-tool carries
      the SDK from the private, never-installed libencedo-hem-internal.a,
      unit 54/54. The ci.yml legs (Linux gcc + Windows MSYS2, each asserting
      the link result; the assertion script proven to pass and to fail
      locally) are written — this box closes with their first green run.)*
- [x] Static-wolfSSL warning fires when wolfSSL resolves to an archive and
      is absent otherwise (configure against the release prefix of
      REQ-BUILD-005 with and without `EHEM_DEPS_LINK=static`).
      *(The release prefix has no wolfSSL archive (shared only, §1), so
      there `EHEM_DEPS_LINK=static` is the configure ERROR above, and without
      it no warning. The warning itself, with `EHEM_DEPS_LINK=default`
      against a scratch prefix whose wolfssl.pc points at an archive only
      (Ubuntu's libwolfssl.a): "CMake Warning: wolfSSL resolves to a STATIC
      archive in this configuration: every binary built from it carries
      wolfSSL, which is licensed GPLv2-or-later … (ARCHITECTURE.md §12 risk
      1). No published package of this project links wolfSSL statically,
      and the release scripts refuse to package such a build
      (REQ-BUILD-005)." — and the status line reads "crypto wolfSSL 5.6.6
      (static)". The default configure on Ubuntu: no warning.)*
- [ ] `EHEM_DEPS_LINK=static` is exercised by the OpenSSL release builds
      (REQ-BUILD-009) against the pinned prefix.
- [x] RESOLVED at STEP-M12-060 (2026-10-13): Ubuntu 24.04 (the dev host =
      ubuntu-latest) ships `libwolfssl.a` (libwolfssl-dev) and `libcurl.a`
      (libcurl4-openssl-dev), but libcurl's `pkg-config --static` closure
      has NO archive for ten of its libraries (nghttp2, idn2, rtmp, ssh,
      psl, gssapi_krb5, lber, ldap, zstd, brotlidec) — a static configure
      fails there by design. MSYS2 UCRT64 (packages downloaded from
      repo.msys2.org: wolfssl 5.9.4-1, curl 8.22.0-1) ships `libwolfssl.a`
      and `libcurl.a` with a closure of ssh2, idn2, OpenSSL, zlib, brotli,
      zstd, nghttp2/ngtcp2/nghttp3, psl. Decision: no `EHEM_DEPS_LINK=static`
      leg in `ci.yml` — Linux cannot, and what MSYS2 could link puts wolfSSL
      inside the binaries, the configuration §1 forbids publishing; the
      static dependency link is exercised by `release.yml`'s OpenSSL builds
      (criterion above, STEP-M12-120). Recorded in ci.yml's header.
