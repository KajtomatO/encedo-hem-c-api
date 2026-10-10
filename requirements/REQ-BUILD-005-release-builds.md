---
id: REQ-BUILD-005
title: Release builds of hem-tool — dynamic-wolfSSL binaries from CI, artifacts on main, GitHub Releases on tags
status: approved
priority: should
revision: 3
source: user decision 2026-10-07 ("github ci actions to build and release the cli tool"; answers the same day: build on every push to main, publish a Release on tags; Release-type build + SHA256SUMS); approved 2026-10-08 (user "ok") as rev 1 (fully static binaries); rev 2 — user decision 2026-10-09 (no statically bundled wolfSSL in any published package, for licensing reasons — wolfSSL is linked dynamically and published as a separate release asset; libcurl stays pinned static; a full-dynamic variant and static packages on another library go to M12) — rev 2 approved 2026-10-09 (user "I accept new req"); rev 3 — user decision 2026-10-10 (`v<version>-rc<N>` tags publish a GitHub pre-release, so the never-run publishing job is proven before the real release; the release text = the tag annotation + checksums, annotated tags enforced) — rev 3 approved 2026-10-10 (user "I approve REQ-BUILD-005 rev 3"); ARCHITECTURE.md §1 (wolfSSL "may also serve as libcurl's TLS backend where we build libcurl ourselves"; "wolfSSL is never statically bundled into a published package"), §11 (M10), §12 risk 1 (wolfSSL licensing); REQ-BUILD-002 (the existing CI stays as is)
depends_on: ["REQ-BUILD-001", "REQ-BUILD-002", "REQ-NET-003", "REQ-NET-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
---

# Release builds of hem-tool — dynamic-wolfSSL binaries from CI, artifacts on main, GitHub Releases on tags

A GitHub Actions workflow (`.github/workflows/release.yml`, separate from
`ci.yml`) SHALL, on every push to `main`, build hem-tool as a Release-type
binary that links wolfSSL dynamically, for Linux x86_64 and Windows x86_64
(MinGW), and upload the archives as workflow artifacts — and, on a version
tag `v*`, publish the same archives, the wolfSSL companion assets and a
`SHA256SUMS` file as a GitHub Release.

- **Link mode.** libcurl and wolfSSL come from **pinned source releases
  inside the workflow** (version and sha256; not the distro / MSYS2
  packages). libcurl: linked statically, with wolfSSL as its TLS backend —
  the ARCHITECTURE §1 case "where we build libcurl ourselves" — and
  HTTP/HTTPS only (every other protocol and optional dependency disabled).
  wolfSSL: built as a **shared** library with the same feature set as the
  headers hem-tool compiles against (the installed `options.h` describes
  the shipped library) and linked **dynamically** — it is never inside the
  hem-tool archive (ARCHITECTURE §1). hem-tool resolves it from its own
  directory first, then the system: on Linux the RUNPATH is exactly
  `$ORIGIN`; on Windows the exe's directory is first in the DLL search
  order. Linux: `ldd` lists the glibc family and `libwolfssl.so.<N>` only
  — no libcurl/libssl/libcrypto; glibc stays dynamic, and the build runs on
  the oldest Ubuntu LTS runner GitHub offers, for reach. Windows: `-static`
  (libgcc / libwinpthread) so `hem-tool.exe` imports Windows system DLLs
  and `libwolfssl.dll` only; the DLL itself is linked the same way, so it
  depends on system DLLs only.
- **Archives.** `hem-tool-<version>-linux-x86_64.tar.gz` and
  `hem-tool-<version>-windows-x86_64.zip`; `<version>` is the project
  version (`ehem_version()`), suffixed `+g<shortsha>` for main-branch
  builds. Each archive carries the binary, a short README (usage pointer,
  checksum command, the run-time wolfSSL requirement and where to get it)
  and the license notices of everything compiled in (MIT for the SDK and
  the tool, curl's license, cJSON, the BIP39 wordlist, qrcodegen) — no
  wolfSSL text, since wolfSSL is not distributed in it.
- **wolfSSL companion assets.** `wolfssl-<wv>-linux-x86_64.tar.gz` and
  `wolfssl-<wv>-windows-x86_64.zip` = the runtime library as a real file
  (`libwolfssl.so.<N>` / `libwolfssl.dll`), wolfSSL's `COPYING` verbatim,
  `BUILD-CONFIG.txt` (the exact configure line) and a README (upstream tag
  URL, source tarball sha256, placement); plus the unmodified upstream
  source tarball `wolfssl-<wv>-stable-src.tar.gz`, published once per
  release so the corresponding source accompanies the binary (GPLv2 §3).
  All of them are listed in `SHA256SUMS`.
- **Checked inside the workflow:** the unit suite (`ctest -L unit`) runs
  against the Release build on both platforms (the shared library on the
  loader path); `hem-tool help` runs; the link mode is asserted, not just
  printed — wolfSSL dynamic (and resolved from `$ORIGIN` on Linux), nothing
  else dynamic, the Windows DLL free of MinGW runtime imports; and a
  **drop-in smoke test** unpacks the hem-tool archive and the companion
  asset side by side and runs `hem-tool help` with a clean environment
  (Linux additionally proves the binary refuses to start without the
  library, when the host has no system copy).
- **Trust store.** System-trust mode (`EHEM_TLS_SYSTEM`, REQ-NET-003) must
  work out of the box: Linux — libcurl configured with the common
  distro CA-bundle paths as fallbacks; Windows — the pinned libcurl's
  wolfSSL backend loads the native store if it can, otherwise a bundled
  Mozilla CA file next to the exe is used by default.
- **Tags.** A tag is either `v<version>` — a release — or
  `v<version>-rc<N>` (N ≥ 1) — a pre-release of that version, published as
  a GitHub *pre-release* (deletable after the check); `<version>` must equal
  the project version, and any other tag fails the job before publishing.
  Tags must be annotated: the release text is the tag annotation followed
  by the checksums, and a lightweight tag fails before publishing. Assets
  are the two hem-tool archives, the two companion assets, the source
  tarball and `SHA256SUMS`. `workflow_dispatch` MAY build any ref into
  artifacts.

**Rationale:** operators need the CLI without a toolchain, and no published
package may bundle wolfSSL statically (licensing — ARCHITECTURE §1, §12
risk 1). One TLS stack (wolfSSL in the shim *and* in libcurl) is kept: the
pinned shared wolfSSL beside the binary removes the distro-wolfSSL
version/ABI lottery just as the rev-1 static build did, while the
GPL-licensed library stays a separately obtained, separately licensed
download with its source. libcurl's license is permissive, so it stays
compiled in. Main-branch artifacts keep testers supplied, tags make the
public drop. The existing CI (REQ-BUILD-002) is untouched — the release
workflow is additional.

**Acceptance criteria:**
- [ ] Triggers: push to `main` → the archives as workflow artifacts; tag
      `v*` → a GitHub Release with the hem-tool archives, the wolfSSL
      companion assets, the source tarball and `SHA256SUMS`;
      `workflow_dispatch` builds a chosen ref. `ci.yml` unchanged.
      *(Read as "unchanged by the release work": ci.yml's MSYS2
      environment moved MINGW64 → UCRT64 on 2026-10-10 by a separate user
      decision — ARCHITECTURE §1 — that applies to both workflows; the
      release workflow still neither calls nor alters ci.yml.)*
- [x] Link mode proven in the job: Linux `readelf` RUNPATH == `$ORIGIN`,
      `ldd` shows `libwolfssl.so.<N>` resolved from the binary's own
      directory and no libcurl/libssl/libcrypto; Windows import table lists
      system DLLs + `libwolfssl.dll` only, and `libwolfssl.dll` imports no
      libgcc/winpthread. *(Rev 2 Linux proven locally 2026-10-09 by the
      same package.sh: RUNPATH `[$ORIGIN]`, NEEDED libwolfssl.so.42 + libc
      only, ldd resolves libwolfssl.so.42 from build-release/ beside the
      binary — not the host's system copy; unit 46/46 on the Release
      build; drop-in smoke OK; SHA256SUMS lists the two archives and the
      source tarball at the pinned hash. The host's system libwolfssl.so.42
      skipped the negative test — the CI runner has none. The rev-1 static
      proof is superseded. First workflow run 2026-10-10 (38045496482):
      Windows ✓; Linux failed to link the shared-SDK integration tests on
      the clean runner — the local proof had been masked by the host's
      system libwolfssl.so.42; fixed (`-rpath-link`, STEP-M10-065 notes).
      **PROVEN 2026-10-10 in run 38046929806 (main 08bd2df): both jobs
      green — every link-mode assertion, the Windows DLL runtime check and
      the drop-in smoke (incl. the Linux negative test, which runs on the
      clean runner) passed; no warnings.**)*
- [x] Unit suite green on the Release build, both platforms, in the
      workflow. *(Run 38046929806, 2026-10-10: 46/46 on Linux (GCC 11.4,
      ubuntu-22.04) and Windows (MSYS2 UCRT64). Rev-1 note kept: the static -O3 build of 2026-10-09
      surfaced and fixed a -Wformat-truncation in crypto_shim.c that Debug
      builds never saw.)*
- [x] RESOLVED (STEP-M10-065, 2026-10-09): the classifier
      (`src/transport_curl.c`) now also accepts wolfSSL's verdict —
      `ASN_AFTER_DATE_E` (-151 in 5.6.x/5.7.x, -150 older) via
      CURLINFO_SSL_VERIFYRESULT, the texts "date error, current date
      after" / "ASN_AFTER_DATE", and CURLE_SSL_CONNECT_ERROR as the curl
      code — and `scripts/release/check-expired-classifier.sh` proves it:
      a valid local CA + an EXPIRED leaf served by `openssl s_server`
      (wolfSSL refuses to load an expired cert as a trust anchor, so the
      leaf must be signed by a valid CA), `hem-tool recovery` prints the
      EXPIRED diagnosis. Proven locally with the wolfSSL-backed build
      (curl's wolfSSL text: "server verification failed: certificate has
      expired."); the link mode of wolfSSL does not change the verdict, and
      the workflow's Linux job runs the same script on every build.
- [ ] System trust works: both release binaries run `hem-tool status`
      against the dev device (public-CA ZeroSSL certificate) with no
      `--cacert` / `--insecure` — attended (no device in CI), recorded
      here with date and binary version. *(Linux ✓ 2026-10-09 with the
      rev-1 static binary hem-tool-1.0.0+ga285ec7 — wolfSSL backend,
      compiled-in bundle /etc/ssl/certs/ca-certificates.crt — `status` and
      `recovery` (healthy path) against my.ence.do under system trust, exit
      0. **Rev 2 Linux ✓ 2026-10-09:** hem-tool-1.0.0+ge008521 unpacked
      from its archive with libwolfssl.so.42 from the companion asset
      beside it, `env -i ./hem-tool status` under system trust → device
      data (fw v1.2.2-DIAG), exit 0. Windows pending.)*
- [x] RESOLVED 2026-10-09 (user decision): wolfSSL licensing for
      distributed binaries (ARCHITECTURE §12 risk 1) — no published package
      bundles wolfSSL statically; hem-tool links it dynamically; the library
      is a separate asset carrying its own license text — GPLv2 "or, at
      your option, any later version" per the pinned 5.7.2 tarball's
      `COPYING`/`LICENSING` (re-checked on every pin bump; rev 1 said
      GPLv3) — its exact build configuration and the corresponding source
      tarball; the hem-tool archive carries only MIT/permissive notices.
      Static packages, if ever shipped, use a different library (M12).
- [ ] Companion assets present: both `wolfssl-<wv>-<os>-x86_64` archives
      and `wolfssl-<wv>-stable-src.tar.gz` on the Release (as artifacts on
      main); the drop-in smoke test passed in both jobs. *(Linux assets
      produced locally 2026-10-09 by package-wolfssl.sh — library as a real
      file, GPLv2 COPYING, BUILD-CONFIG.txt, README with the source sha256;
      smoke OK. Workflow/Windows pending.)*
- [ ] Version/tag consistency enforced (`v<version>` → release,
      `v<version>-rc<N>` → pre-release, anything else fails before
      publishing); a lightweight tag fails before publishing and the release
      text is the annotation + checksums; `SHA256SUMS` re-verified from a
      fresh download in the job; `README.md` "Download" section describes
      the archive + companion-asset layout and the checksum command.
      *(Local 2026-10-10: package.sh rejects v1.0.0, v1.1, v1.1.0-rc0,
      -rcX, -rc1-rc2, -beta1 before configuring and builds v1.1.0-rc1 as
      hem-tool-1.1.0-rc1-*; release-notes.sh refuses our lightweight
      v1.0.0 and renders an annotated tag (wolfSSL's v5.7.2-stable) +
      checksums. In the workflow: pending the first rc tag.)*
