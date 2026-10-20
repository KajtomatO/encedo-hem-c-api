---
id: STEP-M12-137
title: "ext pair QR hash — the SDK composes the pairing QR payload (ehem_ext_qr_payload: hash = base64(SHA-256(request)), the check the new Encedo Auth app makes); v1.2.0-rc3 — and, after the rc3 tests, the Windows console render (UTF-16 via WriteConsoleW); v1.2.0-rc4"
milestone: M12
implements: ["REQ-AUTH-008", "REQ-TOOL-016"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-135"]
evidence:
  commits: ["b25b87b", "f7b875b", "d532433", "5b4e1bc"]   # start, implementation (the hash), TRACE, the Windows console render (+ rc3 records); the TRACE commit follows
  tests:
    - "Local 2026-10-20 (scratchpad ci137.log, ci137-rerun.log): ./dev ci 56/56 gcc + 56/56 clang (wolfSSL; the first run of the sitting exited 1 with 56/56 in its visible tail while a `./dev tool` build ran in the same build/ directory concurrently — re-run alone: exit 0, both compilers green); ./dev ci --openssl 56/56 gcc + 56/56 clang; ./dev test asan 56/56 and ./dev test asan --openssl 56/56; ./dev check 2/2. New assertions — test_notify `test_qr_payload` (verifies: REQ-AUTH-008 rev 5; runs first, before any context — no ehem_global_init): the exact payload for request \"abc\" with the FIPS 180-2 known answer `ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=` (key order, compactness, the bare `/`), \"r.j.w\" → `BQixETmS1vjcSjAPqjT0yq9poZjfeTnWczNU6VHYuI4=` with `\"`/`\\` in user escaped and NULL email/hostname → \"\", the three ARG cases; test_ext_tool (verifies: REQ-TOOL-016 rev 3): the --no-qr print is that SDK payload for the scripted request, verbatim on its own line, and `not_implemented_yet` appears nowhere. Gates: export_symbols (the new symbol is an addition beyond the 1.0 baseline), docs_coverage (API-GUIDE row), public_headers_dep_free, tests_backend_free, ascii_strings — green."
    - "Live shape check 2026-10-20 (dev build, wolfSSL, against the dev device — scratchpad extpair137.log): `hem-tool ext pair --no-qr` printed `{\"link\":\"https://api.encedo.com/notify/register/challenge/<64 chars>\",\"hash\":\"<44-char standard base64>\",\"user\":\"Debug HSM\",\"email\":\"\",\"hostname\":\"my.ence.do\"}` — field for field the user's decoded Manager reference QR of the same device (only link and hash differ, as they must); exit 3 after the 60 s window, the registration left to expire broker-side. The phone-side check itself is the user's rc3 scan (box 6)."
    - "rc3 published 2026-10-20: CI run 39401189070 on development (d532433) and 39401247238 on the tag, 10/10 legs each; release run 39401247243 (Linux 2:26, Windows 9:07, publish 0:15) → pre-release 418234162 with the 12 assets under the final names, SHA256SUMS = those 11 archives (read back through the API) — recorded under REQ-BUILD-009's tag-path criterion (rev 9)."
    - "Windows console render (the rc3 finding), 2026-10-20: `x86_64-w64-mingw32-gcc -std=c99 -Wall -Wextra -Werror -fsyntax-only src/tools/hem-tool/ext_cmd.c` clean (GCC 13 cross; the Windows CI legs compile with MSYS2 GCC 16 and are the check for newer warnings); host rebuild + test_ext_tool, export_symbols, docs_coverage, ascii_strings, public-header gates 5/5 — the byte path's output is unchanged (test_qr_render's 29 × 3-byte first row). The console path cannot run here (no Windows, no Wine): proven by the user's rc4 Windows run (REQ-TOOL-016 rev 4 attended criterion). Local suite after the change (scratchpad ci137b.log, ci137b-openssl.log, asan137b.log): ./dev ci 56/56 gcc + 56/56 clang, exit 0; ./dev ci --openssl 56/56 gcc + 56/56 clang, exit 0; ./dev test asan (wolfSSL) 56/56, exit 0."
  notes: null
reopened: []
cancelled: null
---

**Goal:** the QR `hem-tool ext pair` renders is accepted by the new Encedo
Auth app. The app fetches the payload's `link`, hashes the `request` the
broker serves and compares it with the payload's `hash`; hem-tool sent the
literal `"not_implemented_yet"` (the hem-api-tester shape, REQ-TOOL-016
rev 2 / REQ-AUTH-008 rev 4: "until upstream defines it"), so the app
refuses with "the pairing link served a different request than the code
promised" (the user, testing `v1.2.0-rc2`, 2026-10-20). The Manager puts
standard base64 of SHA-256 over the `request` string from `ext/init`
there (`encedo-manager/assets/build.js:4827-4836`, `pairdeviceNow`), as
does hem-sdk-js (`hem-sdk.js:1510-1518`, its commit a750037). The SDK
gains a public `ehem_ext_qr_payload()` that composes the Manager-identical
JSON — keys `link, hash, user, email, hostname` in that order, compact —
and hem-tool uses it; `v1.2.0-rc3` replaces rc2.

**Notes:** user decision 2026-10-20: the SDK composes the payload (not a
tool-local SHA-256, not a hash-only export) — hem-tool uses the public
API only and the `-shared` hem-tool cannot reach the hidden
`ehem_sha256`; the shim has SHA-256 on both backends; hem-sdk-js keeps
the payload in its SDK too. An exported addition is the minor-version
change REQ-API-008 permits (`check_exports.cmake` accepts symbols beyond
the 1.0 baseline); REQ-API-007's `docs_coverage` gate wants the
API-GUIDE row. A new step rather than a reopening of STEP-M8-070: the
placeholder matched its REQ at the time (the old app never checked —
attended pairing 2026-08-05); the definition arrived later (the 135
reasoning). §6.2 report approved with the plan 2026-10-20: REQ-AUTH-008
rev 5 and REQ-TOOL-016 rev 3 are meaning changes (`needs-reverify` until
the unit tests pass and the user's scan); REQ-AUTH-006 untouched (the
hash is over the opaque `request` string). The user's reference QR
(`tmp/r120-rc/reference-qr-code.png`, decoded 2026-10-20) is
`{"link":"https://api.encedo.com/notify/register/challenge/<64>","hash":"<44-char
standard base64>","user":"Debug HSM","email":"","hostname":"my.ence.do"}`
— the shape the SDK now produces. The dev-host `ext pair --no-qr` run
leaves a registration dangling at the broker (it expires server-side, as
the disruptive probe's does). WIP: STEP-M12-140 stays in doing (limit 2);
the gate's attended checks move to the rc3 binaries — the SDK and the
tool both change. Tagging/pushing is the user's.

**Scope extended 2026-10-20, after the user's rc3 tests** ("one bug: on
windows the QR code is a mess of chars and not a QR-Code"): the render
writes UTF-8 half-block cells through stdio, and a Windows console
decodes stdout bytes in its OUTPUT code page — the OEM page of the
system locale (437, 852, …), never UTF-8 unless `chcp 65001` was run —
so each three-byte cell became three letters of that page. Folded into
this step rather than a third one (WIP 2 with the gate; the step's goal
— a QR the new app accepts — is unmet on Windows without it): when
`out` is a console, `hem_ext_qr_render` writes the rows as UTF-16
through `WriteConsoleW` (code-page-free; the legacy raster fonts have
the three blocks too), a file or pipe still gets the UTF-8 bytes (the
unit test's path, unchanged). REQ-TOOL-016 rev 4 is an annotation plus
an attended criterion, no meaning change (the clause promised a QR in
the terminal; Windows showed none) — reported in chat. `v1.2.0-rc4`
replaces rc3; the Windows attended checks of the gate run on rc4's
binaries (the Linux binaries' behaviour is unchanged between rc3 and
rc4 — Windows-only code — so the user's rc3 Linux results stand, at
their discretion per RELEASING). BACKLOG item 12 records the wider
Windows code-page question (device strings) this surfaced.

**Definition of done**
- [x] `ehem_ext_qr_payload()` exported from libencedo-hem on both backends,
      documented in `include/ehem/auth.h`, indexed in docs/API-GUIDE.md;
      the export-baseline and docs-coverage gates green.
- [x] `ext pair` composes its payload with it — `--no-qr` prints the
      Manager-identical JSON (`{link, hash, user, email, hostname}`,
      `hash` = base64(SHA-256(request))); the tool's own JSON code is gone.
- [x] Unit: test_notify (the FIPS 180-2 "abc" known answer in base64, key
      order, `/` unescaped, `"`/`\` escaped, NULL → `""`, the ARG cases)
      and test_ext_tool (the printed payload for the scripted request,
      verbatim); `./dev ci` and `./dev ci --openssl` green on gcc + clang,
      ASan on both backends.
- [x] REQ-AUTH-008 rev 5 and REQ-TOOL-016 rev 3 (the §6.2 changes),
      REQ-API-008 annotation (1.2.0 additions), ARCHITECTURE §5/§8/§11,
      KNOWN-ISSUES "FIXED in 1.2.0" entry, release notes (rc3 + final —
      scratchpad `release-notes-1.2.0-rc3.md`, `release-notes-1.2.0.md`).
- [x] CI green on `development`; `v1.2.0-rc3` published with the 12 assets
      (run id + release id in REQ-BUILD-009's tag-path record — no meaning
      change). *(2026-10-20: CI 39401189070 + 39401247238, release run
      39401247243, release 418234162, 12 assets — REQ-BUILD-009 rev 9.)*
- [x] Windows console render (the rc3 finding): `hem_ext_qr_render`
      writes the rows as UTF-16 through `WriteConsoleW` when `out` is a
      console, UTF-8 bytes otherwise; MinGW cross-syntax clean; the unit
      test's byte path unchanged; REQ-TOOL-016 rev 4 (annotation +
      attended criterion), KNOWN-ISSUES "FIXED in 1.2.0" entry,
      ARCHITECTURE §8/§11, BACKLOG item 12, release notes (rc4 + final).
- [ ] CI green on `development` again; `v1.2.0-rc4` published with the 12
      assets (run id + release id in REQ-BUILD-009's tag-path record — no
      meaning change).
- [ ] Attended (the user): `ext pair` from the published rc3 hem-tool on
      Linux, scanned by the new Encedo Auth app → paired (the user's
      2026-10-20 report "Finished tests on RC" — the record's app/phone
      and kid still to be supplied); `ext pair` from the published rc4
      Windows hem-tool shows a QR in the console and pairs — recorded in
      REQ-TOOL-016 and REQ-AUTH-008; both back to `verified`; TRACE
      regenerated.
