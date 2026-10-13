---
id: REQ-BUILD-009
title: Release matrix — static hem-tool and SDK library packages (wolfSSL and OpenSSL flavours) beside the M10 hem-tool release
status: approved
priority: should
revision: 1
source: user decisions 2026-10-12 (M12 planning: one milestone, one release 1.2.0; static variants ship on OpenSSL, never wolfSSL; today's hem-tool asset name stays and new variants get a suffix; the full-dynamic hem-tool is dropped; `release.yml` builds the published variants, `ci.yml` exercises the switches and both backends; macOS and MSVC out); ARCHITECTURE.md §1, §11 M12 part C, §12 risk 1; REQ-BUILD-005 (the hem-tool release this extends — unchanged); approved 2026-10-12 (M12 decomposition, user: "approve")
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
| `encedo-hem-<label>-<os>-x86_64.{tar.gz,zip}` | SDK library package, wolfSSL flavour: `include/ehem/`, `libencedo-hem.so.1.2.0` + symlinks (Linux) / `libencedo-hem.dll` + `libencedo-hem.dll.a` (Windows), `libencedo-hem.a`, the pinned static `libcurl.a` the static library needs, the CMake package config, `encedo-hem.pc`, notices, README. Runs and links against the **wolfSSL companion asset** of the same release (unchanged `wolfssl-<wv>-<os>-x86_64`) |
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
- [ ] Static hem-tool: Linux `ldd` = glibc family only, no RUNPATH;
      Windows import table = system DLLs only; `strings`/`nm` show no
      wolfSSL; `hem-tool --version` reports `OpenSSL <pinned>` compiled
      and loaded (job log).
- [ ] Library packages: the consumer smoke (find_package + pkg-config,
      shared where present and static) passes in the job for both
      flavours on both platforms; the wolfSSL-flavour static library
      links against the companion asset's library and nothing else
      non-system (job log).
- [ ] Tag path: an annotated `v1.2.0-rc<N>` publishes a pre-release
      carrying every asset (count recorded) with the annotation +
      checksums as text; `SHA256SUMS` re-verified in the job.
- [ ] Attended (recorded here with date and asset labels): each
      published hem-tool variant runs `hem-tool status` against the dev
      device under system trust on Linux and Windows; the static
      binaries from a directory with NO companion library.
- [ ] REQ-BUILD-005 unchanged in behaviour: the dynamic hem-tool archive,
      the companion assets and the source tarball are byte-for-byte the
      same kind of output as 1.1.0 (same scripts, same assertions).
- [ ] OPEN (settle at the step): whether the wolfSSL companion asset on
      Windows must gain `libwolfssl.dll.a` so the wolfSSL-flavour static
      SDK can be linked by a consumer, or whether `libencedo-hem.a`
      consumers are told to use the shared SDK on Windows; and the exact
      OpenSSL 3.5.x pin + sha256.
