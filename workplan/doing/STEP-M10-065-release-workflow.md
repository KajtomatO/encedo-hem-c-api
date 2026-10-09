---
id: STEP-M10-065
title: "Release workflow: hem-tool (dynamic wolfSSL, static libcurl) for Linux + Windows + the wolfSSL companion asset; artifacts on main, GitHub Release on tags"
milestone: M10
implements: ["REQ-BUILD-005"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: []
evidence:
  commits: []   # the user commits (never-commit rule); SHAs to be backfilled
  tests:
    - "LOCAL DRY RUN 2026-10-09, rev 2 (Linux leg, the exact scripts the workflow calls): scripts/release/build-deps.sh → wolfSSL 5.7.2 SHARED (SONAME libwolfssl.so.42, NEEDED libm/libc; tarball sha256 verified) + curl 8.10.1 static (wolfSSL TLS backend, HTTP-only; CURLTargets.cmake links the absolute libwolfssl.so path), stamp wolfssl-5.7.2-shared+curl-8.10.1-static, second run short-circuits; scripts/release/package.sh → Release build, ctest -L unit 46/46 (LD_LIBRARY_PATH to the prefix), RUNPATH [$ORIGIN] asserted, ldd resolves libwolfssl.so.42 from build-release/ beside the binary (NOT the host's system copy), no curl/ssl/crypto, hem-tool archive hem-tool-1.0.0+ge008521-linux-x86_64.tar.gz (binary + README.txt + LICENSES/ incl. LICENSE.qrcodegen, no wolfSSL text) + scripts/release/package-wolfssl.sh → wolfssl-5.7.2-linux-x86_64.tar.gz (libwolfssl.so.42 real file + COPYING GPLv2 + BUILD-CONFIG.txt + README.txt) + wolfssl-5.7.2-stable-src.tar.gz (hash == the pin), drop-in smoke OK (negative test skipped: this host has a system libwolfssl.so.42 — the CI runner has none), SHA256SUMS 3 lines, sha256sum -c OK; scripts/release/check-expired-classifier.sh with the DYNAMIC binary → 'diagnosis: the device certificate has EXPIRED (… server verification failed: certificate has expired.)' = REQ-NET-005 classifier under the shared wolfSSL backend; ./dev ci 46/46 gcc+clang + ./dev check 2/2 green. SYSTEM TRUST: both archives unpacked as a user would, libwolfssl.so.42 copied beside hem-tool, `env -i ./hem-tool status` (default URL, no --cacert/--insecure) → device data (fw v1.2.2-DIAG), exit 0"
    - "SUPERSEDED (design changed 2026-10-09 — wolfSSL must not be bundled statically): LOCAL DRY RUN 2026-10-09 (Linux leg, the exact scripts the workflow called): scripts/release/build-deps-static.sh → wolfSSL 5.7.2 + curl 8.10.1 static (CMake, wolfSSL TLS backend, HTTP-only); scripts/release/package.sh → Release build, ctest -L unit 46/46 on the static build, ldd = libm/libc only, shared lib exports 0 non-ehem symbols, archive hem-tool-1.0.0+ga285ec7-linux-x86_64.tar.gz (2.4 MB binary + LICENSES/ + README.txt) + SHA256SUMS; scripts/release/check-expired-classifier.sh with the STATIC binary → 'diagnosis: the device certificate has EXPIRED (… server verification failed: certificate has expired.)' = the REQ-NET-005 classifier proven under the wolfSSL backend, no cloud traffic (EHEM_CHECKIN_URL hook)"
  notes: >
    2026-10-09 (rev 1, static). Written: .github/workflows/release.yml (push
    to main + workflow_dispatch → artifacts; v* tags → GitHub Release with
    the archives + one SHA256SUMS, re-verified before publishing; tag ==
    project version enforced by package.sh); scripts/release/
    build-deps-static.sh (pinned wolfSSL 5.7.2 from the GitHub tag archive +
    curl 8.10.1, BOTH via CMake — no autotools anywhere; Linux CA bundle+path
    compiled in, Windows relies on CURLSSLOPT_NATIVE_CA; cache-stamped),
    scripts/release/package.sh, scripts/release/make-expired-cert.py (valid
    CA + EXPIRED leaf — wolfSSL refuses to LOAD an expired cert as a trust
    anchor, so a self-signed expired cert cannot be its own CA),
    scripts/release/check-expired-classifier.sh (openssl s_server +
    `hem-tool recovery` → the EXPIRED diagnosis line is the proof). SDK/tool
    changes the dry run forced: transport_curl.c classifier also matches
    wolfSSL's ASN_AFTER_DATE_E (-151 in 5.6.6) / its text and
    CURLE_SSL_CONNECT_ERROR; CURLSSLOPT_NATIVE_CA on _WIN32; crypto_shim.c
    date formatter clamped (-O3 -Wformat-truncation fired — Debug builds
    never saw it); CMake -Wl,--exclude-libs,ALL on the shared library
    (REQ-API-006: with static deps every curl/wolfSSL symbol was exported —
    the export gate caught it); main.c EHEM_CHECKIN_URL test hook; wolfSSL
    CMake needs -DCMAKE_C_FLAGS=-Wno-error on GCC 13. README "Download
    hem-tool" section. Regular ./dev ci 46/46 gcc+clang + ./dev check green.

    2026-10-09 (rev 2, dynamic wolfSSL — user decision the same day: no
    published package may bundle wolfSSL statically; the shared wolfSSL is
    published as a SEPARATE release asset; libcurl stays pinned static;
    REQ-BUILD-005 rev 2 → draft, re-approval pending). Reworked:
    build-deps-static.sh → build-deps.sh (wolfSSL -DBUILD_SHARED_LIBS=ON with
    the same feature flags so the installed options.h matches the shipped
    library; tarballs pinned by sha256 and verified on every run; stale
    prefix wiped on a stamp mismatch; the exact configure line and the source
    tarball kept in the prefix; Windows DLL linked -static so it imports no
    libgcc/winpthread — asserted); package.sh (Linux
    CMAKE_BUILD_WITH_INSTALL_RPATH + INSTALL_RPATH=$ORIGIN so the binary
    carries only $ORIGIN; the -lm workaround dropped — a shared libwolfssl
    NEEDs libm itself; Windows keeps -static for the exe: it only restricts
    -l searches and CMake passes the absolute libwolfssl.dll.a path, so the
    exe still imports libwolfssl.dll; libwolfssl.so.42 / .dll copied beside
    build-release/hem-tool = the deployment layout; the link-mode assertion
    inverted — wolfSSL MUST be dynamic and resolved from $ORIGIN, nothing
    else dynamic; qrcodegen's MIT header extracted into LICENSES/ and the
    silent `|| true` gap closed; README.txt rewritten; NO COPYING.wolfssl in
    the hem-tool archive; drop-in smoke test of both archives with env -i,
    Linux negative test guarded by ldconfig -p; SHA256SUMS regenerated, not
    appended); NEW package-wolfssl.sh (wolfssl-<v>-<os>-x86_64 archive =
    real-file library + COPYING + BUILD-CONFIG.txt + README with the source
    sha256; Linux also copies wolfssl-<v>-stable-src.tar.gz into dist/);
    release.yml (cache keyed on hashFiles of build-deps.sh, no env pins;
    artifacts release-<os>-x86_64; the release job attaches 2 hem-tool
    archives + 2 wolfSSL archives + the source tarball + one SHA256SUMS with a
    duplicate-name check). wolfSSL 5.7.2's COPYING is GPLv2 ("or any later
    version" per LICENSING) — every "GPLv3" in the docs corrected. NOT DONE
    (needs a push / attended): the workflow run itself on GitHub (Linux +
    Windows jobs, artifacts), the Windows leg, the tag path, the attended
    system-trust run with the dynamic binary, REQ-BUILD-005 rev 2
    re-approval before any v* tag.
reopened: []
cancelled: null
---

**Goal:** `.github/workflows/release.yml` (REQ-BUILD-005 rev 2): two jobs —
`linux` (oldest available Ubuntu LTS runner) and `windows` (MSYS2
MINGW64) — each building **pinned** wolfSSL as a **shared** library and
**pinned** libcurl as a static one (wolfSSL TLS backend, HTTP/HTTPS only)
from sha256-verified sources, then the project with
`-DCMAKE_BUILD_TYPE=Release` against that prefix, running `ctest -L unit`,
asserting the link mode (wolfSSL dynamic and resolved from the binary's
own directory, nothing else dynamic), packaging
`hem-tool-<version>[+g<sha>]-<os>-x86_64.{tar.gz,zip}` (README + the
notices of what is compiled in — no wolfSSL inside) and the companion
asset `wolfssl-<wv>-<os>-x86_64.{tar.gz,zip}` (library + COPYING +
BUILD-CONFIG + README; Linux also the upstream source tarball),
smoke-testing the two side by side, uploading artifacts on `push: main` /
`workflow_dispatch`, and on `v*` tags publishing a GitHub Release with all
archives + `SHA256SUMS` (tag == project version enforced).

**Notes:**
- Pins (version + sha256) live in `scripts/release/build-deps.sh` only; the
  workflow caches the built prefix keyed on that script's hash, so any
  recipe change rebuilds. Before bumping a pin, re-read the new tarball's
  `COPYING`/`LICENSING` (5.7.2 = GPLv2 or later).
- wolfSSL configure flags must cover the shim's needs — the unit suite is
  the check (test_crypto: X25519, HMAC, PBKDF2, ECC verify, cert parsing,
  AES key wrap; SHA-512 for the BIP39 helper). `WOLFSSL_CURL` covers what
  the pinned libcurl wants (SNI, ALPN, OpenSSL-compat). The SAME flags
  produce the headers hem-tool compiles against and the library the user
  drops next to it (KNOWN-ISSUES: struct layouts follow `options.h`).
- CMake pickup: `CMAKE_PREFIX_PATH` + `PKG_CONFIG_PATH` to the staged
  prefix; `CURL_USE_STATIC_LIBS`; wolfSSL comes in as the absolute path of
  `libwolfssl.so` / `libwolfssl.dll.a` from pkg-config and from curl's
  exported target, so the final link imports it even under Windows'
  `-static` (which only restricts `-l` searches). Linux RUNPATH is exactly
  `$ORIGIN` (`CMAKE_BUILD_WITH_INSTALL_RPATH` + `CMAKE_INSTALL_RPATH`), so
  the unit tests need `LD_LIBRARY_PATH` for the ctest run; `hem-tool`, the
  `ldd` assertion and the classifier script use the library copied beside
  the binary.
- **REQ-NET-005 classifier (REQ-BUILD-005 criterion, RESOLVED):**
  `transport_curl.c` recognises wolfSSL's expired-cert verdict
  (`ASN_AFTER_DATE_E` via CURLINFO_SSL_VERIFYRESULT and its text);
  `check-expired-classifier.sh` serves a valid local CA + an EXPIRED leaf
  with `openssl s_server` and asserts `hem-tool recovery` prints the
  EXPIRED diagnosis. The link mode does not change the verdict.
- Trust store: Linux — CA bundle + hashed directory compiled into libcurl;
  Windows — `CURLSSLOPT_NATIVE_CA` (set by the SDK transport on `_WIN32`).
- Licensing: DECIDED 2026-10-09 (user) — wolfSSL is never bundled
  statically; the hem-tool archive carries `LICENSE` (MIT), curl's COPYING,
  cJSON, the BIP39 wordlist and qrcodegen notices; wolfSSL's COPYING
  (GPLv2 or later), its build configuration and the corresponding source
  travel with the companion asset. ARCHITECTURE §12 risk 1 records it.
- `README.md`: Download section describes the archive + companion-asset
  layout.

**Definition of done**
- [ ] `release.yml` green on a push to main: hem-tool archives + wolfSSL
      companion assets (+ the source tarball) uploaded as artifacts, the
      link-mode assertion (wolfSSL dynamic, libcurl static, the Windows
      DLL runtime-free) and the drop-in smoke test passed, unit suite green
      on the Release build on both platforms.
- [x] Expired-cert classification proven with the wolfSSL-backed libcurl
      (REQ-BUILD-005 / REQ-NET-005 criterion recorded) — locally, by the
      same script the workflow runs (2026-10-09, static build; the shared
      build re-runs it in every Linux job).
- [ ] Attended: both downloaded binaries, each with its companion library
      beside it, run `hem-tool status` against the dev device under system
      trust (recorded in REQ-BUILD-005).
- [ ] Tag path exercised at least by a dry run (`workflow_dispatch` on a
      throwaway pre-release tag or a `-rc` tag, deleted afterwards if
      wanted) — Release created with all archives + SHA256SUMS; version/tag
      mismatch fails. Only after REQ-BUILD-005 rev 2 is re-approved.
- [ ] README Download section (archive + companion asset); `./dev ci` still
      green; `implements: REQ-BUILD-005` tag in the workflow header and in
      all three scripts.
