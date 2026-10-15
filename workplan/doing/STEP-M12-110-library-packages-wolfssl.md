---
id: STEP-M12-110
title: "Release: SDK library packages, wolfSSL flavour (encedo-hem-<label>-<os>-x86_64) on both platforms + the deps-openssl prefix (OpenSSL 3.5 LTS pin) in build-deps.sh"
milestone: M12
implements: ["REQ-BUILD-009", "REQ-BUILD-008"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M12-070"]
evidence:
  commits: ["b604dda"]   # start — the implementation, the dispatch run and the done commit follow
  tests: []
  notes: |
    §6.2, approved in chat 2026-10-14: the user decided "I dont want any
    distribution with static wolfssl, static distribution should only
    openssl" — so the wolfSSL library package is shared-only (REQ-BUILD-009
    rev 2: no libencedo-hem.a, no libcurl.a; the Windows libwolfssl.dll.a
    question moot), and a shared-only encedo-hem.pc names no
    Requires.private (REQ-BUILD-008 rev 5, needs-reverify until this step's
    CI runs): pkg-config resolves Requires.private even for --cflags
    (pkgconf 1.8.1: "Package libcurl was not found"), so a standalone
    shared package would fail every pkg-config consumer. This step does
    both; REQ-BUILD-008 joined its implements, STEP-M12-070 stays done.
    The OpenSSL pin, settled the same day: 3.5.9 (2026-09-29, the newest
    3.5 LTS), sha256 603f5602…eb859a computed from the official download =
    the published .sha256, PGP signature good against OpenSSL's key from
    openssl-library.org/source/pubkeys.asc; LICENSE.txt Apache-2.0 — in
    REQ-BUILD-009 rev 2.
reopened: []
cancelled: null
---

**Goal:** the release workflow produces `encedo-hem-<label>-linux-x86_64.tar.gz`
and `encedo-hem-<label>-windows-x86_64.zip` — the SDK's **shared library
only** (user decision 2026-10-14: no static library ships with wolfSSL):
headers, `libencedo-hem.so.*` / `libencedo-hem.dll` + `.dll.a` (libcurl
static inside), the CMake package config, `encedo-hem.pc`, notices, README —
running against the unchanged wolfSSL companion asset; the job builds the
REQ-BUILD-008 sample consumer against the unpacked package (find_package +
pkg-config) and runs it; `SHA256SUMS` covers the new assets. In the same
step `build-deps.sh` gains the second prefix `deps-openssl` (pinned OpenSSL
3.5.x + libcurl with OpenSSL as TLS backend) so STEP-M12-120 can start from
a cached, proven prefix, and `encedo-hem.pc` names `Requires.private` only
when the static library is installed (REQ-BUILD-008 rev 5).

**Notes:** New `scripts/release/package-lib.sh` next to `package.sh` /
`package-wolfssl.sh`: a shared-only Release build (`EHEM_BUILD_STATIC=OFF`,
no hem-tool) against the same deps prefix with `package.sh`'s link settings
(Linux: RUNPATH `$ORIGIN`, so the companion library put beside it in `lib/`
is found; Windows: no MinGW runtime DLL among the DLL's imports), `cmake
--install` into a staging tree; notices: MIT, curl, cJSON, BIP39 wordlist —
no wolfSSL text (not distributed in it). Consumer smoke in both jobs: unpack
the package + the companion asset, put the companion's library into the
package (`lib/` on Linux, `bin/` on Windows), run
`tests/consumer/check_consumer.cmake` against the unpacked package
(find_package + pkg-config, shared), then `ldd`/`objdump`: libencedo-hem and
libwolfssl resolve from the package, nothing else non-system.
`deps-openssl` prefix: OpenSSL 3.5.x (exact version + sha256 from the
official download, checked against the announcement; `LICENSE.txt`
Apache-2.0 captured for the notices; `no-shared no-tests` minimal config,
`--prefix`/`--openssldir` for the Linux trust store) and libcurl 8.10.1
built again with OpenSSL, HTTP/HTTPS only as today; CI dependency caches
keyed per prefix; `docs/RELEASING.md` "Changing the pinned dependencies"
covers both prefixes. Keep the free-runner budget in view (OpenSSL build ~5
min cold, cached afterwards). Run through `workflow_dispatch` on the branch;
nothing goes to `main` from here. Gotcha (memory): never conclude link
behaviour from the dev host — its system `libwolfssl.so.42` masks
clean-runner failures; the job log is the proof.

**Definition of done**
- [ ] `package-lib.sh` produces the wolfSSL-flavour library package (shared
      library only) on Linux and Windows with the REQ-BUILD-009 contents;
      notices without wolfSSL text; README states the companion-asset
      requirement.
- [ ] Consumer smoke in both jobs: find_package + pkg-config against the
      unpacked package + companion asset; libencedo-hem and the companion
      wolfSSL resolve from the package, nothing else non-system (job log,
      run id recorded).
- [ ] `SHA256SUMS` lists the new assets; the publishing job's
      duplicate-name check and re-verification cover them (dispatch run).
- [ ] `build-deps.sh` builds `deps-openssl` (OpenSSL 3.5.x pin + sha256 +
      libcurl with OpenSSL) on both platforms; LICENSE.txt read and the
      pin recorded in REQ-BUILD-009 (its OPEN pin criterion); CI cache
      keyed per prefix.
- [x] REQ-BUILD-009 OPEN criterion decided and recorded (rev bump).
      (Rev 2, 2026-10-14: moot for the Windows import library — the
      wolfSSL package is shared-only by the user's decision; the pin
      OpenSSL 3.5.9.)
- [ ] REQ-BUILD-008 rev 5: `encedo-hem.pc` names `Requires.private` only
      with the static library; the consumer gate asserts it (present with,
      absent without) on every CI leg; REQ-BUILD-008 re-verified.
- [ ] `docs/RELEASING.md` pins section updated; `implements: REQ-BUILD-009`
      tags in package-lib.sh / build-deps.sh / release.yml.
- [ ] REQ-BUILD-005 outputs byte-for-byte the same kind (same scripts,
      same assertions — the dynamic hem-tool archive, companion assets,
      source tarball unchanged; checked in the dispatch run).
