---
id: STEP-M12-140
title: "M12 gate — every variant built and asserted in CI, both backends green, attended per-variant status, foreign-library refusal demo, attended wizard, docs checklist, TRACE regen, the 1.2.0 release"
milestone: M12
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-130", "STEP-M12-135", "STEP-M12-137"]
evidence:
  commits: ["67ffd14"]   # start; the evidence commits follow
  tests:
    - "CI (box 1), 2026-10-19: ci.yml run 39320511927 on development (8dd915f) — all 10 legs green: Linux gcc, clang, gcc OpenSSL, clang OpenSSL, static-only, shared-only (hem-tool on the shared SDK); Windows MinGW, OpenSSL, static-only, shared-only. The rc tag's own CI run 39320626785: 10/10. Release run 39320626797 (the user's annotated v1.2.0-rc2 on 8dd915f): both build jobs green with every package step — the dynamic variants (package.sh, package-wolfssl.sh, package-lib.sh: link-mode, delay-load, no-DLL and drop-in smoke assertions), the OpenSSL variants (package-static.sh, package-lib.sh static: glibc-only / system-DLL-only imports, no wolfSSL symbol or string, the null-provider OPENSSL_CONF control, the static consumer gate), both Linux classifier steps; the publishing job's assemble (sha256sum -c, duplicate-name check) and publish green; release 417736562 with the 12 assets of REQ-BUILD-009 rev 8."
    - "CI (box 1) for the replacement candidates, 2026-10-20 (STEP-M12-137's two fixes): ci.yml 39401189070 on development (d532433, rc3) and 39439574020 (a481d07, rc4) — 10/10 legs each; the tags' own CI runs 39401247238 and 39439585486 — 10/10 each; release runs 39401247243 (rc3: Linux 2:26, Windows 9:07, publish 0:15) and 39439585466 (rc4: Linux 3:17, Windows 13:41, publish 0:17) — every build, package, smoke and classifier step green, the publishing job's assemble and publish green; pre-releases 418234162 (rc3) and 418511519 (rc4), 12 assets each under the final names, SHA256SUMS read back (11 archives) — REQ-BUILD-009 rev 9 / rev 10. rc4 (a481d07) is the tested candidate: the Linux binaries' behaviour is unchanged since rc3 (the rc4 diff is Windows-only console code), the Windows ones carry the QR render fix."
    - "Linux checks on the PUBLISHED rc2 binaries, 2026-10-19 (run on the dev host from the release assets, checksums verified against SHA256SUMS; the transcript reported to the user): one-file hem-tool-1.2.0-rc2-linux-x86_64 — ldd glibc only, no RUNPATH, --version 'OpenSSL 3.5.9 (compiled) / 3.5.9 (loaded), static' — `env -i ./hem-tool --url https://my.ence.do status` from a directory with no other file → device data, exit 0 (REQ-BUILD-009 attended criterion, Linux half). hem-tool-1.2.0-rc2-linux-x86_64-shared ALONE on this host (system libwolfssl.so.42 = 5.6.6) → --version '5.7.2 (compiled) / 5.6.6 (loaded)', `status` → exit 12, empty stdout, the REQ-TOOL-024 message naming the companion asset and the one-file asset, no crash (REQ-API-010 / REQ-TOOL-024 gate criteria); with the companion libwolfssl.so.42 beside it → resolved from $ORIGIN, 5.7.2 loaded, `status` exit 0. init-wizard on the one-file binary shows `hostname [my.ence.do]:` and cancels with exit 6 at end of input; `--passphrase x keys types` → exit 0 and one warning line; `--help` is 45 lines."
    - "Local gate suite (box 2), 2026-10-19 23:02–23:10 on ce1dddb's tree (log: scratchpad gate-local.log): ./dev ci 56/56 gcc + 56/56 clang (wolfSSL); ./dev ci --openssl 56/56 gcc + 56/56 clang; ./dev test asan 56/56 and ./dev test asan --openssl 56/56 (ASan/LSan clean); ./dev check 2/2 (export symbols, public headers). ./dev test it on the wolfSSL gcc build: 23/23 passed in 424.74 s — test_sign_live needed the third attempt of ctest's --repeat until-pass:3 (the dev helper's stall mitigation): attempts 1 and 2 hit EHEM_ERR_UNREACHABLE / EHEM_ERR_NETWORK on /api/auth/token ('Failed to connect to my.ence.do port 443', 'SSL connection timeout') for about a minute — the KNOWN-ISSUES sustained-load stall; the device recovered on its own, no power-cycle; the eight later tests passed first time. Device left clean afterwards: `hem-tool keys list` → 5 keys, the 3 protected ones (TLS PrivateKey, the paired phone, TLS Certificate) intact, 0 EHEMTEST labels (the key whose cleanup failed during the stalled attempt is gone)."
    - "Live sweep RE-RUN on the final code (box 2; HEAD df94d65 = rc4's a481d07 plus records), 2026-10-27 13:20–13:29 (log: scratchpad it140b.log): ./dev test it on the wolfSSL gcc build — 14/23 passed first time (keymgmt, update/import, wrap, notify session, ext pair/login live, logger, derive …), then the KNOWN-ISSUES sustained-load stall at test_sign_live, as on 2026-10-19: EHEM_ERR_UNREACHABLE on /api/auth/token ('Failed to connect to my.ence.do port 443 … Timeout was reached', then 'Couldn't connect to server'), all three --repeat attempts; the eight tests after it failed the same way (9 failed, exit 8). Unlike 2026-10-19 the device did NOT recover on its own: at 13:37 the USB network link (192.168.7.1 on enx0050c29af800) shows ARP INCOMPLETE, no ping, ports 443 and 80 closed — the device is off the link (hung hard, unplugged, or moved for the user's Windows test). One EHEMTEST key may be left: e6e5ee7eceadcde387aba90b594964f9 (its cleanup failed in the stalled attempt) — `hem-tool keys rm --all` for the EHEMTEST set once the device is back, and the sweep re-run (the box stays ticked on the 2026-10-19 23/23 run; the re-run's result is recorded here either way)."
  notes: null
