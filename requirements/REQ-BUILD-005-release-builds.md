---
id: REQ-BUILD-005
title: Release builds of hem-tool — static binaries from CI, artifacts on main, GitHub Releases on tags
status: draft
priority: should
revision: 1
source: user decision 2026-10-07 ("github ci actions to build and release the cli tool"; answers the same day: build on every push to main, publish a Release on tags; fully static binaries; Release-type build + SHA256SUMS); ARCHITECTURE.md §1 (wolfSSL "may also serve as libcurl's TLS backend where we build libcurl ourselves"), §11 (M10), §12 risk 1 (wolfSSL licensing); REQ-BUILD-002 (the existing CI stays as is)
depends_on: ["REQ-BUILD-001", "REQ-BUILD-002", "REQ-NET-003", "REQ-NET-005"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
---

# Release builds of hem-tool — static binaries from CI, artifacts on main, GitHub Releases on tags

A GitHub Actions workflow (`.github/workflows/release.yml`, separate from
`ci.yml`) SHALL build hem-tool as a single-file, Release-type binary for
Linux x86_64 and Windows x86_64 (MinGW) on every push to `main` —
uploading the archives as workflow artifacts — and, on a version tag
`v*`, SHALL publish the same archives plus a `SHA256SUMS` file as a
GitHub Release.

- **Static.** libcurl and wolfSSL are built from **pinned source
  releases inside the workflow** (not the distro / MSYS2 packages) and
  linked statically: libcurl with wolfSSL as its TLS backend — the
  ARCHITECTURE §1 case "where we build libcurl ourselves" — and HTTP/HTTPS
  only (every other protocol and optional dependency disabled). Linux:
  no third-party shared library remains (`ldd` lists only the glibc
  family); glibc stays dynamic, and the build runs on the oldest Ubuntu
  LTS runner GitHub offers, for reach. Windows: `-static`
  (libgcc / libwinpthread) so `hem-tool.exe` depends only on Windows
  system DLLs.
- **Archives.** `hem-tool-<version>-linux-x86_64.tar.gz` and
  `hem-tool-<version>-windows-x86_64.zip`; `<version>` is the project
  version (`ehem_version()`), suffixed `+g<shortsha>` for main-branch
  builds. Each archive carries the binary, a short README (usage pointer,
  checksum command) and the license notices (MIT for the SDK and the
  tool, curl's license, wolfSSL's — see the licensing criterion).
- **Checked inside the workflow:** the unit suite (`ctest -L unit`) runs
  against the static Release build on both platforms; `hem-tool help`
  runs; the dependency check (`ldd` / `objdump -p … | grep DLL`) is
  asserted, not just printed.
- **Trust store.** System-trust mode (`EHEM_TLS_SYSTEM`, REQ-NET-003) must
  work out of the box: Linux — libcurl configured with the common
  distro CA-bundle paths as fallbacks; Windows — the pinned libcurl's
  wolfSSL backend loads the native store if it can, otherwise a bundled
  Mozilla CA file next to the exe is used by default.
- **Tags.** `v<version>` must equal the project version (the job fails on
  a mismatch); release notes come from the tag annotation; assets are the
  two archives and `SHA256SUMS`. `workflow_dispatch` MAY build any ref
  into artifacts.

**Rationale:** operators need the CLI without a toolchain. A static
binary with one TLS stack (wolfSSL in the shim *and* in libcurl) is the
portable shape and removes the distro-libcurl/wolfSSL version lottery;
main-branch artifacts keep testers supplied, tags make the public drop.
The existing CI (REQ-BUILD-002) is untouched — the release workflow is
additional.

**Acceptance criteria:**
- [ ] Triggers: push to `main` → the two archives as workflow artifacts;
      tag `v*` → a GitHub Release with the archives + `SHA256SUMS`;
      `workflow_dispatch` builds a chosen ref. `ci.yml` unchanged.
- [ ] Static proven in the job: Linux `ldd` shows no libcurl/libwolfssl
      (glibc family only); Windows import table lists only system DLLs.
- [ ] Unit suite green on the static Release build, both platforms, in
      the workflow.
- [ ] OPEN — REQ-NET-005 under a wolfSSL-backed libcurl: the expired-cert
      classifier (`src/transport_curl.c:335-343`) keys on the
      OpenSSL/GnuTLS verify code 10 and the text "expired"; wolfSSL's
      date failure is `ASN_AFTER_DATE_E` (-150, "ASN date error, current
      date after"). Extend the classifier (code and/or text) and PROVE it
      in the workflow against a local HTTPS endpoint serving an expired
      self-signed certificate — a release binary that cannot classify an
      expired device certificate silently loses automatic recovery.
- [ ] System trust works: both release binaries run `hem-tool status`
      against the dev device (public-CA ZeroSSL certificate) with no
      `--cacert` / `--insecure` — attended (no device in CI), recorded
      here with date and binary version.
- [ ] OPEN — user decision before the FIRST tagged Release: wolfSSL
      licensing for distributed binaries (ARCHITECTURE §12 risk 1) —
      GPLv3 compliance (license text + source offer in the archive;
      hem-tool and the SDK are MIT, compatible) or Encedo's commercial
      wolfSSL license. The notice bundle reflects the choice.
- [ ] Version/tag consistency enforced; `SHA256SUMS` re-verified from a
      fresh download in the job; `README.md` gains a "Download" section
      (Releases link + checksum command).
