---
id: REQ-BUILD-006
title: Build configuration switches — library variants, hem-tool link mode, dependency link mode, static-wolfSSL warning
status: approved
priority: must
revision: 1
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
- [ ] The four options exist with the documented defaults; a default
      configure produces exactly today's targets, install set and link
      lines (diff of `cmake --install` listings and `ldd`/`objdump -p`
      of hem-tool before/after — no change).
- [ ] Each invalid combination (both variants OFF; tool shared without
      the shared variant; `EHEM_DEPS_LINK=static` with a dependency that
      has no archive) fails at configure with a message naming the
      option (negative configure runs, recorded).
- [ ] `EHEM_TOOL_LINK=shared`: hem-tool's `ldd`/import table lists
      `libencedo-hem` and the unit suite passes; `EHEM_BUILD_SHARED=OFF`:
      no shared library built or installed, suite passes;
      `EHEM_BUILD_STATIC=OFF`: no static library installed, suite passes
      (CI legs, both platforms where the distro packages allow).
- [ ] Static-wolfSSL warning fires when wolfSSL resolves to an archive and
      is absent otherwise (configure against the release prefix of
      REQ-BUILD-005 with and without `EHEM_DEPS_LINK=static`).
- [ ] `EHEM_DEPS_LINK=static` is exercised by the OpenSSL release builds
      (REQ-BUILD-009) against the pinned prefix.
- [ ] OPEN (needs verification at the step): whether the CI distro
      packages (Ubuntu libwolfssl-dev / libcurl4-*-dev, MSYS2 wolfssl /
      curl) ship static archives at all — decides whether an
      `EHEM_DEPS_LINK=static` leg can run in `ci.yml` or only in
      `release.yml`.
