---
id: STEP-M4-010
title: "key-type classifier — typed metadata + size/format table (public API, pure)"
milestone: M4
implements: ["REQ-KEY-006"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#6-protocol-bindings"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** Public `ehem_key_type_parse(type_str, &out)` (no I/O, no
allocation) decomposing a device `type` string — bare name (`ED25519`)
or comma flag set (`ATT,PKEY,ECDH,ExDSA,SECP256R1`) — into: algorithm
family enum, mode flags (ExDSA/ECDH), role flags (ATT/PKEY/CERT/
GENERIC_DER), and per-family size/format facts (pubkey wire encoding +
length; signature wire format DER-variable vs raw-fixed + max length).
Consumers can build the HEM-OP-2 local length table from it without
touching the device.

**Notes:** Vocabulary grounded in doc list.md/get.md + REQ-KEY-005 create
types + live flag sets (STEP-M3-010, api_quirks). Tolerant: unknown
tokens skipped, no algorithm token → family unknown, never a hard error
(fw may add flags). Sizes: NIST curves = compressed x963 (1+⌈bits/8⌉,
firmware exports compressed — wc_ecc_export_x963_ex(...,1)) + DER sig
max per curve; ED25519 32/64; ED448 57/114; CURVE25519/448 pubkey only.
Likely `include/ehem/keymgmt.h` + `src/keytype.c`. Pure function → unit
tests only; live cross-check of the constants happens at the M4 gate
(STEP-M4-060).

**Definition of done**
- [ ] Every documented bare name + all four live-observed flag-set
      strings classify correctly (family, modes, roles) — unit tests.
- [ ] Tolerance: unknown tokens skipped; no-algorithm string → family
      unknown without error — unit tests.
- [ ] Size/format table matches the REQ-KEY-006 constants for all
      asymmetric families — unit tests.
- [ ] Export + public-header gates green (new public symbols prefixed
      `ehem_`); MinGW cross-compile clean; ASan/LSan clean.
