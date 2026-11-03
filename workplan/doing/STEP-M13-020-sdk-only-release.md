---
id: STEP-M13-020
title: "tls-probe and the SDK-only release — check-expired-classifier.sh on tls-probe, package.sh as the SDK-only orchestrator, package-static.sh deleted, release.yml with 8 assets; REQ-BUILD-010 implemented, REQ-NET-005 re-verified"
milestone: M13
implements: ["REQ-BUILD-010", "REQ-NET-005"]
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#1-decisions-fixed"]
depends_on: ["STEP-M13-010"]
evidence:
  commits: ["066da6d"]   # start; the implementation commit follows this edit; the done commit after the user's release dispatch
  tests:
    - "2026-11-03, dev builds (both go through Ubuntu's libcurl 8.5.0 on OpenSSL 3.0.13 as the TLS backend; the SDK's crypto backend wolfSSL 5.6.6 in build-m13, OpenSSL in build-openssl): `cmake --build <dir> --target tls-probe` OK on both; `check-expired-classifier.sh <dir>/tests/release/tls-probe 9443|9445` → expired leaf `rc=EHEM_ERR_NETWORK tls_expired=1 detail=/api/system/status: SSL certificate problem: certificate has expired`, not-yet-valid leaf `rc=EHEM_ERR_NETWORK tls_expired=0 … certificate is not yet valid` — classifier OK twice, both builds."
    - "2026-11-03, Release dry run against pinned prefixes built on this host the same day (build-deps.sh deps → wolfSSL 5.7.2 shared + libcurl 8.10.1 static; build-deps.sh deps-openssl openssl → OpenSSL 3.5.9 + libcurl 8.10.1, both static): `package.sh deps linux <dist>` — unit 37/37 (consumer_interface included) with LD_LIBRARY_PATH on the prefix, classifier under libcurl 8.10.1 + wolfSSL 5.7.2: expired → `tls_expired=1 … server verification failed: certificate has expired.`, not-yet-valid → `tls_expired=0 … server verification failed: certificate not valid yet.`; libencedo-hem.so.1.2.0 NEEDED libwolfssl.so.42 + libc.so.6 only, SONAME libencedo-hem.so.1, RUNPATH $ORIGIN; consumer_cmake_shared and consumer_pc_shared OK, both resolving libencedo-hem.so.1 and libwolfssl.so.42 from the unpacked package. `package.sh deps-openssl linux <dist> static` — unit 36/36 (export_symbols is not registered without a shared library), classifier under libcurl 8.10.1 + OpenSSL 3.5.9 OK for both leaves; archives libcrypto.a libcurl.a libencedo-hem.a libssl.a with no wolfSSL symbol; consumer_cmake_static and consumer_pc_static OK, the pkg-config --static closure linked. <dist> = encedo-hem-1.2.0+g066da6d-linux-x86_64.tar.gz, …-shared.tar.gz, wolfssl-5.7.2-linux-x86_64.tar.gz, wolfssl-5.7.2-stable-src.tar.gz, SHA256SUMS (4 lines) — the Linux half of the 8 assets; no file matching hem-tool in any archive; each package's README.txt names the versions and points at encedo-hem-tool, the wolfSSL README names the -shared SDK as what loads it. Both runs together ~3 min. Logs: scratchpad pkg-dynamic.log, pkg-static.log, deps-wolfssl.log, deps-openssl.log; dist in scratchpad dist-m13/."
    - "2026-11-03: `shellcheck scripts/release/*.sh` clean; `grep -rn hem-tool scripts/release .github/workflows/release.yml` → 19 lines, every one a pointer text (the README paragraphs, header comments naming encedo-hem-tool or the pre-M13 driver `hem-tool recovery`); no `implements:` tag names REQ-BUILD-005/009 or REQ-TOOL-026 any more. The release dispatch on `development` is the user's — run id recorded at the close."
  notes: "tests/release/tls_probe.c (public API only; EHEM_TLS_CA_FILE on the given CA; no_auto_checkin=1 — the verdict ehem_error.tls_expired is set either way (src/proto_common.c) and nothing but the one request leaves the process, so the old EHEM_CHECKIN_URL redirection is gone; prints `rc=… tls_expired=… detail=…`, exit 0 once printed) + tests/release/CMakeLists.txt (links EHEM_STATIC_TARGET; built with BUILD_TESTING, not a CTest — CI compiles it on every leg); tests/CMakeLists.txt adds the directory. check-expired-classifier.sh takes the probe and parses its line (`tls_expired=1` for the expired leaf; `rc=EHEM_ERR_NETWORK tls_expired=0` plus the date text for the not-yet-valid one). package.sh rewritten as the SDK-only orchestrator — both variants in one script: prefix stamp check, Release configure WITH the tests, `ctest -L unit`, the classifier on Linux (ports 8443/8444 dynamic, 8445/8446 static; Windows carries neither `openssl s_server` nor python3-cryptography), then package-wolfssl.sh + package-lib.sh shared, or package-lib.sh static, then write_sums; package-static.sh deleted. package-lib.sh: no -DEHEM_BUILD_TOOL=OFF; the 'hem-tool in the package' find became 'a program in bin/ other than the DLL'; README pointer paragraphs in both packages; the REQ id dropped from the user-facing Windows OPENSSL_CONF sentence. package-wolfssl.sh: README/HOWTO describe the -shared SDK as the loader. build-deps.sh: the Windows delay-load import library recipe removed (the --output-def link flag, the .def check, dlltool, the .def cleanup AND the prefix listing of libwolfssl.delay.a — caught by grep after the dry run; that listing alone would have failed the Windows job) — wolfssl stamp r3; comments retagged, 'REQ-BUILD-009 rev 4' kept where it names the recorded decision. release.yml: header, job names 'Linux x86_64' / 'Windows x86_64 (MinGW UCRT64)', two package steps per job, no separate classifier steps (package.sh runs the check inside the Linux package steps, as REQ-BUILD-010's text says). release-notes.sh and make-expired-cert.py retagged. Every release script and the workflow now carry `implements: REQ-BUILD-010`. Both GitHub prefix caches miss on the next run (build-deps.sh's hash is the key): expect cold builds (1.2.0 timings: Linux 3–4 min, Windows 8–13 min). NOT carried over from package-static.sh: the one-file binary's glibc-only / system-DLL-only / OPENSSL_CONF null-provider asserts — hem-tool's, ported by encedo-hem-tool's REQ-PKG-003; the library packages keep package-lib.sh's link asserts and consumer gates (REQ-BUILD-010's list). Docs that lag until STEP-M13-050: docs/RELEASING.md (variant table, package-static.sh, hem-tool attended checks), tests/README.md (tests/release/), ARCHITECTURE §10."
