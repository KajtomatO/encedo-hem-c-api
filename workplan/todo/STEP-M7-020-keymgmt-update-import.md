---
id: STEP-M7-020
title: "ehem_key_update + ehem_key_import — /api/keymgmt/update and /import bindings"
milestone: M7
implements: ["REQ-KEY-007", "REQ-KEY-008"]
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#2-context--constraints"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Both bindings exported, tagged `implements: REQ-KEY-007` /
      `REQ-KEY-008`; header docs carry the label-required and
      public-only rules
- [ ] Unit tests green (gcc+clang+asan): body bytes, `EHEM_ERR_ARG`
      pre-validation with zero I/O, 400/403/406 mapping, kid parse
- [ ] Live: create→update→get→search round-trip; import X25519 →
      ecdh(ext_kid) == shim secret byte-exact; dedup 406; type-support
      probe run and recorded in REQ-KEY-008 (open criteria resolved);
      cleanup clean
- [ ] REQ-KEY-007 open criteria (unknown-kid 406, label-required)
      resolved from the live probes
- [ ] Export/header gates green; MinGW cross-syntax check run
