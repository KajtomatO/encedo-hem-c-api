---
id: REQ-KEY-006
title: Client-side key-type classifier — typed metadata from device type strings
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §11 (M4: "key-type metadata exposed so consumers can build local length tables"); HEM-OP-2, HEM-OBJ-5 (consumer mapping table); encedo-hem-api-doc keymgmt/list.md + get.md (type vocabulary); live my.ence.do fw v1.2.2-DIAG 2026-07-16 (flag-set form, STEP-M3-010); encedo_firmware api_keymgmt.c (per-family type emission); approved 2026-07-16
depends_on: ["REQ-KEY-001", "REQ-KEY-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#6-protocol-bindings"]
---

# Client-side key-type classifier — typed metadata from device type strings

The SDK SHALL provide a pure, network-free classifier
`ehem_key_type_parse(type_str, &out)` that decomposes a device `type`
string into typed metadata: algorithm family (enum), mode flags
(`ExDSA`/`ECDH`), role flags (`ATT`/`PKEY`/`CERT`/`GENERIC_DER`), and —
for families with fixed geometry — size/format facts: public-key wire
encoding and length as returned by `ehem_key_get` (REQ-KEY-003), signature
wire format (DER-variable vs. raw-fixed) and maximum signature length as
returned by `ehem_sign` (REQ-OPS-001).

Input comes in two shapes, both handled:

- **bare algorithm name** — the doc's `list.md`/`get.md` form and the
  `get` wire reality (`SECP256R1/384R1/521R1/256K1`, `ED25519`, `ED448`,
  `CURVE25519`, `CURVE448`, `CERT`, `DER_PKEY`, AES/HMAC/`MLKEM*`/`MLDSA*`
  names per the REQ-KEY-005 vocabulary);
- **comma-separated flag set** — the live `list`/`search` form on real
  devices (observed 2026-07-16: `ATT,PKEY,ECDH,ExDSA,SECP256R1`,
  `PKEY,GENERIC_DER`, `CERT,GENERIC_DER`, `ECDH,CURVE25519`), where
  role/mode tokens accompany the algorithm token.

Tolerant parsing (ARCHITECTURE §6): unrecognized tokens are skipped
without error (firmware may add flags); a string with no recognizable
algorithm token classifies as family *unknown* with whatever flags were
recognized — never a hard failure. The classifier allocates nothing and
performs no I/O, so consumers can run it per list entry for free
(HEM-OP-2's zero-round-trip length table; HEM-OBJ-5's attribute mapping).

**Rationale:** M4 milestone item. The PKCS#11 consumer must answer length
queries locally (HEM-OP-2) and map search records to attributes
(HEM-OBJ-5: `CKA_KEY_TYPE`, `CKA_SIGN` from ExDSA, `CKA_DERIVE` from
ECDH) without re-deriving the device's string vocabulary itself.
REQ-KEY-001 deliberately preserves `type` verbatim; this REQ is the typed
view on top. Signature sizes are wire-format sizes (DER for ECDSA per
REQ-OPS-001) — fixed r‖s sizes after conversion are the consumer's table.

**Acceptance criteria:**
- [ ] Every documented bare algorithm name and all four live-observed
      flag-set strings classify to the correct family, modes, and roles
      (unit tests, no fake transport needed).
- [ ] Unknown tokens (e.g. a fabricated `NEWFLAG,ED25519`) are skipped:
      classification succeeds with the known tokens honored; a string with
      no algorithm token yields family unknown without error (unit tests).
- [ ] Size/format metadata matches the wire reality for the asymmetric
      families: SECP256R1/384R1/521R1/256K1 → compressed-x963 pubkey
      (1+⌈bits/8⌉ bytes), DER signature with documented per-curve maximum;
      ED25519 → 32-byte pubkey, raw 64-byte signature; ED448 → 57-byte
      pubkey, raw 114-byte signature; CURVE25519/448 → 32/56-byte pubkey,
      no signature (unit tests against the constants; live cross-check at
      the M4 gate for the families the dev device creates).
- [ ] OPEN (live, expected at M5's per-family matrix): record the live
      flag-set strings for families not yet observed (AES, HMAC, ML-KEM,
      ML-DSA) here; the tolerant-parsing rule means recording them must
      require no parser change beyond token additions.
