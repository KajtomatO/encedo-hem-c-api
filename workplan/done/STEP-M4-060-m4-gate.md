---
id: STEP-M4-060
title: "M4 gate — live sign → local wolfCrypt verify; §12 risk 3 closed; trace regen"
milestone: M4
implements: []
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#12-risks--open-questions"]
depends_on: ["STEP-M4-010", "STEP-M4-020", "STEP-M4-030", "STEP-M4-040", "STEP-M4-050"]
evidence:
  commits:
    - "(this commit) — M4 gate: §12 risk 3 RESOLVED, REQ records, TRACE regen"
  tests:
    - "gate run 2026-07-16: ./dev test it 10/10 GREEN incl. test_sign_live (the gate criterion: device signatures verify locally — ED25519 64B raw + SECP256R1 71B DER vs the 33B compressed pubkey; scope probe keymgmt:get → 403); unit 21/21 gcc+clang + asan clean; GitHub CI green on linux + windows-mingw (user-confirmed post-push)"
  notes: >
    Gate criterion (ARCHITECTURE §11 M4) met live: signatures produced via
    the SDK verify locally with wolfCrypt (test_sign_live, both families);
    independently cross-proven with OPENSSL in the M4-050 tool demo
    (hem-tool sign → keys pub --raw → pkeyutl / dgst verify OK), incl.
    classifier size cross-check (32/33-byte pubkeys, 71 ≤ 72 DER max).
    Bookkeeping done at this gate: (1) ARCHITECTURE §12 risk 3 marked
    RESOLVED (sign = exact strcmp keymgmt:use:<kid>, sub!=M; broader
    scopes 403; one cached token per KID serves get+sign). (2) REQ-OPS-001
    / REQ-KEY-006 / REQ-TOOL-007 / REQ-TOOL-008 acceptance boxes recorded;
    KEY-006 keeps ONE deliberate open criterion (AES/HMAC/MLKEM/MLDSA live
    flag-set vocabulary — first creatable at M5); new live vocabulary
    observations recorded (ATT,PKEY,ExDSA,ED25519 / ATT,PKEY,ExDSA,
    SECP256R1 on created keys). (3) MSYS2 wolfSSL 5.9.2-2 finding recorded
    in STEP-M4-020 (options.h lacks HAVE_COMP_KEY → compressed-point
    verify is Linux-only; windows-mingw green by the UNSUPPORTED-mapping
    design). (4) TRACE.md regenerated per §4.3 — 4 transitions
    (OPS-001, KEY-006, TOOL-007, TOOL-008 approved → verified); 42 REQs =
    38 verified / 4 implemented / 0 approved. M4 COMPLETE.
reopened: []
cancelled: null
---

**Goal:** The M4 milestone gate demonstrated live against the dev device:
create EHEMTEST SECP256R1 (mode ExDSA) + ED25519 keys → sign via the SDK
→ signatures verify locally with wolfCrypt → keys deleted; the tool path
demonstrated by hand (`hem-tool sign` piped/checked against
`keys pub --raw` material); classifier size constants cross-checked
against the live material. Bookkeeping: §12 risk 3 marked RESOLVED with
the recorded scope-probe facts; REQ open criteria recorded; TRACE.md
regenerated per §4.3.

**Notes:** Chore step (implements: []) — gate + trace regen, mirrors
STEP-M2-070/M3-070. Gate criterion per ARCHITECTURE §11 M4: "signature
produced via the SDK verifies locally with wolfCrypt". Integration suite
must be fully green (`./dev test it`), unit suites green on gcc/clang +
ASan; Windows CI green. Record any device/doc divergences found on the
way in the affected REQs (device > doc).

**Definition of done**
- [x] Live gate run green: create → sign → local verify → delete for both
      families; `hem-tool sign` + `keys pub` demo performed and noted in
      evidence. (test_sign_live in the 10/10 gate run; tool demo with
      OpenSSL cross-verification in M4-050 evidence.)
- [x] ARCHITECTURE §12 risk 3 rewritten RESOLVED (exact per-KID scope for
      crypto ops; get/sign token sharing; probe results).
- [x] All REQ-OPS-001 / REQ-KEY-006 / REQ-TOOL-007 / REQ-TOOL-008 open
      criteria recorded (checked or explicitly deferred with reason —
      KEY-006's per-family vocabulary criterion stays open by design
      until M5 creates those families).
- [x] TRACE.md regenerated (§4.3); coverage report clean of new
      violations; summary reported in chat. (4 transitions → 38 verified /
      4 implemented / 0 approved of 42.)
