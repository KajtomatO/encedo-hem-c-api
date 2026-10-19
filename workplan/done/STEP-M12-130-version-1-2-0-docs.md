---
id: STEP-M12-130
title: "Version 1.2.0 + user-facing docs (API-GUIDE, README, COVERAGE, KNOWN-ISSUES, RELEASING, tests/README) + v1.2.0-rc1 pre-release"
milestone: M12
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#4-public-api--conventions"]
depends_on: ["STEP-M12-020", "STEP-M12-030", "STEP-M12-040", "STEP-M12-050", "STEP-M12-060", "STEP-M12-070", "STEP-M12-080", "STEP-M12-090", "STEP-M12-100", "STEP-M12-110", "STEP-M12-120"]
evidence:
  commits: ["333284c", "5954950"]   # start; the version + docs commit (the rc1 tag's commit); the done commit follows
  tests:
    - "CI run 38903704626 (push of 5954950 to development, 2026-10-16): all 10 legs green — Linux gcc / clang / gcc OpenSSL / clang OpenSSL / gcc static-only / gcc shared-only (hem-tool on the shared SDK); Windows MinGW / OpenSSL / static-only / shared-only"
    - "Release run 38903755451 (workflow_dispatch on development at 5954950, 2026-10-16): Linux and Windows jobs green with warm caches (1:16 / 2:14), every package and smoke step green; the publishing job skipped (no tag); artifacts release-linux-x86_64 32,964,076 bytes and release-windows-x86_64 9,390,666 bytes — the 11 archives + SHA256SUMS"
    - "Release run 38904497654 (push of the annotated tag v1.2.0-rc1 = 5954950, 2026-10-16, tagged by the user): all three jobs green — Linux 3:46 and Windows 8:07 with BOTH prefixes built cold (a tag run cannot read the caches made on development), the publishing job's 'Assemble assets + one SHA256SUMS' (sha256sum -c, duplicate-name check), 'Release notes' and 'Publish' steps green. Release 414975403, prerelease true, 12 uploaded assets: hem-tool-1.2.0-rc1-{linux,windows}-x86_64{,-static}.{tar.gz,zip}, encedo-hem-1.2.0-rc1-{linux,windows}-x86_64{,-static}.{tar.gz,zip}, wolfssl-5.7.2-{linux-x86_64.tar.gz,windows-x86_64.zip}, wolfssl-5.7.2-stable-src.tar.gz, SHA256SUMS (+ GitHub's own zip and tar.gz source archives = 14 visible)"
    - "Attended by the user (reported 2026-10-19), rc1 assets on Windows AND Linux: the dynamic hem-tool started without wolfSSL shows the refusal message; with the companion library copied beside it `status` works; the -static hem-tool ran `init-wizard` end to end, generated a key, signed a string, wiped the device, then listed the keys of the other (Manager-initialised) unit — all worked (recorded under REQ-BUILD-009's attended criterion; the gate repeats `status` under system trust on the rc2 binaries)"
    - "Local 2026-10-16 after the bump: ./dev ci 56/56 gcc + clang, ./dev ci --openssl 56/56 gcc + clang (the docs gate check_docs_coverage, the export baseline and the consumer gate among them). The build reads 1.2.0 everywhere: libencedo-hem.so.1.2.0 with SONAME libencedo-hem.so.1, the consumer gate's install has encedo-hem.pc 'Version: 1.2.0' and encedo-hem-config-version.cmake PACKAGE_VERSION 1.2.0, test_version asserts ehem_version() = the project version, hem-tool --version prints 'hem-tool 1.2.0'. Exports beyond the 1.0 baseline: the 1.1 additions + ehem_build_info_get (REQ-API-008 rev 2 already records the 1.2 additions)"
  notes: |
    The version lives in one place — project(VERSION) in CMakeLists.txt;
    ehem_version(), the user agent, the .so version, the package version
    file and encedo-hem.pc take it from there.
    KNOWN-ISSUES had no entry of its own for 0xC0000135 (the Windows
    missing DLL was a paragraph of the foreign-libwolfssl entry), so the
    one entry became "FIXED in 1.2.0" covering both and moved below the
    OPEN ones; the X25519 entry's residual notes now point at the
    self-test they motivated.
    README Status block reads "1.2.0 release candidate (2026-10-16)" with
    1.1.0 as the latest release; STEP-M12-140 turns it into "released"
    with the date of the tag (its docs-checklist box).
    rc1 tag message drafted from the README Status block and ARCHITECTURE
    §11 M12 (RELEASING "Final release"): scratchpad
    release-notes-1.2.0-rc1.md, for `git tag -a v1.2.0-rc1
    --cleanup=whitespace -F <file>`. The user tagged 5954950 on
    development and pushed the tag on 2026-10-16.
    The step body said "ten" published archives: the release has eleven
    (the wolfSSL source tarball was missed) + SHA256SUMS = 12 assets,
    matching REQ-BUILD-009's table.
    The rc1 test (user, 2026-10-19) brought three fixes for M12 before
    1.2.0 — the one-file hem-tool becomes the plain asset name and the
    wolfSSL builds get a `-shared` suffix (the SDK packages follow),
    init-wizard's hostname defaults to my.ence.do, --passphrase is
    deprecated — plus six backlog notes: STEP-M12-135, then v1.2.0-rc2
    replaces rc1. This step's scope (1.2.0 + the docs + a published rc)
    is complete as is.
reopened: []
cancelled: null
---