reopened: []
cancelled: null
---

**Goal:** 1.2.0 ships: every published variant built in CI with its link
mode asserted and the unit suite green on both backends; attended
`hem-tool status` under system trust for each of the four published
hem-tool binaries (the one-file and the `-shared`, Linux + Windows); the
foreign-library refusal demonstrated — the release dynamic Linux hem-tool
without its companion library, on the dev host whose `libwolfssl.so.42`
is 5.6.6, prints the REQ-TOOL-024 message and exits 12 instead of
crashing; the Windows missing-DLL message shown live (DLL removed, then
restored); an attended `init-wizard` run end to end on the second unit
(Linux and Windows consoles, echo-off included) followed by `status` +
passphrase login; every attended criterion of REQ-API-010, REQ-TOOL-024,
REQ-TOOL-026, REQ-TOOL-028 and REQ-BUILD-009 recorded in its REQ; the
docs checklist re-verified; TRACE regenerated with all M12 REQs
transitioned; ARCHITECTURE §11 M12 marked gate-passed; the release per
docs/RELEASING.md "Final release" (merge, `v1.2.0` tag — the user tags).

**Notes:** Milestone gate (chore, `implements: []`). Attended parts in
one sitting with the user: (1) the four binaries from the rc4
pre-release (the final names, STEP-M12-135; rc3 replaced rc2 after the
user found the `ext pair` QR rejected by the new Encedo Auth app, and
rc4 replaces rc3 after the user's rc3 Windows test found that QR
rendered as mojibake in the console — both STEP-M12-137, 2026-10-20 —
so the Windows attended checks run on rc4's binaries; the Linux
binaries' behaviour is unchanged between rc3 and rc4 (Windows-only
code), so the user's rc3 Linux results stand at their discretion per
RELEASING; the rc2 Linux checks below stay as dated records), each `status` against the
dev device under system trust (my.ence.do, public-CA cert), the one-file
ones from a directory with NO companion library; (2) the refusal demo on this host (`env -i
./hem-tool status` with the companion library absent → exit 12, the
message naming wolfSSL 5.7.2 compiled / 5.6.6 loaded — the exact
KNOWN-ISSUES scenario; the dev host is the only place with a foreign
same-SONAME library at hand); (3) Windows: the published zip, DLL removed
→ the message; DLL restored → `status` works; (4) the wizard on the second
unit — it must be uninitialised: a `wipe-device` first destroys that
unit's keys and any pairing, the user decides at the sitting (REQ-TEST-007
attended-only; the M10 gate did the same cycle); then `status` + login
with the chosen passphrase; (5) the attended tty echo-restoration check
on both consoles (REQ-TOOL-028). CI evidence: the `ci.yml` run on
`development` (all legs: default, switch legs, openssl Linux + Windows)
and the release run of the rc tag (every variant, every assertion) —
read from the Actions API, run ids recorded. Fresh `./dev ci` gcc + clang
+ ASan + gates on both backends locally; full `./dev test it` on the
wolfSSL build (the OpenSSL live sweep was STEP-M12-090's — repeat only if
the shim changed since) — the KNOWN sustained-load stall may need a
power-cycle; device left clean (0 EHEMTEST, protected set intact). Docs
checklist (REQUIREMENTS-MANAGEMENT.md §5.3 / RELEASING "Final release"):
README Status block (version, date, scope, next milestone), new commands
and APIs, Documentation list, Layout; docs/COVERAGE.md; KNOWN-ISSUES.md —
re-read against the shipped behaviour, since the M10 gate's miss is why
this box exists. TRACE (§4.3): REQ-API-010/011, REQ-BUILD-006..009,
REQ-TOOL-024..028 → implemented/verified per evidence (BUILD-006/008/009
may stay `implemented` like BUILD-002/005 — proven by workflow runs — if
no `verifies:` test exists; say so); REQ-NET-005 back to verified;
upstream-filing pile and BACKLOG.md reviewed (macOS, MSVC candidates
stay). Tagging/committing/pushing is the user's (feedback: never commit
without asking).

