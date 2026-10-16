---
id: STEP-M12-120
title: "Release matrix: -static hem-tool and -static SDK library package on OpenSSL (both platforms), no-wolfSSL assertions, classifier under OpenSSL-libcurl, release.yml matrix, README download table, RELEASING.md"
milestone: M12
implements: ["REQ-BUILD-009", "REQ-TOOL-024"]
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M12-090", "STEP-M12-100", "STEP-M12-110"]
evidence:
  commits: ["f90e7a9", "9548860", "54ae1bf", "0483532"]   # start; the approved §6.2 decisions; the implementation; the local evidence — the done commit follows
  tests:
    - "tests/unit/test_tool_startup.c (verifies: REQ-TOOL-024 rev 5): test_static_refusal_message — hem_tool_print_refusal() fed the build information of other builds: OpenSSL linked in → the remedy names the OpenSSL configuration (OPENSSL_CONF, openssl.cnf), neither the companion asset nor the -static hem-tool; wolfSSL linked in → 'this hem-tool build is broken'; the same facts linked dynamically → the replace-the-library remedies. test_version_refusal_message now takes the remedy by this build's link (the release -static build runs this suite). Local 2026-10-15: ./dev ci 56/56 gcc + clang, ./dev ci --openssl 56/56 gcc + clang, ASan 56/56; GCC 16.2 replay of every compile command (142, both backends; -fsyntax-only and -O2): 0 diagnostics"
    - "build-deps.sh openssl, local Linux 2026-10-15 (recipe r2): 79 s; the script's own check — openssl version -d -e -m = /etc/ssl, /usr/lib/engines-3, /usr/lib/ossl-modules (make variables; before: the prefix's own lib/engines-3 and lib/ossl-modules); no engines-3/ossl-modules directory in the prefix"
    - "package.sh <deps-openssl> linux dist static, local 2026-10-15 (package-static.sh + package-lib.sh static), all green: unit 55/55 on the Release build (OpenSSL, EHEM_DEPS_LINK=static, static SDK only) — its consumer_interface gate linked consumer_pc_static from the all-static `pkg-config --static` closure of the scratch install (REQ-BUILD-008 criterion 4, locally); hem-tool: ldd = linux-vdso, libc.so.6, ld-linux-x86-64.so.2, no RUNPATH, no wolfSSL_/wolfCrypt_/wc_ symbol or string; --version 'crypto: OpenSSL 3.5.9 (compiled) / 3.5.9 (loaded), static', 'transport: libcurl 8.10.1 (compiled) / 8.10.1 (loaded), static'; the archive alone, unpacked: help and keys types run, status against a closed port exits 1 (initialised, then unreachable), and with OPENSSL_CONF = a file activating only the null provider exits 12 with 'remedy: OpenSSL is linked into hem-tool, so the OpenSSL configuration it read ... ' (the Linux control). encedo-hem-...-static: libencedo-hem.a, libcurl.a, libssl.a, libcrypto.a (no wolfSSL symbol), include/{ehem,curl,openssl}, lib/cmake/{encedo-hem,CURL}, five .pc files rewritten to ${pcfiledir} (no build-prefix path left — checked), notices incl. LICENSE.openssl; the gate in package mode: consumer_cmake_static and consumer_pc_static built ('pkg-config --static closure') and run, ldd libc only. check-expired-classifier.sh on build-release-static/hem-tool: the expired leaf recognised ('certificate has expired'), the not-yet-valid one refused and not classified"
    - "package.sh dynamic against the M10 wolfSSL prefix, then static into the same dist (as the job runs them), local 2026-10-15: dynamic 56/56, drop-in OK, library package smoke OK (the restructured package-lib.sh); SHA256SUMS over all six Linux archives, sha256sum -c OK"
    - "Cross replay 2026-10-15, Windows (Debian x86_64-w64-mingw32 GCC 13, msvcrt; uname shim): build-deps.sh openssl — './Configure mingw64 no-autoload-config ...', configuration.h defines OPENSSL_NO_AUTOLOAD_CONFIG, curl_config.h '#define CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG 1', libcrypto.a carries 'C:/Program Files/Common Files/SSL' (+ /engines-3, /ossl-modules, ct_log_list.cnf) and no build-prefix path; the make variables with spaces reach the compiler intact (and MSYS2's converter leaves a -D...=\"C:/...\" value alone: SIMPLE_WINDOWS_PATH, msys2_path_conv.cc); only running bin/openssl is impossible here"
    - "Cross replay 2026-10-15, Windows package-static.sh (shims: objdump/nm/strings → MinGW binutils, cygpath, zip/unzip via Python; CMAKE_TOOLCHAIN_FILE in the environment, BUILD_TESTING off): FOUND — hem-tool.exe failed to link (libcrypto.a: WspiapiGetAddrInfo, CertOpenStore ... unresolved): curl's package config looks up OpenSSL inside find_package(CURL), before CMakeLists.txt set OPENSSL_USE_STATIC_LIBS, so FindOpenSSL's targets lacked ws2_32 + crypt32 (Linux unaffected: FindOpenSSL adds threads + dl for any .a). FIXED in 54ae1bf (the switch set with EHEM_DEPS_LINK=static, before the curl lookup). Then: hem-tool.exe imports advapi32, bcrypt, crypt32, kernel32, user32, ws2_32 + msvcrt.dll (the cross CRT — the script's allowlist rightly rejects it; UCRT64 links the Universal CRT); no wolfSSL symbol or string; compiled-in directories the admin-only ones. package-lib.sh static: staged, .pc rewritten, archives without wolfSSL, zipped; its gate configured and built both static consumers ('pkg-config --static closure'), importing system DLLs only (+ msvcrt). Not provable here: running anything — the dispatch run is the proof"
    - "CI run 38892261894 (push to development, 0483532), 2026-10-16: all 10 legs green — the new test_static_refusal_message among the unit tests on every leg (Linux gcc/clang on both backends, the switch legs, the four Windows legs)"
    - "Release run 38894733780 (workflow_dispatch on development, 0483532), 2026-10-16: both jobs green, every prefix built cold (the build-deps.sh hash changed). Linux 3:21 — deps 0:38, deps-openssl 1:40, dynamic package 0:13, wolfSSL classifier 0:02, -static package 0:19, -static classifier 0:01. Windows 14:24 — MSYS2 2:17, deps 1:52, deps-openssl 8:19, dynamic package 0:38, -static package 0:49. Whole run 14.5 min. The publishing job was skipped (no tag). No annotations on either job. Artifacts: release-linux-x86_64 32,963,535 bytes (25,129,032 at run 38719457329), release-windows-x86_64 9,390,803 (1,706,552) — the four -static archives. The job logs need a signed-in reader; each check below fails its step, so the step conclusions are the record: build-deps.sh openssl — openssl version -d -e -m equal to the chosen directories (Linux /etc/ssl, /usr/lib/engines-3, /usr/lib/ossl-modules; Windows C:/Program Files/Common Files/SSL and its engines-3, ossl-modules), and on Windows OPENSSL_NO_AUTOLOAD_CONFIG in configuration.h and CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG in curl_config.h; package-static.sh — the unit suite on the static OpenSSL Release build, the link mode (Linux ldd glibc family only, no RUNPATH/RPATH; Windows: system DLLs only, the Universal CRT imported), no wolfSSL symbol (nm) or string, the exact --version lines (OpenSSL 3.5.9 and libcurl 8.10.1, compiled = loaded, static), the archive alone runs help and keys types, status against a closed port exits 1, and under the null-provider OPENSSL_CONF exits 1 on Windows (not read) and 12 with the OpenSSL-configuration remedy on Linux (the control); package-lib.sh static — the package holds no shared library and no wolfSSL file or symbol, its .pc and CMake files no build path, and the REQ-BUILD-008 gate in package mode built and ran consumer_cmake_static and consumer_pc_static ('pkg-config --static closure' required) against the unpacked package, each linking no SDK, curl or crypto shared library; check-expired-classifier.sh on build-release-static/hem-tool (Linux). The dynamic step is unchanged and green beside them (REQ-BUILD-005, REQ-TOOL-026 negative smoke)"
    - "curl's configure-time OpenSSL checks on MinGW (the 'Open at start' note): of curl 8.10.1's seven openssl_check_symbol_exists results only HAVE_SSL_SET0_WBIO and HAVE_OPENSSL_SRP differ from Linux (cross replay, both caches compared). Accepted: without SSL_set0_wbio curl calls SSL_set_bio(ssl, bio, bio) — the same single BIO for both directions; TLS-SRP is never used by the SDK"
  notes: |
    Settled at start, 2026-10-15 (the step's "Open at start" item). Today's
    Windows build compiles in OPENSSLDIR = <prefix>/ssl, the runner's
    D:/a/... path. curl initialises OpenSSL with OPENSSL_INIT_LOAD_CONFIG,
    and libcrypto loads the configuration at its first provider use
    (crypto/provider_core.c), so the -static binary would read
    openssl.cnf from there. The user chose "No config at all" for
    Windows, over an administrator-only directory and the OSSL_WINCTX
    registry lookup. §6.2 approved in chat: REQ-BUILD-009 rev 4 (the
    bullet plus a criterion: the Windows binary ignores a null-provider
    OPENSSL_CONF, the Linux one refuses it as the control). The
    compiled-in directories also move under the admin-only
    C:/Program Files/Common Files/SSL, since an explicit config request
    or curl's CA fallback would read them. Linux keeps
    --openssldir=/etc/ssl.
    Found while checking the control locally: a refused OpenSSL hem-tool
    advises "install the OpenSSL <v> library ... or use the -static
    hem-tool", which is wrong for the -static binary. §6.2 approved in
    chat: REQ-TOOL-024 rev 5 (needs-reverify) — with the crypto library
    linked in, the remedy names the OpenSSL configuration. This step
    reworks it, and REQ-TOOL-024 joins its implements.
    Shape (2026-10-15): package.sh's `static` variant hands over to the new
    package-static.sh (the hem-tool) and package-lib.sh `static` (the
    library package), so the dynamic path — REQ-BUILD-005's scripts and
    assertions — stays as it was (package-lib.sh's shared branch is the 110
    code, re-proven locally). Jobs: each job builds both prefixes and both
    variants, one after the other (a 2×2 matrix would rebuild the shared
    caches per leg; the static steps add one Release build each). Linux keeps
    its distribution configuration, but its compiled-in engines and modules
    directories also leave <prefix> (root-owned /usr/lib/...): a provider
    named in a root-owned openssl.cnf could otherwise be looked up under
    the runner's /home/runner/work/... tree.
    Asset count (2026-10-16): the body's "10 + SHA256SUMS" misses the
    wolfSSL source tarball — a release carries 11 archives (Linux 6,
    Windows 5) + SHA256SUMS = 12 assets, as docs/RELEASING.md says;
    STEP-M12-130 records the published count.
reopened: []
cancelled: null
---

**Goal:** the release workflow builds, checks and publishes
`hem-tool-<label>-<os>-x86_64-static` (one file, OpenSSL backend, every
third-party library static) and `encedo-hem-<label>-<os>-x86_64-static`
(static SDK + `libcurl.a`, `libssl.a`, `libcrypto.a`, config + `.pc`,
OpenSSL notice) for Linux and Windows beside the unchanged M10 assets;
the job asserts the link mode (Linux: `ldd` = glibc family only, no
RUNPATH; Windows: system DLLs only), **no wolfSSL symbol or string in any
`-static` asset**, runs the unit suite on the Release build, the
REQ-NET-005 classifier under the OpenSSL-backed libcurl
(`check-expired-classifier.sh` on the static binary), `hem-tool --version`
showing `OpenSSL <pinned>`, and the consumer smoke for the static library
package; README "Download" gets the asset table and "which one do I
want"; `docs/RELEASING.md` the new variants and the attended per-variant
smoke.

**Notes:** `package.sh` gains a variant argument (`dynamic` = today /
`static`): configure `EHEM_CRYPTO_BACKEND=openssl EHEM_DEPS_LINK=static
EHEM_TOOL_LINK=static` against `deps-openssl` (STEP-M12-110). Linux link:
third-party archives + `-static-libgcc`, glibc dynamic — no `-static`
(a static glibc breaks `getaddrinfo`/NSS; settled at the decomposition);
Windows: `-static` as today (`crypt32`, `ws2_32`, `bcrypt` … are system
DLLs). Trust store: Linux — OpenSSL's `OPENSSLDIR` default plus libcurl's
distro CA-bundle fallbacks as REQ-BUILD-005; Windows —
`CURLSSLOPT_NATIVE_CA` (the transport sets it on `_WIN32`; libcurl ≥ 7.71
honours it with OpenSSL). Assertions in `package.sh`: `ldd`/`readelf`,
`objdump -p`, plus `nm`/`strings` for `wolfSSL_` / `wc_` → must be absent
(the §1 rule as a test). `check-expired-classifier.sh` run on the static
binary closes REQ-BUILD-007's last criterion (OpenSSL verdict 10 /
"certificate has expired" — the original path). `package-lib.sh static`:
`libencedo-hem.a` + the three OpenSSL/curl archives + config/`.pc`
pointing at them + notices incl. OpenSSL `LICENSE.txt`; consumer smoke
with `pkg-config --libs --static` must link with no unresolved symbol
(REQ-BUILD-008 criterion 4). `release.yml`: the two jobs each build both
prefixes and all variants, or a 2×2 matrix — pick by runner time; the
publishing job keeps its shape (assemble, verify `SHA256SUMS`, duplicate
names, publish), asset count grows from 6 to 10 + SHA256SUMS (no OpenSSL
source tarball — Apache-2.0 needs none). `hem-tool --version` in the job
log must say `OpenSSL 3.5.x (compiled) / 3.5.x (loaded), static`. README
"Download": table of the five asset kinds + guidance (dynamic + companion
= default; `-static` = no companion, OpenSSL; library packages for
consumers); `docs/RELEASING.md`: variants, prefixes, attended smoke list
per variant. Prove via `workflow_dispatch` on the branch; the
`v1.2.0-rc<N>` tag path is STEP-M12-130's. Gotcha: never read link
results off the dev host (system wolfSSL masks); the MinGW printf rule
for anything new in the tool.

