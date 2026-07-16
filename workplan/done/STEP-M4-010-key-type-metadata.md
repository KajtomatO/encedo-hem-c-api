---
id: STEP-M4-010
title: "key-type classifier — typed metadata + size/format table (public API, pure)"
milestone: M4
implements: ["REQ-KEY-006"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#6-protocol-bindings"]
depends_on: []
evidence:
  commits:
    - "2c3298a — key-type classifier (REQ-KEY-006)"
  tests:
    - "tests/unit/test_keytype.c — all 25 documented bare names (22 create.md + CERT/DER_PKEY/direct DER_PKEY) with full size/format quintuple; the 4 live flag sets (modes+roles decomposed); tolerance (unknown token skipped, no-algorithm → UNKNOWN ok, empty/stray commas, GENERIC_DER order independence, prefix-of-token rejected, NULL → ARG); family_str round-trip over every family"
  notes: >
    include/ehem/keymgmt.h new section: ehem_key_family enum (device
    vocabulary names), EHEM_KEY_MODE_/ROLE_ bit flags, ehem_key_type_info
    {family, modes, roles, pubkey_len, sig_max_len, sig_der},
    ehem_key_type_parse() + ehem_key_family_str(). src/keytype.c: pure
    table-driven token walk (no alloc/I-O); DER containers resolved
    POST-walk from role flags (CERT > GENERIC_DER) so token order can't
    matter; unknown tokens skipped. Sizes: NIST = compressed-x963 pubkey
    (33/49/67) + DER sig max (72/104/141); ED25519 32/64, ED448 57/114 raw;
    CURVE25519/448 32/56 pubkey-only; MLKEM 800/1184/1568 (FIPS 203);
    MLDSA 1312/1952/2592 pub + 2420/3309/4627 sig (FIPS 204); AES/HMAC no
    material. ehem_key_family_str added (needed by keys pub, M4-040).
    ./dev ci 18/18 gcc+clang, asan clean, export+header gates green,
    keytype.c MinGW cross-compile (x86_64-w64-mingw32-gcc -Werror) clean.
    Live cross-check of constants deferred to the M4 gate (M4-060) per plan.
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
- [x] Every documented bare name + all four live-observed flag-set
      strings classify correctly (family, modes, roles) — unit tests.
      (test_bare_names — 25 names; test_live_flag_sets — 4 strings.)
- [x] Tolerance: unknown tokens skipped; no-algorithm string → family
      unknown without error — unit tests. (test_tolerant_parsing.)
- [x] Size/format table matches the REQ-KEY-006 constants for all
      asymmetric families — unit tests. (expect_info quintuple asserts on
      every name; live cross-check at the M4 gate.)
- [x] Export + public-header gates green (new public symbols prefixed
      `ehem_`); MinGW cross-compile clean; ASan/LSan clean.
      (export_symbols + public_headers_dep_free passed; keytype.c compiled
      -Werror with x86_64-w64-mingw32-gcc; ./dev test asan 18/18.)