reopened: []
cancelled: null
---

**Goal:** a release dispatch on `development` builds, tests, asserts and
packages the SDK libraries only — 8 assets — with the REQ-NET-005
classifier proven end to end by `tls-probe` under both backends, and no
hem-tool anywhere in the release path.

**Notes:** `tests/release/tls_probe.c` (~60 lines, public API only):
`tls-probe <url> <cacert>` creates a context with the CA, calls
`ehem_system_status`, prints `rc=… tls_expired=… detail=…`; built when
`BUILD_TESTING`, not registered as a CTest.
`scripts/release/check-expired-classifier.sh <tls-probe> [port]` serves the
expired leaf and the not-yet-valid leaf (`make-expired-cert.py` as today)
and greps `tls_expired=1` for the first, `tls_expired=0` plus the date text
for the second. `package.sh <prefix> <os> dist [static]` = configure +
build Release with tests, `ctest -L unit`, the classifier check, then
`package-wolfssl.sh` (dynamic flavour only) + `package-lib.sh [static]`,
`write_sums`; `package-static.sh` deleted; the release workflow's step
names and texts follow; the packages' `README.txt` point at
encedo-hem-tool for the CLI. Retag the release scripts and the workflow
`implements: REQ-BUILD-010` (REQ-BUILD-005/009 are superseded). The
dispatch on `development` is the user's. Linux job time should drop by the
one-file build (1.2.0: 2:44 Linux, 10:26 Windows).

**Definition of done**
- [x] `tls-probe` builds on both backends; `check-expired-classifier.sh`
      passes locally against it (expired → `tls_expired=1`; not-yet-valid
      → `tls_expired=0` + the date text) on the dev build and on a Release
      build against a pinned prefix (REQ-NET-005's new criterion).
      *(2026-11-03: both dev builds; both Release builds against the pinned
      prefixes — the wolfSSL-backed libcurl included.)*
- [x] `package-static.sh` deleted; `package.sh` is the SDK-only
      orchestrator; `grep -rn hem-tool scripts/release
      .github/workflows/release.yml` → only the README pointer texts *(and
      the header comments pointing at encedo-hem-tool; 2026-11-03)*.
- [ ] Release dispatch on `development` (the user) green on both platforms:
      unit suites, link asserts, consumer gates, both classifier checks;
      artifacts = the 8 assets; no `hem-tool` file in any archive listing
      (run id recorded under REQ-BUILD-010).
- [ ] REQ-BUILD-010's first four criteria ticked; REQ-NET-005 out of
      needs-reverify; TRACE regenerated (REQ-BUILD-005/009 superseded rows
      carry no live code tag any more).