**Open at start (found at STEP-M12-110, 2026-10-15): OPENSSLDIR on
Windows.** OpenSSL compiles its configuration directory in as
`OPENSSLDIR` (unix-Makefile.tmpl: `<prefix>/ssl` without `--openssldir`),
and `build-deps.sh` passes none on Windows — so the `-static` binary would
look for `openssl.cnf` under the runner's `D:/a/…/deps-openssl/ssl`, a path
an unprivileged local user may be able to create on another machine; an
`openssl.cnf` can load provider DLLs (the CVE-2019-1552 class). Settle it
here: an administrator-only `--openssldir` (the `CommonProgramFiles\SSL`
the MSVC builds default to, or `-DOSSL_WINCTX` registry lookup) and/or no
configuration loading at all, decided with the user if it shapes the
asset; prove what the binary reads (`openssl version -d` from the same
prefix, and the `-static` hem-tool behaviour with no such directory).
Linux: `--openssldir=/etc/ssl` reads the distribution's `openssl.cnf`,
written for its own OpenSSL — check the static binary when that file names
modules it lacks.
*Settled 2026-10-15 (user decision; §6.2 approved — REQ-BUILD-009 rev 4):
Windows loads no configuration at all, and the compiled-in directories lie
under `C:\Program Files\Common Files\SSL`; Linux reads `/etc/ssl` — see
the notes.*
Also found (cross replay of the Windows `deps-openssl` build, 2026-10-15):
curl 8.10.1's configure-time OpenSSL checks fail on MinGW against the
static OpenSSL — its `openssl_check_symbol_exists` links
`${OPENSSL_LIBRARIES}` + `ws2_32` + `bcrypt`, but `libcrypto.a` also needs
`crypt32` (CAPI engine, `winstore`), which FindOpenSSL adds to its imported
targets only. Effect: `HAVE_SSL_SET0_WBIO` off (curl uses the equivalent
`SSL_set_bio`) and SRP off (unused); Linux finds both. Fix it in
`build-deps.sh` or record it as accepted here.

