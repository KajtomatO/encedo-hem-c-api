---
id: STEP-M10-070
title: "M10 gate: attended wipe → init → recovery cycle on the dev device; attended-only check; MFW re-check"
milestone: M10
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#9-testing-policy"]
depends_on: ["STEP-M10-030", "STEP-M10-040", "STEP-M10-060", "STEP-M10-065", "STEP-M10-068"]
evidence:
  commits: ["6204c13"]   # the user's commit carrying the attended-cycle records; the gate's own edits (this file, TRACE, ARCHITECTURE §11, KNOWN-ISSUES, REQ-TEST-007) land in the user's next commit
  tests:
    - "M10 GATE 2026-10-10, local, on 6204c13 + the gate edits (no code change since): ./dev ci — gcc 47/47 + clang 47/47 unit; ./dev check — export_symbols + public_headers_dep_free 2/2; ./dev test asan — 47/47 ASan/LSan clean; ./dev test it — integration 23/23 green in 338 s on the original test device (my.ence.do, fw v1.2.2-DIAG), no stall"
    - "REQ-TEST-007: grep for ehem_system_wipeout / ehem_device_init under tests/integration + tests/disruptive → no match"
    - "Attended (user, 2026-10-10): rc4 cycles on Windows + Linux on a second unit (fw v1.2.2) — init, key, sign, recovery (healthy), wipe ×2; gate sitting \"1 & 2 done\" (init with the test passphrase, recovery, status, login; release artifacts smoke-run with the companion library beside each binary)"
  notes: >
    Gate run 2026-10-10. Two physical units were used: the attended
    wipe/init cycles ran on a second unit (fw v1.2.2), whose wipes kept its
    trusted HTTPS certificate; the original test device (fw v1.2.2-DIAG,
    the unit whose 2026-07-22 wipe lost its TLS material) was put back by
    the user for the integration sweep, so `./dev test it` ran on a device
    initialised before M10, not one re-initialised by init-device. Surfaced
    by the cycle and recorded in KNOWN-ISSUES: the release hem-tool crashes
    with a foreign libwolfssl (fix = M12, user decision), and a wipe does
    not always remove the TLS material. MFW unchanged (firmware still
    v1.2.2). §4.3: REQ-TEST-007 approved → verified (its three criteria
    checked here; realized by the absence of live tests + the docs, like
    the other TEST-area REQs).
reopened: []
cancelled: null
---

**Goal:** Milestone gate (chore, `implements: []` — it verifies
REQ-TEST-007 and provides the attended evidence for REQ-SYS-014,
REQ-AUTH-011, REQ-TOOL-021 and REQ-TOOL-022). In one attended sitting on
the dev device: `hem-tool wipe-device --wait` (typed hostname) → device
back over http, uninitialised → `hem-tool --url http://… init-device …`
with the test passphrase (so `hem.env` keeps working) → `hem-tool
recovery` with its verdict recorded (the 2026-10-10 wipes kept the
trusted HTTPS certificate, so the healthy path is expected; tls-recover
only if HTTPS is really gone) → `hem-tool
status` under system trust and a passphrase login succeed. Each
observation is recorded in the owning REQ (date, device, outcome) and
in this step's `evidence.notes`.

**Notes:** **The wipe destroys everything on the device:** all keys, the
TLS material, the logs and the resident phone pairing (the user's phone,
protected-labeled since M8) — re-pair afterwards with `ext pair` if
mobile auth should keep working, or accept its loss. The user decides
at the sitting; nothing here runs unattended. REQ-TEST-007 check: grep
`ehem_system_wipeout` / `ehem_device_init` under tests/integration and
tests/disruptive → nothing. MFW re-check (the M10 decomposition chore):
firmware is still v1.2.2 — confirm with `hem-tool status` and record
"MFW unchanged" (or promote anything the firmware now routes). Then
§4.3 regeneration (the four attended-only REQs transition on their
recorded evidence, REQ-TEST-007's convention), ARCHITECTURE §11 M10
marked gate-passed, KNOWN-ISSUES updated with anything the cycle
surfaced. Commits/tags are the user's.

**Definition of done**
- [x] Attended cycle completed and recorded: wipe (REQ-SYS-014 +
      REQ-TOOL-022 evidence), init (REQ-AUTH-011 + REQ-TOOL-021), recovery
      run and its verdict recorded (REQ-TOOL-023), status + login green.
      *(The four attended REQ records were made 2026-10-10 from the user's
      rc4 Windows + Linux runs; the sitting re-runs the cycle on the gate
      commit's artifacts.)* *(2026-10-10: the user's rc4 cycles on a
      second unit (fw v1.2.2) — Windows: init, key, sign, `recovery`
      healthy, wipe; Linux: "everything works", wipe; then the gate
      sitting, user: "1 & 2 done" (init with the test passphrase,
      recovery, status, login); the original test device (fw
      v1.2.2-DIAG) was put back afterwards for the integration sweep.)*
- [x] REQ-TEST-007: no live test for init/wipe in the tree (grep); the
      attended records are in place. *(grep over tests/integration +
      tests/disruptive → none; records in REQ-AUTH-011/SYS-014/TOOL-021/
      TOOL-022; tests/README.md + ARCHITECTURE §9 name the class.)*
- [x] MFW re-checked against the running firmware; result recorded in
      ARCHITECTURE §11. *(`hem-tool status` 2026-10-10: test device
      "Encedo nGINE FW v1.2.2-DIAG", second unit "FW v1.2.2" — the
      firmware the MFW list was swept against on 2026-08-07; no upgrade,
      MFW unchanged.)*
- [x] Fresh `./dev ci` (gcc + clang) + ASan + gates green; `./dev test it`
      integration sweep green on the re-initialised device. *(2026-10-10:
      47/47 gcc, 47/47 clang, gates 2/2, ASan 47/47, integration 23/23 in
      338 s — on the original test device the user put back, initialised
      before M10; see the notes.)*
- [x] Release artifacts of the gate commit (STEP-M10-065 workflow)
      downloaded — the hem-tool archive and the wolfSSL companion asset
      unpacked side by side — and smoke-run (`hem-tool status`, system
      trust) on Linux and Windows — attended. The licensing decision is
      recorded (REQ-BUILD-005 rev 2, user decision 2026-10-09: wolfSSL
      dynamic, never bundled statically; rev 2 re-approved the same day).
      *(User: "1 & 2 done", 2026-10-10. The newest Release build at the
      time was run 38058607548 (Run workflow on main 339af96); the gate
      commit differs from it only in the init/wipe hint texts and docs —
      the v1.1.0 tag run rebuilds everything, and docs/RELEASING.md
      smoke-tests the published assets once more. Licensing decision
      recorded in REQ-BUILD-005 / ARCHITECTURE §1.)*
- [x] TRACE.md regenerated (§4.3); ARCHITECTURE §11 M10 gate-passed.
      *(2026-10-10.)*
