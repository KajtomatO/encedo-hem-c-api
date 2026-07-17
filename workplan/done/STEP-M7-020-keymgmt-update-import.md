---
id: STEP-M7-020
title: "ehem_key_update + ehem_key_import — /api/keymgmt/update and /import bindings"
milestone: M7
implements: ["REQ-KEY-007", "REQ-KEY-008"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#2-context--constraints"]
depends_on: []
evidence:
  commits: ["554d509"]
  tests: ["verifies: REQ-KEY-007/REQ-KEY-008 (tests/unit/test_keymgmt.c — 8 new cases; tests/integration/test_update_import_live.c — 5 live cases green 2026-07-18)"]
  notes: >
    Unit 29/29 gcc+clang + ASan clean; export/header gates green; MinGW
    cross-syntax OK. ALL FOUR probes RESOLVED live (my.ence.do fw
    v1.2.2-DIAG): (1) update unknown-kid → 406 → NOT_FOUND; (2) descr-only
    body → 400 (label required — driven via the internal request path);
    (3) duplicate import → 406 with EMPTY payload (dedup, python finding
    confirmed); (4) type support: SECP256R1 compressed 33B + ED25519 32B +
    MLKEM512 800B ALL ACCEPTED — the 70-byte cap is confirmed dead code,
    and the repo does not validate ML-KEM material. NEW FINDING (device >
    doc, REQ-KEY-007 rev2): update is a WHOLE-RECORD REWRITE — a label-only
    update CLEARS the stored descr (doc's "left untouched" is wrong;
    keep-descr requires read-then-resend; pinned in the live test; upstream
    doc-repo filing candidate). Import X25519 → ecdh(ext_kid) matched the
    shim secret byte-exact; imported pubkey readback byte-identical.
    add_b64_field helper factored out (create/update/import share it).
reopened: []
cancelled: null
---

**Goal:** `ehem_key_update` (label required, descr optional, empty-200
success, 406→NOT_FOUND) and `ehem_key_import` (public-key import →
`{"kid"}`) in proto_keymgmt.c + keymgmt.h, unit- and live-verified
including the REQ-KEY-008 type-support probe.

**Notes:** Both are small POSTs following the create-binding patterns
(label/descr validation already exists — reuse, don't duplicate).
Import probe order per REQ-KEY-008: X25519 (shim keypair → ecdh
ext_kid byte-exact vs shim — the strong proof), SECP256R1 with the
compressed-x963 bytes `ehem_key_get` exports, ED25519, then MLKEM512
(800-byte pubkey — tests the dead 70-byte cap). Record accepted/rejected
+ flag-sets in the REQ. Dedup probe: import the same pubkey twice → 406.
Update probes: unknown-KID 406 mapping; label-omitted 400 (drive via
raw request path in the probe, not the public API — the API always
sends label). EHEMTEST hygiene + cleanup per REQ-TEST-003; transient
retry helpers from M5-020 for flaky-device resilience.

**Definition of done**
- [x] Both bindings exported, tagged `implements: REQ-KEY-007` /
      `REQ-KEY-008`; header docs carry the label-required and
      public-only rules (+ the whole-record-rewrite warning)
- [x] Unit tests green (gcc+clang+asan): body bytes, `EHEM_ERR_ARG`
      pre-validation with zero I/O, 400/403/406 mapping, kid parse
- [x] Live: create→update→list/search round-trip (incl. the descr-clear
      pin); import X25519 → ecdh(ext_kid) == shim secret byte-exact;
      dedup 406; type-support probe run and recorded in REQ-KEY-008
      (open criteria resolved); cleanup clean
- [x] REQ-KEY-007 open criteria (unknown-kid 406, label-required)
      resolved from the live probes (+ rev2 whole-record finding)
- [x] Export/header gates green; MinGW cross-syntax check run