**Definition of done**
- [x] `package.sh static` + `package-lib.sh static` produce the two
      `-static` assets on both platforms from `deps-openssl`; notices
      include OpenSSL's Apache-2.0 text.
      (Release run 38894733780, both jobs: the -static step green on Linux
      and Windows; LICENSE.openssl in both archives — seen in the local
      Linux run, packaged by the same script lines on Windows.)
- [x] Job assertions green: Linux `ldd` glibc only + no RUNPATH; Windows
      import table system DLLs only; `nm`/`strings` show no `wolfSSL_`/`wc_`
      in any `-static` asset; unit suite on the Release OpenSSL build both
      platforms; `hem-tool --version` reports the pinned OpenSSL compiled
      and loaded, `static` (run id recorded in REQ-BUILD-009).
      (Run 38894733780: package-static.sh fails on each of these; green on
      both platforms. Recorded in REQ-BUILD-009 rev 5 and REQ-TOOL-025 rev 4.)
- [x] `check-expired-classifier.sh` passes on the static Linux binary
      (REQ-BUILD-007 last criterion checked).
      (Run 38894733780, Linux step "Prove the expired-certificate
      classifier (-static, OpenSSL backend)" green. REQ-BUILD-007 had
      already handed this criterion to REQ-BUILD-009 at its rev 4 — every
      REQ-BUILD-007 box was checked then — so it is recorded there.)