**Definition of done**
- [x] CI: `ci.yml` green on `development` with every leg (default, switch
      legs, openssl Linux + Windows); the rc release run green with every
      variant's link-mode / no-wolfSSL / smoke assertions (run ids
      recorded).
- [x] Fresh local `./dev ci` gcc + clang + ASan + gates green on both
      backends; `./dev test it` green on the wolfSSL build; device clean.
- [ ] Attended: `status` under system trust for all four published
      hem-tool binaries (the one-file ones without a companion library) —
      recorded in REQ-BUILD-009.
- [x] Attended: foreign-library refusal demo on this host (release dynamic
      Linux hem-tool, no companion, system 5.6.6 → message, exit 12, no
      crash) — recorded in REQ-API-010 and REQ-TOOL-024.
- [ ] Attended: Windows missing-DLL message (zip without DLL → message +
      exit 12; DLL restored → works) — recorded in REQ-TOOL-026.
- [ ] Attended: `init-wizard` end to end on the second unit on Linux and
      Windows (echo-off and restoration checked), then `status` + login —
      recorded in REQ-TOOL-028.
- [x] Attended: `ext pair` from the published rc3 hem-tool (Linux) scanned
      by the new Encedo Auth app → paired, and from the published rc4
      hem-tool on Windows the QR renders in the console and pairs (the
      STEP-M12-137 fixes) — recorded in REQ-TOOL-016 and REQ-AUTH-008.
      *(2026-10-20 / 2026-10-27: the user — rc3 tests finished with the
      Windows display as the only finding; "Ext pair works on windows"
      from rc4; Encedo HEM Auth 2.0.0-dev.8, Samsung SM-S938B (Android),
      kid 3dff0e4888f77b1e14e6812c17dbc4ce listed `[PROTECTED]`.)*
- [ ] Docs checklist re-verified against the shipped behaviour: README
      Status block / commands / APIs / Documentation / Layout,
      docs/COVERAGE.md, KNOWN-ISSUES.md, docs/RELEASING.md.
- [ ] TRACE.md regenerated (§4.3): all M12 REQs transitioned, REQ-NET-005
      verified again, no orphans/broken anchors, zero deliberately open
      criteria or each one named with its reason.
- [ ] ARCHITECTURE §11 M12 marked gate-passed; KNOWN-ISSUES and BACKLOG
      reviewed; `v1.2.0` release per RELEASING "Final release" suggested
      to the user (merge `development` → `main`, annotated tag — user tags).
