---
id: REQ-BUILD-009
title: Release matrix — static hem-tool and SDK library packages (wolfSSL and OpenSSL flavours) beside the M10 hem-tool release
status: implemented
priority: should
revision: 5   # 2026-10-16 (STEP-M12-120 done): release run 38894733780 recorded — the static hem-tool, OpenSSL-configuration, library-package and REQ-BUILD-005 criteria checked; the main-push, tag-path and attended criteria stay open (STEP-M12-130/140). No meaning change. Rev 4 = 2026-10-15 (STEP-M12-120, §6.2 approved in chat): the -static assets' OpenSSL configuration — none on Windows (user decision), /etc/ssl on Linux — and its criterion. Rev 3 = 2026-10-15 (STEP-M12-110 done): the wolfSSL-flavour evidence recorded under the library-package and REQ-BUILD-005 criteria (both stay open for STEP-M12-120 / 130). No meaning change. Rev 2 = 2026-10-14 (STEP-M12-110, §6.2 approved in chat): the wolfSSL library package is shared-only (user decision); its smoke criterion follows; the OPEN criterion resolved (the Windows import library moot; the OpenSSL 3.5.9 pin)
source: user decisions 2026-10-12 (M12 planning: one milestone, one release 1.2.0; static variants ship on OpenSSL, never wolfSSL; today's hem-tool asset name stays and new variants get a suffix; the full-dynamic hem-tool is dropped; `release.yml` builds the published variants, `ci.yml` exercises the switches and both backends; macOS and MSVC out); user decision 2026-10-14 (STEP-M12-110: "I dont want any distribution with static wolfssl, static distribution should only openssl" — the wolfSSL library package carries no static library; §6.2 report approved the same day); user decision 2026-10-15 (STEP-M12-120: the Windows -static assets load no OpenSSL configuration — "No config at all", chosen over an administrator-only directory and a registry lookup; §6.2 report approved the same day); ARCHITECTURE.md §1, §11 M12 part C, §12 risk 1; REQ-BUILD-005 (the hem-tool release this extends — unchanged); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-BUILD-005", "REQ-BUILD-006", "REQ-BUILD-007", "REQ-BUILD-008", "REQ-API-010", "REQ-TOOL-026"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
---

# Release matrix

The release workflow (`.github/workflows/release.yml`, REQ-BUILD-005)
SHALL additionally build, check and publish — for linux-x86_64 and
windows-x86_64, as artifacts on `main` and as assets of every `v*`
release — three further variants, named by a `-static` suffix that means
"OpenSSL backend, every third-party library static, no companion
library":

| Asset | Contents |
|---|---|
| `hem-tool-<label>-<os>-x86_64.{tar.gz,zip}` | **unchanged** (REQ-BUILD-005): dynamic wolfSSL, needs the companion asset |
| `hem-tool-<label>-<os>-x86_64-static.{tar.gz,zip}` | one file; OpenSSL backend (REQ-BUILD-007); pinned static OpenSSL (3.5 LTS line) + libcurl with OpenSSL as its TLS backend; README + notices (MIT, curl, cJSON, BIP39, qrcodegen, **OpenSSL Apache-2.0**) |
| `encedo-hem-<label>-<os>-x86_64.{tar.gz,zip}` | SDK library package, wolfSSL flavour, **shared library only** (user decision 2026-10-14 — no static library ships with wolfSSL): `include/ehem/`, `libencedo-hem.so.1.2.0` + symlinks (Linux) / `libencedo-hem.dll` + `libencedo-hem.dll.a` (Windows) — libcurl static inside it — the CMake package config, `encedo-hem.pc`, notices, README. Runs against the **wolfSSL companion asset** of the same release (unchanged `wolfssl-<wv>-<os>-x86_64`) |
| `encedo-hem-<label>-<os>-x86_64-static.{tar.gz,zip}` | SDK static library package, OpenSSL flavour: `include/ehem/`, `libencedo-hem.a`, the pinned static `libcurl.a`, `libssl.a`, `libcrypto.a` it was built against, the CMake package config and `.pc` pointing at them, notices incl. OpenSSL's, README |
| `wolfssl-<wv>-<os>-x86_64.*`, `wolfssl-<wv>-stable-src.tar.gz`, `SHA256SUMS` | unchanged, SHA256SUMS now covers every asset |

- **What "static" means.** Linux: every third-party library static and
  glibc dynamic — `ldd` lists the glibc family only (a static glibc
  breaks `getaddrinfo`/NSS); RUNPATH absent. Windows: `-static` as today
  — the import table lists Windows system DLLs only (`crypt32`/`ws2_32`
  etc. for OpenSSL are system DLLs). Asserted in the job like the M10
  link-mode check, plus **no wolfSSL symbol or string in any `-static`
  asset** (`nm`/`strings` for `wolfSSL_`/`wc_`) — the §1 rule as a test.
- **Checked inside the workflow** per variant: unit suite on the Release
  build (both backends), link mode, `hem-tool help`, the REQ-NET-005
  classifier under the OpenSSL-backed libcurl
  (`check-expired-classifier.sh` on the static binary), the Windows
  missing-DLL message of the dynamic binary (REQ-TOOL-026 negative
  smoke), and a **consumer smoke**: each library package unpacked and
  the REQ-BUILD-008 sample consumer built against it via `find_package`
  and `pkg-config`, then run.
- **Trust store** of the static binaries: Linux — OpenSSL's default
  certificate directory/file compiled in plus libcurl's distro
  CA-bundle fallbacks (as REQ-BUILD-005); Windows — `CURLSSLOPT_NATIVE_CA`
  (the SDK transport already sets it on `_WIN32`; libcurl supports it
  with the OpenSSL backend).
- **OpenSSL configuration** of the `-static` assets (user decision
  2026-10-15): on Windows none is loaded — OpenSSL is built with
  `no-autoload-config` and libcurl with
  `CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG`, so neither `openssl.cnf` nor
  `OPENSSL_CONF` is read. OpenSSL's compiled-in directories
  (`OPENSSLDIR`, `ENGINESDIR`, `MODULESDIR`) lie under the
  administrator-only `C:\Program Files\Common Files\SSL`, never under the
  build runner's path, which a standard user could create on another
  machine (the CVE-2019-1552 class). An application linking the Windows
  static library package loads a configuration itself if it wants one.
  On Linux they read `/etc/ssl/openssl.cnf` (root-owned), as a
  distribution's OpenSSL does.
- **Pins** live where REQ-BUILD-005's do (`scripts/release/build-deps.sh`,
  version + sha256, a second prefix `deps-openssl`); the CI dependency
  caches are keyed per prefix. OpenSSL's `LICENSE.txt` is re-read on
  every bump like wolfSSL's `COPYING`.
