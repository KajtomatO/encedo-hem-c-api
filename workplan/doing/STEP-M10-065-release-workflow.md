---
id: STEP-M10-065
title: "Release workflow: static hem-tool for Linux + Windows, artifacts on main, GitHub Release on tags"
milestone: M10
implements: ["REQ-BUILD-005"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: []
evidence:
  commits: []   # the user commits (never-commit rule); SHA to be backfilled
  tests:
    - "LOCAL DRY RUN 2026-10-09 (Linux leg, the exact scripts the workflow calls): scripts/release/build-deps-static.sh → wolfSSL 5.7.2 + curl 8.10.1 static (CMake, wolfSSL TLS backend, HTTP-only); scripts/release/package.sh → Release build, ctest -L unit 46/46 on the static build, ldd = libm/libc only, shared lib exports 0 non-ehem symbols, archive hem-tool-1.0.0+ga285ec7-linux-x86_64.tar.gz (2.4 MB binary + LICENSES/ + README.txt) + SHA256SUMS; scripts/release/check-expired-classifier.sh with the STATIC binary → 'diagnosis: the device certificate has EXPIRED (… server verification failed: certificate has expired.)' = the REQ-NET-005 classifier proven under the wolfSSL backend, no cloud traffic (EHEM_CHECKIN_URL hook)"
  notes: >
    2026-10-09. Written: .github/workflows/release.yml (push to main +
    workflow_dispatch → artifacts; v* tags → GitHub Release with the two
    archives + one SHA256SUMS, re-verified before publishing; tag == project
    version enforced by package.sh); scripts/release/build-deps-static.sh
    (pinned wolfSSL 5.7.2 from the GitHub tag archive + curl 8.10.1, BOTH via
    CMake — no autotools anywhere; -fPIC; Linux CA bundle+path compiled in,
    Windows relies on CURLSSLOPT_NATIVE_CA; cache-stamped),
    scripts/release/package.sh (Release build against the prefix,
    CURL_USE_STATIC_LIBS, CMAKE_C_STANDARD_LIBRARIES=-lm so the static
    libwolfssl's pow/log resolve, -static on Windows; unit suite; static
    assertion via ldd / objdump; licenses + README into the archive;
    SHA256SUMS), scripts/release/make-expired-cert.py (valid CA + EXPIRED
    leaf — wolfSSL refuses to LOAD an expired cert as a trust anchor, so a
    self-signed expired cert cannot be its own CA), scripts/release/
    check-expired-classifier.sh (openssl s_server + `hem-tool recovery` →
    the EXPIRED diagnosis line is the proof). SDK/tool changes the dry run
    forced: transport_curl.c classifier also matches wolfSSL's
    ASN_AFTER_DATE_E (-151 in 5.6.6) / its text and CURLE_SSL_CONNECT_ERROR;
    CURLSSLOPT_NATIVE_CA on _WIN32; crypto_shim.c date formatter clamped
    (-O3 -Wformat-truncation fired — Debug builds never saw it); CMake
    -Wl,--exclude-libs,ALL on the shared library (REQ-API-006: with static
    deps every curl/wolfSSL symbol was exported — the export gate caught
    it); main.c EHEM_CHECKIN_URL test hook; wolfSSL CMake needs
    -DCMAKE_C_FLAGS=-Wno-error on GCC 13. README "Download hem-tool"
    section. Regular ./dev ci 46/46 gcc+clang + ./dev check green after all
    of it. NOT DONE (needs a push / attended / a decision): the workflow run
    itself on GitHub (Linux + Windows jobs, artifacts), the Windows leg, the
    tag path, the licensing decision (REQ-BUILD-005 / §12 risk 1).
reopened: []
cancelled: null
---

**Goal:** `.github/workflows/release.yml` (REQ-BUILD-005): two jobs —
`linux` (oldest available Ubuntu LTS runner) and `windows` (MSYS2
MINGW64) — each building **pinned** wolfSSL and libcurl from source
(static, libcurl `--with-wolfssl`, HTTP/HTTPS only), then the project
with `-DCMAKE_BUILD_TYPE=Release` against that prefix, running
`ctest -L unit`, asserting the static dependency check, packaging
`hem-tool-<version>[+g<sha>]-<os>-x86_64.{tar.gz,zip}` with README and
license notices, uploading artifacts on `push: main` /
`workflow_dispatch`, and on `v*` tags publishing a GitHub Release with the
archives + `SHA256SUMS` (tag == project version enforced).

**Notes:**
- Pin wolfSSL and curl to explicit release tags in the workflow; cache
  the built prefix keyed on those versions (actions/cache) so the
  Windows autotools build does not repeat every run.
- wolfSSL configure flags must cover the shim's needs — the unit suite is
  the check (test_crypto: X25519, HMAC, PBKDF2, ECC verify, cert parsing,
  AES key wrap; SHA-512 for the BIP39 helper once STEP-M10-020 lands).
  Add `--enable-curl`-style options the pinned libcurl wants (SNI, ALPN,
  OCSP off). Expect config differences from MSYS2/distro wolfSSL (the
  KNOWN-ISSUES / memory notes on HAVE_COMP_KEY etc.) — fine, the tests
  decide.
- CMake pickup: `CMAKE_PREFIX_PATH` + `PKG_CONFIG_PATH` to the staged
  prefix; static libcurl needs `CURL_STATICLIB` (CURL::libcurl from a
  static-only install carries it) and the wolfSSL link order.
- **REQ-NET-005 classifier (REQ-BUILD-005 open criterion):** extend
  `transport_curl.c` to recognise wolfSSL's expired-cert verdict
  (`ASN_AFTER_DATE_E` -150 via CURLINFO_SSL_VERIFYRESULT and/or its
  text); add a job step that starts a local HTTPS endpoint with an
  expired self-signed certificate (`openssl req -x509 … -not_after` or
  `faketime`) and asserts the binary reports the expired classification
  (a tiny `hem-tool status` run → the REQ-NET-005 notice text). Unit-pin
  the text matcher if it is factored out.
- Trust store: Linux — `--with-ca-bundle`/`--with-ca-path` fallbacks for
  Debian/Ubuntu, Fedora/RHEL, Arch, SUSE paths; Windows — check whether
  the pinned curl's wolfSSL backend supports the native CA store
  (`CURLSSLOPT_NATIVE_CA`); else ship Mozilla's `cacert.pem` in the zip
  and default `EHEM_TLS_SYSTEM` to it when present next to the exe
  (tool-side, documented).
- Licensing (open criterion): ask the user before the first `v*` tag;
  until then main-branch artifacts only. Bundle `LICENSE` (MIT), curl's
  COPYING, wolfSSL's COPYING (GPLv3) or the commercial notice.
- `README.md`: Download section. ARCHITECTURE §12 risk 1 gets the
  decision when made.

**Definition of done**
- [ ] `release.yml` green on a push to main: both archives uploaded as
      artifacts, static checks asserted, unit suite green on the Release
      build on both platforms.
- [x] Expired-cert classification proven with the wolfSSL-backed libcurl
      (REQ-BUILD-005 / REQ-NET-005 criterion recorded) — locally with the
      static build, by the same script the workflow runs (2026-10-09).
- [ ] Attended: both downloaded binaries run `hem-tool status` against
      the dev device under system trust (recorded in REQ-BUILD-005).
- [ ] Tag path exercised at least by a dry run (`workflow_dispatch` on a
      throwaway pre-release tag or a `-rc` tag, deleted afterwards if
      wanted) — Release created with archives + SHA256SUMS; version/tag
      mismatch fails.
- [x] README Download section; `./dev ci` still green; `implements:
      REQ-BUILD-005` tag in the workflow header (and in the scripts).