- [x] Consumer smoke for the static library package (find_package +
      pkg-config `--static`) links and runs in the job (REQ-BUILD-008
      criterion 4 checked).
      (Run 38894733780, both platforms: package-lib.sh static's gate built
      and ran both static consumers from the unpacked package, the
      pkg-config one from the `--static` closure. REQ-BUILD-008 rev 7.)
- [x] `release.yml` builds every asset of the REQ-BUILD-009 table on a
      `workflow_dispatch` run; `SHA256SUMS` covers all; publishing job
      unchanged in shape and within the runner budget (times recorded).
      (Run 38894733780: Linux 6 archives, Windows 5, each job's
      SHA256SUMS written over every archive in its dist/ after both
      variants (`write_sums`) and uploaded with them; the publishing job
      is untouched — it concatenates, checks names and `sha256sum -c` on a
      tag (STEP-M12-130). Times: Linux 3:21, Windows 14:24 with every cache
      cold, of which the -static steps add 0:20 and 0:49; the repository is
      public, so the hosted runners carry no minute limit.)
- [x] README "Download" asset table + guidance; `docs/RELEASING.md`
      variants, prefixes and attended per-variant smoke; `implements:
      REQ-BUILD-009` tags; `./dev ci` green.
      (54ae1bf: README "Download" with seven asset rows and "Which one do I
      want?"; RELEASING "Variants", the rc check of twelve assets, the
      per-variant attended smoke, and the OpenSSL build choices to keep
      on a bump; package-static.sh carries the tag; ./dev ci 56/56 on both
      backends, 2026-10-15.)
- [x] OpenSSL configuration (REQ-BUILD-009 rev 4): the Windows OpenSSL is
      built with `no-autoload-config` and curl with
      `CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG` (asserted in `build-deps.sh`);
      `openssl version -d -e -m` shows the admin-only directories; the
      Windows static hem-tool runs under a null-provider `OPENSSL_CONF`, and
      the Linux one refuses it with exit 12 (job log).
      (Run 38894733780: build-deps.sh's directory and define checks in the
      cold deps-openssl builds, package-static.sh's null-provider runs —
      Windows exit 1, Linux exit 12 with the remedy; both green.)
- [x] REQ-TOOL-024 rev 5: the static-link remedy in `startup.c` with its
      unit test; REQ-TOOL-024 needs-reverify → verified.
      (test_static_refusal_message green on all 10 CI legs, run
      38892261894, and in both release jobs' static unit suite; the Linux
      job's null-provider run checks the printed remedy. REQ-TOOL-024 rev 6.)
