---
id: STEP-M12-110
title: "Release: SDK library packages, wolfSSL flavour (encedo-hem-<label>-<os>-x86_64) on both platforms + the deps-openssl prefix (OpenSSL 3.5 LTS pin) in build-deps.sh"
milestone: M12
implements: ["REQ-BUILD-009", "REQ-BUILD-008"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M12-070"]
evidence:
  commits: ["b604dda", "c343a28", "fccf8b2"]   # start; the approved §6.2 decision; the implementation — the Windows deps fix, the dispatch run and the done commit follow
  tests:
    - "encedo-hem.pc (REQ-BUILD-008 rev 5; check_consumer.cmake, verifies: REQ-BUILD-008): Requires.private only with the static library — the default and static-only installs keep 'Requires.private: libcurl wolfssl', a shared-only one carries a comment line instead; the gate asserts both ways. Local 2026-10-15: the gate green in the default, shared-only and static-only trees; ./dev ci 56/56 gcc + clang and ./dev ci --openssl 56/56 gcc + clang"
    - "build-deps.sh openssl → deps-openssl, local Linux 2026-10-15: OpenSSL 3.5.9 (./Configure linux-x86_64 --openssldir=/etc/ssl no-shared no-module no-tests --libdir=lib) + libcurl 8.10.1 on OpenSSL, both static: libssl.a, libcrypto.a (the legacy provider built in), libcurl.a (its .pc requires openssl; it references SSL_CTX_new); no .so, .dll or wolfSSL file; LICENSE.openssl = Apache-2.0; 'openssl version' 3.5.9. The first build, without no-module, installed lib/ossl-modules/legacy.so — found on inspection; the prefix check now refuses any shared object"
    - "package.sh end to end, local Linux 2026-10-15, against the M10 wolfSSL prefix (wolfSSL 5.7.2, curl 8.10.1): Release unit suite 56/56, hem-tool drop-in OK, then package-lib.sh: encedo-hem-1.1.0+gc343a28-linux-x86_64.tar.gz = include/ehem/*, lib/libencedo-hem.so{,.1,.1.1.0} (RUNPATH $ORIGIN, NEEDED libwolfssl.so.42, no libcurl), lib/cmake/encedo-hem (no find_dependency), lib/pkgconfig/encedo-hem.pc (no Requires.private), LICENSES (MIT, curl, cJSON, BIP39 — no wolfSSL text), README. Smoke against the unpacked package + companion alone (PKG_CONFIG_LIBDIR = the package, no LD_LIBRARY_PATH): check_consumer.cmake package mode green — the find_package and pkg-config shared consumers build and run; ldd: libencedo-hem.so.1 and libwolfssl.so.42 from the package's lib/, glibc otherwise — on a host whose system libwolfssl.so.42 (5.6.6) would mask a wrong lookup. SHA256SUMS lists the four archives and verifies"
    - "Not provable locally: the Windows package (MinGW, the DLL folded with -static, its import table) and the deps-openssl build in MSYS2 — the release dispatch run is the proof"
    - "CI run 38705546185 (push, fccf8b2, 2026-10-15): all 10 legs green — the consumer gate asserts Requires.private present in the default and static-only trees and absent in the shared-only ones, Linux and Windows (REQ-BUILD-008 rev 5)"
    - "Release run 38705623978 (workflow_dispatch on development, fccf8b2, 2026-10-15): Linux job green — deps-openssl built cold (99 s: OpenSSL 3.5.9 + libcurl on OpenSSL), the package step (13 s) ran package-lib.sh with its smoke, the artifact grew by the library package (24,720,005 → 25,129,487 bytes). Windows job FAILED in 'Build shared wolfSSL + static libcurl', exit 1 after 38 s — the deps cache missed on build-deps.sh's new hash, and the cold rebuild hit a latent fault: the -Wl,--output-def,/d/a/… path never reached the linker in Windows form (MSYS2 converts no argument with a space before its first slash, msys2_path_conv.cc), so ld noted 'can't open output def file' and linked on (mingw-w64's crtexe.c installs a no-op invalid-parameter handler), and the gendef fallback had recovered the .def in every earlier Windows build — until MSYS2 dropped mingw-w64-tools (gendef) from the toolchain group on 2026-10-14 (MINGW-packages dad2346). Fix: the .def path in Windows form (cygpath -m), the fallback removed, the .def checked for wolfSSL_Init, a stale one removed on rebuild"
    - "Cross replay 2026-10-15 (Debian x86_64-w64-mingw32 GCC 13, msvcrt; the STEP-M12-100 cross wolfSSL prefix): package-lib.sh's Windows build — shared-only Release + -DCMAKE_SHARED_LINKER_FLAGS=-static — links libencedo-hem.dll importing libwolfssl.dll, bcrypt, kernel32, ws2_32 and the CRT only (no curl, libgcc, winpthread); no static archive, no hem-tool in the stage; the gate in package mode green, both shared consumers linked with -Wl,--disable-auto-import, importing libencedo-hem.dll + system DLLs only (not run: no Windows here)"
    - "Cross replay 2026-10-15 of build-deps.sh's openssl branch for Windows (the same script, a uname shim reporting MINGW64_NT, CROSS_COMPILE=x86_64-w64-mingw32- and the cross toolchain via CMAKE_TOOLCHAIN_FILE): ./Configure mingw64 no-shared no-module no-tests builds; the prefix holds libssl.a, libcrypto.a, libcurl.a (curl: 'Enabled SSL backends: OpenSSL v3+', libcurl.pc Requires.private: openssl, -lcrypt32 -lws2_32 -lbcrypt), bin/openssl.exe (PE32+), LICENSE.openssl — no .dll, .dll.a or wolfSSL file (the script's own check); only the last line, running bin/openssl, cannot run on Linux"
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