- **Jobs:** the Linux and Windows jobs each build both prefixes and all
  variants (or a 2×2 matrix — settled at the step; the publishing job is
  unchanged in shape: assemble, verify `SHA256SUMS`, duplicate-name
  check, publish). Build time stays within the free runner budget.
- **Docs:** README "Download" gains the asset table and "which one do I
  want" guidance; `docs/RELEASING.md` covers the new prefixes and the
  attended per-variant smoke.
- **Out of scope** (user decision 2026-10-12): a full-dynamic hem-tool,
  macOS, MSVC (BACKLOG.md candidates).

**Rationale:** operators asked for a one-file tool and consumers for a
library they can link, while the licensing position (§1: no wolfSSL in
a static package) forces the static flavour onto OpenSSL. One release,
one `SHA256SUMS`, one tag path (proven in M10) — only the asset set
grows. The wolfSSL library package reuses the companion asset so the
GPL-licensed library stays a separately obtained download.

**Acceptance criteria:**
- [ ] `release.yml` on a push to `main`: every asset in the table
      produced on both platforms; all link-mode and no-wolfSSL-in-static
      assertions, unit suites, classifier checks and consumer smokes
      pass (run id recorded).
      *(The same workflow on a `workflow_dispatch` on `development`,
      2026-10-16 — release run 38894733780 (0483532), both jobs green:
      every asset of the table built (Linux 6 archives, Windows 5, each
      job's `SHA256SUMS` over all of them), every assertion, unit suite,
      classifier check and consumer smoke below passed. The box waits for
      the run of the push to `main` (STEP-M12-130/140).)*
- [x] Static hem-tool: Linux `ldd` = glibc family only, no RUNPATH;
      Windows import table = system DLLs only; `strings`/`nm` show no
      wolfSSL; `hem-tool --version` reports `OpenSSL <pinned>` compiled
      and loaded (job log).
      *(Release run 38894733780, 2026-10-16, both jobs green.
      `scripts/release/package-static.sh` fails the job unless: the unit
      suite passes on the static OpenSSL Release build; Linux — `ldd`
      lists the glibc family only and `readelf -d` no RUNPATH/RPATH;
      Windows — `objdump -p` imports only listed system DLLs, the
      Universal CRT among them; `nm` shows no `wolfSSL_`/`wolfCrypt_`/`wc_`
      symbol and `strings` no such name; `--version` prints exactly
      `crypto:    OpenSSL 3.5.9 (compiled) / 3.5.9 (loaded), static` and
      `transport: libcurl 8.10.1 (compiled) / 8.10.1 (loaded), static`;
      the archive, unpacked alone, holds one hem-tool file that runs
      `help`, `keys types` and `status` (initialised, exit 1 on a closed
      port). The Linux job then ran `check-expired-classifier.sh` on the
      static binary: the REQ-NET-005 classifier under the OpenSSL-backed
      libcurl (REQ-BUILD-007's hand-over). Job logs need a signed-in
      reader; the step conclusions are the record.)*
- [x] OpenSSL configuration (job log): the Windows static hem-tool
      initialises and runs with an `OPENSSL_CONF` that activates only
      OpenSSL's `null` provider (which would leave it no algorithms), and
      the prefix's `openssl version -d -e -m` lists the
      administrator-only directories; the Linux static binary refuses the
      same file (exit 12) — the control that the file is read there.
      *(Release run 38894733780, 2026-10-16, both jobs green, both
      `deps-openssl` prefixes built cold. `build-deps.sh` fails unless the
      prefix's `openssl version -d -e -m` names exactly the chosen
      directories — Windows `C:/Program Files/Common Files/SSL`,
      `…/engines-3`, `…/ossl-modules`; Linux `/etc/ssl`,
      `/usr/lib/engines-3`, `/usr/lib/ossl-modules` — and, on Windows,
      `configuration.h` defines `OPENSSL_NO_AUTOLOAD_CONFIG` and curl's
      `curl_config.h` `CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG`.
      `package-static.sh` runs the unpacked hem-tool's `status` under an
      `OPENSSL_CONF` naming a file that activates only the `null`
      provider: Windows exit 1 (initialised, the file not read), Linux
      exit 12 with "OpenSSL is linked into hem-tool, so the OpenSSL
      configuration it read …" (REQ-TOOL-024 rev 5); any other result
      fails the job.)*
- [x] Library packages: the consumer smoke (find_package + pkg-config)
      passes in the job on both platforms — the wolfSSL flavour against
      its shared library, run with the companion asset's library and
      nothing else non-system; the OpenSSL flavour against its static
      library (job log).
      *(wolfSSL flavour, 2026-10-15, STEP-M12-110 — release run
      38719457329 (workflow_dispatch on `development`, 46be9d3), both jobs
      green: `scripts/release/package-lib.sh` builds the shared library
      alone, packages `encedo-hem-<label>-<os>-x86_64` and runs the
      REQ-BUILD-008 gate in package mode against the unpacked package with
      the companion asset's library put into it (Linux `lib/`, Windows
      `bin/`), `PKG_CONFIG_LIBDIR` = the package, no `LD_LIBRARY_PATH`; it
      fails the job unless the package holds no static library and no
      hem-tool, the library imports the companion wolfSSL and no libcurl /
      OpenSSL / MinGW runtime (Linux: RUNPATH `$ORIGIN`; Windows: the
      Universal CRT, no msvcrt), and each consumer resolves libencedo-hem
      and wolfSSL from the package and nothing else outside the system
      (Linux `ldd`; Windows: the import table, the package's `bin/` first
      on PATH). The job logs need a signed-in reader — the run's
      conclusions are the record. The artifacts grew by the package:
      Linux 24,720,005 → 25,129,032 bytes, Windows 1,274,926 → 1,706,552
      (against run 38483282667). The OpenSSL flavour, 2026-10-16 —
      release run 38894733780 (0483532), both jobs green:
      `package-lib.sh static` builds the static library alone (OpenSSL
      backend, `EHEM_DEPS_LINK=static`) and packages
      `encedo-hem-<label>-<os>-x86_64-static` with `libcurl.a`, `libssl.a`,
      `libcrypto.a`, their headers, curl's CMake package and `.pc` files
      rewritten to `${pcfiledir}`; it fails unless no shared library, no
      wolfSSL file or symbol and no build-prefix path is in the package,
      and its gate builds and runs `consumer_cmake_static` and
      `consumer_pc_static` (the `pkg-config --static` closure) against the
      unpacked package — each with no SDK, curl or crypto shared library
      (Linux `ldd`; Windows the import table). REQ-BUILD-008 criterion 4.)*
- [ ] Tag path: an annotated `v1.2.0-rc<N>` publishes a pre-release
      carrying every asset (count recorded) with the annotation +
      checksums as text; `SHA256SUMS` re-verified in the job.
- [ ] Attended (recorded here with date and asset labels): each
      published hem-tool variant runs `hem-tool status` against the dev
      device under system trust on Linux and Windows; the static
      binaries from a directory with NO companion library.
- [x] REQ-BUILD-005 unchanged in behaviour: the dynamic hem-tool archive,
      the companion assets and the source tarball are byte-for-byte the
      same kind of output as 1.1.0 (same scripts, same assertions).
      *(Through STEP-M12-110, 2026-10-15: `package.sh` gained only the
      `package-lib.sh` call before SHA256SUMS, `package-wolfssl.sh` is
      unchanged, and `build-deps.sh`'s wolfSSL and libcurl options are the
      1.1.0 ones — the Windows link only now writes the delay-load `.def`
      at a path the linker can open (46be9d3). Release run 38719457329 ran
      every REQ-BUILD-005 assertion green on both platforms, the Windows
      no-DLL smoke (exit 12) included. Rechecked after STEP-M12-120,
      2026-10-16: the -static variant enters `package.sh` through a fourth
      argument (`static`) that hands over to `package-static.sh` before any
      dynamic step, and `SHA256SUMS` is written by the same code for both
      variants; `package-lib.sh`'s shared branch is the 110 code;
      `build-deps.sh`'s wolfSSL and libcurl-on-wolfSSL lines are unchanged
      (the diff touches the OpenSSL branch only). Release run 38894733780:
      the dynamic step and the wolfSSL classifier green on both platforms,
      from a cold `deps` cache.)*
- [x] RESOLVED at STEP-M12-110 (2026-10-14) — was OPEN: whether the
      wolfSSL companion asset on Windows must gain `libwolfssl.dll.a` so
      the wolfSSL-flavour static SDK can be linked by a consumer, or
      whether `libencedo-hem.a` consumers are told to use the shared SDK
      on Windows; and the exact OpenSSL 3.5.x pin + sha256.
      *(Moot: the wolfSSL library package carries no static library (user
      decision 2026-10-14), so the companion assets stay as in 1.1.0. The
      pin: OpenSSL 3.5.9 (2026-09-29, the newest 3.5 LTS release), sha256
      `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a` —
      computed from the official download (the openssl-3.5.9 release on
      github.com/openssl/openssl, linked from openssl-library.org/source),
      equal to the published `.sha256`, and the tarball's PGP signature
      verifies against OpenSSL's key from
      openssl-library.org/source/pubkeys.asc (B146 647E 45A7 B339 47AB
      226B 2A2C 87D1 6169 2D40). `LICENSE.txt`: Apache License 2.0.)*
