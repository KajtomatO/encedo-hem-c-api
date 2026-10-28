---
id: REQ-BUILD-010
title: Release matrix without hem-tool — the SDK library packages per OS (OpenSSL static as the plain assets, wolfSSL shared as -shared), the wolfSSL companion and source, SHA256SUMS; 8 assets
status: approved
priority: must
revision: 1   # 2026-10-28 (M13 decomposition): drafted with the hem-tool split plan of 2026-10-27 and approved with it (the plan's §6.2 table: supersedes REQ-BUILD-005 and REQ-BUILD-009, whose hem-tool halves are encedo-hem-tool's REQ-PKG-003)
source: user decisions 2026-10-27 (the hem-tool split — clean split: SDK releases carry no hem-tool from 1.2.1 on; the one-file hem-tool is released from encedo-hem-tool, built against this repository's published static package); REQ-BUILD-009 rev 12 (the library-package half of the 1.2.0 matrix — carried over unchanged: names, contents, link asserts, consumer gates); REQ-BUILD-005 rev 6 (the workflow's triggers, the annotated-tag rule and release text); ARCHITECTURE.md §1 (wolfSSL is never statically bundled into a published package), §11 M13 part B; user decision 2026-10-14 (the wolfSSL library package is shared-only)
depends_on: ["REQ-BUILD-001", "REQ-BUILD-002", "REQ-BUILD-006", "REQ-BUILD-007", "REQ-BUILD-008", "REQ-NET-005", "REQ-API-010"]
supersedes: ["REQ-BUILD-005", "REQ-BUILD-009"]
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
---

# Release matrix without hem-tool

`release.yml` SHALL, on every push to `main`, on every annotated
`v<version>[-rc<N>]` tag and on dispatch, build and publish the SDK's
**library packages only** — per OS (Linux x86_64 on the `ubuntu-22.04`
glibc floor; Windows x86_64, MSYS2 UCRT64), against the pinned dependency
prefixes of REQ-BUILD-009 (OpenSSL 3.5 LTS static + libcurl; wolfSSL 5.7.2
shared + libcurl):

| Asset | Contents |
|---|---|
| `encedo-hem-<label>-<os>-x86_64.{tar.gz,zip}` | the plain package: `libencedo-hem.a` built on the OpenSSL backend, the public headers, the static OpenSSL and libcurl archives it needs, the CMake package config (`encodo-hem::encedo-hem-static`, version file = the project version), `encedo-hem.pc`, `README.txt`, `LICENSES/` — REQ-BUILD-009's static library package, unchanged |
| `encedo-hem-<label>-<os>-x86_64-shared.{tar.gz,zip}` | the wolfSSL package: `libencedo-hem.so.1` / `.dll` (+ import library) linked against the companion wolfSSL, headers, CMake config, `.pc`, `README.txt`, `LICENSES/` — REQ-BUILD-009's shared library package, unchanged (it needs the companion asset) |
| `wolfssl-5.7.2-<os>-x86_64.{tar.gz,zip}` | the companion shared wolfSSL (REQ-BUILD-005's asset, unchanged) |
| `wolfssl-5.7.2-stable-src.tar.gz` | the wolfSSL source (licensing; unchanged) |
| `SHA256SUMS` | one file over every archive above |

Eight assets per release; **no `hem-tool` file in any asset** — hem-tool is
released from [encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool)
(`c-v<version>` tags), built against the plain package of this release.

- **Per build, before packaging:** the unit suite (`ctest -L unit`) on the
  Release build of each backend against the pinned prefixes; the link
  asserts of REQ-BUILD-009 for both library packages (the plain package's
  archives carry no wolfSSL symbol or string and no shared crypto/curl
  dependency; the `-shared` library imports the companion wolfSSL and no
  libcurl / crypto of its own); the consumer gates in package mode
  (`find_package` + `pkg-config`, shared and static, REQ-BUILD-008); the
  REQ-NET-005 classifier check driven by **`tls-probe`**
  (`tests/release/tls_probe.c`: public API only, built when
  `BUILD_TESTING`, not a CTest) — the expired leaf → `tls_expired=1`, the
  not-yet-valid leaf → `tls_expired=0` with the date text — under both
  backends.
- **Tag rule and release text** as REQ-BUILD-005: the tag must match
  `project(VERSION)` (`v<version>` or `v<version>-rc<N>`, annotated) or the
  job fails before building; `<label>` = the tag minus `v`, or
  `<version>+g<sha>` on a branch run; the release text = the annotated
  tag's message followed by the checksums; `-rc` tags publish a
  pre-release; a push to `main` or a dispatch yields the archives as
  workflow artifacts only.
- `scripts/release/package.sh <prefix> <os> dist [static]` is the SDK-only
  orchestrator (configure + build Release with tests, the unit suite, the
  classifier check, then `package-wolfssl.sh` for the dynamic flavour and
  `package-lib.sh`, `write_sums`); `package-static.sh` — the one-file
  hem-tool build — is deleted.
- Each package's `README.txt` names the SDK, OpenSSL/wolfSSL and libcurl
  versions and points at encedo-hem-tool for the command-line client.
- 1.2.1 is a **patch** version (user decision 2026-10-28): CMake options and
  targets leave with the tool, no change under `include/` — the next SDK
  version after 1.2.0 is 1.2.1.

**Rationale:** the user's decisions of 2026-10-27 — the tool is a separate
deliverable with its own version and repository; a clean split keeps the
SDK release to what the SDK is. The library half of the 1.2.0 matrix is
carried over verbatim so that encedo-hem-tool, which pins and downloads the
plain package, sees no change in what it consumes.

**Acceptance criteria:**
- [ ] `package-static.sh` deleted; `package.sh` builds, tests, checks and
      packages the libraries only; `grep -rn hem-tool scripts/release
      .github/workflows/release.yml` → none but the pointer texts
      (STEP-M13-020).
- [ ] `tls-probe` exists and `check-expired-classifier.sh` drives it:
      expired leaf → `tls_expired=1`; not-yet-valid leaf → `tls_expired=0`
      and the date text; green locally on both backends (STEP-M13-020).
- [ ] A release dispatch on `development` yields the 8 assets as artifacts
      (run id); every assert, the unit suites, the consumer gates and the
      two classifier checks green on both platforms; no `hem-tool` file in
      any archive's listing (STEP-M13-020).
- [ ] Each package's `README.txt` names the versions and points at
      encedo-hem-tool (STEP-M13-020).
- [ ] Tag path: the user's annotated `v1.2.1-rc1` publishes the pre-release
      with the 8 assets, the text = annotation + checksums, `SHA256SUMS`
      re-verified in the publishing job (run id, release id; STEP-M13-060).
- [ ] Push to `main` (the 1.2.1 release): artifacts, no release; then the
      user's `v1.2.1` → the release with 8 assets (run ids, release id;
      recorded after the release as REQ-BUILD-009 rev 12 did).
