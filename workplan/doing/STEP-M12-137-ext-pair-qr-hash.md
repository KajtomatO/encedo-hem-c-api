---
id: STEP-M12-137
title: "ext pair QR hash — the SDK composes the pairing QR payload (ehem_ext_qr_payload: hash = base64(SHA-256(request)), the check the new Encedo Auth app makes); v1.2.0-rc3"
milestone: M12
implements: ["REQ-AUTH-008", "REQ-TOOL-016"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-135"]
evidence:
  commits: []
  tests: []
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

**Definition of done**
- [ ] `ehem_ext_qr_payload()` exported from libencedo-hem on both backends,
      documented in `include/ehem/auth.h`, indexed in docs/API-GUIDE.md;
      the export-baseline and docs-coverage gates green.
- [ ] `ext pair` composes its payload with it — `--no-qr` prints the
      Manager-identical JSON (`{link, hash, user, email, hostname}`,
      `hash` = base64(SHA-256(request))); the tool's own JSON code is gone.
- [ ] Unit: test_notify (the FIPS 180-2 "abc" known answer in base64, key
      order, `/` unescaped, `"`/`\` escaped, NULL → `""`, the ARG cases)
      and test_ext_tool (the printed payload for the scripted request,
      verbatim); `./dev ci` and `./dev ci --openssl` green on gcc + clang,
      ASan on both backends.
- [ ] REQ-AUTH-008 rev 5 and REQ-TOOL-016 rev 3 (the §6.2 changes),
      REQ-API-008 annotation (1.2.0 additions), ARCHITECTURE §5/§8/§11,
      KNOWN-ISSUES "FIXED in 1.2.0" entry, release notes (rc3 + final).
- [ ] CI green on `development`; `v1.2.0-rc3` published with the 12 assets
      (run id + release id in REQ-BUILD-009's tag-path record — no meaning
      change).
- [ ] Attended (the user): `ext pair` from the published rc3 hem-tool,
      scanned by the new Encedo Auth app → paired — recorded in
      REQ-TOOL-016 and REQ-AUTH-008; both back to `verified`; TRACE
      regenerated.
