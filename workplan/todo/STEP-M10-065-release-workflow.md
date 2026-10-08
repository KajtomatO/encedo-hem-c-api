---
id: STEP-M10-065
title: "Release workflow: static hem-tool for Linux + Windows, artifacts on main, GitHub Release on tags"
milestone: M10
implements: ["REQ-BUILD-005"]
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
- [ ] Expired-cert classification proven in the workflow with the
      wolfSSL-backed libcurl (REQ-BUILD-005 / REQ-NET-005 criterion
      recorded).
- [ ] Attended: both downloaded binaries run `hem-tool status` against
      the dev device under system trust (recorded in REQ-BUILD-005).
- [ ] Tag path exercised at least by a dry run (`workflow_dispatch` on a
      throwaway pre-release tag or a `-rc` tag, deleted afterwards if
      wanted) — Release created with archives + SHA256SUMS; version/tag
      mismatch fails.
- [ ] README Download section; `./dev ci` still green; `implements:
      REQ-BUILD-005` tag in the workflow header.