**Goal:** the project reads 1.2.0 everywhere observable (`project(VERSION
1.2.0)`, `ehem_version()`, shared library `libencedo-hem.so.1.2.0` with
SONAME 1 kept, CMake package version, `encedo-hem.pc` `Version`); the
user-facing documentation describes 1.2.0 before anything is tagged —
the checklist of docs/RELEASING.md "Final release" that the M10 gate
missed; an annotated `v1.2.0-rc1` tag (the user tags) publishes a
pre-release carrying every asset of the REQ-BUILD-009 table.

**Notes:** Chore (`implements: []`): the version bump and the docs are
the milestone's, not one REQ's; STEP-M12-140 then verifies. Docs: 
`docs/API-GUIDE.md` — `ehem_build_info_get` + `EHEM_ERR_BACKEND_MISMATCH`
(the docs gate already forces the symbol; the error-model row and the
"backend compatibility" paragraph are prose); `README.md` — Status block
(1.2.0, date, scope, next milestone), hem-tool section (`--version`,
`keys types`, `init-wizard`, exit codes 12 and 6), build section (the
CMake options, `EHEM_CRYPTO_BACKEND`, `./dev build --openssl`),
"Download" (the asset table from STEP-M12-120), the Documentation list
and the Layout (shim split, `tests/consumer`, `tests/support/crypto_ref`);
`docs/COVERAGE.md` — M12 binds no new endpoint: re-check and say so in
its header; `KNOWN-ISSUES.md` — the foreign-libwolfssl crash and the
Windows 0xC0000135 entries become "fixed in 1.2.0: refused with exit 12 /
reported by hem-tool", the "Residual notes" (options.h decides layouts)
stay as the reason the self-test exists; `docs/RELEASING.md` — variants,
both prefixes, attended smoke per variant (STEP-M12-120 started this —
finish it); `tests/README` — the openssl legs, the consumer gate, the
tests-backend-free gate. `tests/unit/exports_baseline_1.0.txt` stays the
1.0 freeze (additions are legal; note the 1.2 addition in REQ-API-008's
record if not already). The release text for rc1 comes from the README
Status block + ARCHITECTURE §11 M12 (RELEASING "Final release"). The user
tags and pushes (`git tag -a v1.2.0-rc1 --cleanup=whitespace -F …`);
record the run id and the asset count in REQ-BUILD-009's tag-path
criterion; delete the rc afterwards or keep it — user's call. CI must be
green on `development` first.

**Definition of done**
- [x] 1.2.0 everywhere observable: CMake project version, `ehem_version()`
      (test_version), SONAME 1 kept, package version file, `.pc` Version;
      `./dev ci` green.
      (See tests: every one checked on the local build; ./dev ci both
      backends green 2026-10-16.)
- [x] docs/API-GUIDE.md: new API indexed and the error model / backend
      compatibility prose added (docs gate green).
      (The index rows and the error-model text came with STEP-M12-010/020;
      now the title reads 1.2, a "Crypto backend" section says what each
      backend's check does — wolfSSL exact version + struct self-test,
      OpenSSL major version + known answers — and the hem-tool section
      names exit 12 and `--version`. check_docs_coverage green.)
- [x] README.md: Status block, hem-tool commands + exit codes, build
      options and backend switch, Download table, Documentation list,
      Layout — all at 1.2.0.
      (Status block: 1.2.0 rc and what is new; Dependencies: wolfSSL or
      OpenSSL; Build: the five M12 options + the static-wolfSSL warning;
      ./dev: --openssl and the switch flags; hem-tool: `init-wizard`,
      `keys types`, `--version`, the exit-code paragraph (0/1/2, the
      per-command codes, 12); Install: the crypto library, the .pc file;
      Documentation: tests/README.md added; Layout: the two shims,
      tests/consumer, crypto_ref; License: OpenSSL in the -static
      packages. Download: STEP-M12-120's table.)
- [x] docs/COVERAGE.md re-checked (no new endpoints — stated);
      KNOWN-ISSUES.md foreign-libwolfssl + 0xC0000135 entries closed as
      fixed in 1.2.0; docs/RELEASING.md and tests/README complete.
      (COVERAGE: "Re-checked for 1.2.0" — `ehem_build_info_get` makes no
      device call. KNOWN-ISSUES: see notes. RELEASING: STEP-M12-120 wrote
      the variants, prefixes and per-variant smoke; the examples now say
      1.2.0. tests/README: a "Build switches and the backend check"
      section — the ten CI legs, the link assertions, the REQ-API-010 seam
      tests and the release jobs' real cases; the package gate on both
      library packages; the integration list brought up to date.)
- [x] CI green on `development` (all legs incl. openssl and the switch
      legs); `workflow_dispatch` release run green with every asset.
      (CI 38903704626: 10/10 legs on 5954950. Release dispatch
      38903755451: both jobs green, the 11 archives + SHA256SUMS in the
      two artifacts — see tests.)
- [x] `v1.2.0-rc1` suggested to the user; the pre-release carries every
      REQ-BUILD-009 asset with annotation + checksums (run id and count
      recorded in REQ-BUILD-009).
      (Run 38904497654 on the user's annotated tag: release 414975403 is
      a pre-release with the 12 assets of the table, the tag message as
      text + the SHA256SUMS block; `sha256sum -c` re-verified in the
      publishing job. Recorded in REQ-BUILD-009's tag-path criterion,
      rev 6. The user tested both hem-tool variants on Linux and Windows
      — see tests.)
