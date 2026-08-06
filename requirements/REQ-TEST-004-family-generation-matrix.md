---
id: REQ-TEST-004
title: Per-family generation matrix — every fw create type exercised live
status: verified
priority: must
revision: 3
source: ARCHITECTURE.md §11 (M5 gate); user decision 2026-07-16 (M5 decomposition); firmware v1.2.2 api_keymgmt.c:866-963 (create type vocabulary, ground truth per REQ-MGMT §8); approved 2026-07-16
depends_on: ["REQ-KEY-001", "REQ-KEY-003", "REQ-KEY-004", "REQ-KEY-005", "REQ-KEY-006", "REQ-OPS-001", "REQ-TEST-002", "REQ-TEST-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#11-milestones"]
---

# Per-family generation matrix — every fw create type exercised live

The live suite SHALL include a per-family generation matrix test
that, for **every** key type accepted by the device firmware's create
endpoint (fw v1.2.2 vocabulary, api_keymgmt.c:866-963 — 23 types:
`SECP256R1/384R1/521R1/256K1`, `CURVE25519/448`, `ED25519/448`,
`SHA2-256/384/512`, `SHA3-256/384/512`, `AES128/192/256`,
`MLKEM512/768/1024`, `MLDSA44/65/87`), performs the cycle:

1. **create** with an `EHEMTEST`-prefixed label (REQ-TEST-003); the four
   NIST-P/K types are created with mode `ECDH,ExDSA` (the device default
   is ECDH-only — python OQ-19 — and the sign leg needs ExDSA);
2. **list** (full paginated walk, REQ-KEY-001) — the new kid is present
   and its live flag-set string is captured;
3. **get** (REQ-KEY-003) + REQ-KEY-006 classification cross-check —
   family matches the created type; public material length matches the
   classifier's size table where the family is asymmetric;
4. **sign + local verify** (REQ-OPS-001 + crypto shim) — only for the
   ExDSA-capable families (NIST-P/K ×4, ED25519, ED448);
5. **delete** (REQ-KEY-004) → absence check (get or list).

The matrix runs sequentially with **at most one matrix key alive at a
time** (repo capacity is small), and cleanup failures fail the test
(REQ-TEST-003).

**Suite placement (rev 3, M8 gate, user decision 2026-08-06):** the
matrix runs under the **`disruptive`** CTest label, not `integration`.
On the current device state it reproducibly HARD-STALLS the firmware —
4/4 that day, including alone on a freshly power-cycled, orphan-free,
otherwise idle device (died in the FIRST family's sign; near-identical
ops in test_sign_live passed minutes earlier) — and recovery is a
physical power-cycle, the definition of disruptive here. This
supersedes the M5-era observation that no single test triggers the
stall (falsified 2026-08-06; the KNOWN-ISSUES stall entry has the full
history). The matrix still runs on demand (`./dev test it -d`) and its
M5/M7 full-green history stands; rev 2 was the 21→23 type-count fix.

**Rationale:** this is the substance of the M5 gate ("generate → list →
sign where ExDSA-capable → delete cycle per family on the real device",
§11 wording per the 2026-07-16 decomposition). The exdsa sign leg cannot
apply to AES/HMAC/ML-KEM/ML-DSA/X25519/X448 — their "use" operations
(HMAC, cipher, encaps/decaps, ML-DSA sign, ECDH) arrive at M6, which
extends the use leg. The flag-set capture closes REQ-KEY-006's open
vocabulary criterion. Firmware-vocabulary ground truth beats the doc:
unknown types are device 400, repo failure 406.

**Acceptance criteria:**
- [x] The matrix is table-driven and covers exactly the 23 fw v1.2.2
      types; adding a future type is a one-row change. — MATRIX[] in
      test_keygen_matrix_live.c, 23 rows, one struct per type.
- [x] Sequential create→…→delete with ≤ 1 matrix key alive; a mid-cycle
      failure still attempts deletion of the live key before failing
      (no stranded `EHEMTEST` keys on a green-or-red run). — run_row deletes
      + untracks before the next row; teardown ehem_test_cleanup deletes any
      still-tracked key pass-or-fail; verified across the flaky-device runs
      (no EHEMTEST leftovers after a mid-run failure).
- [x] Sign leg: signatures from all six ExDSA-capable families verify
      locally via the crypto shim (Ed448 via `ehem_ed448_verify`;
      NOT_COMPILED_IN → EHEM_ERR_UNSUPPORTED, recorded-not-failed). — LIVE
      2026-07-17: SECP256R1 71B, SECP384R1 102B, SECP521R1 138B, SECP256K1
      71B (DER, each ≤ classifier max), ED25519 64B, ED448 114B — all
      verified locally against the device-exported pubkeys.
- [x] RESOLVED (live): the observed flag-set strings for AES/HMAC/MLKEM/
      MLDSA are recorded into REQ-KEY-006 (its open criterion, now closed);
      the new `PQC` token needed NO parser change (tolerant walk).
- [x] RESOLVED (live): full matrix green against the dev device — all 23
      families create → list → get/classify → sign(where ExDSA) → delete.
      Classifier pubkey_len cross-checks matched the wire for every
      asymmetric family (33/49/67/33/32/57/32/56; ML-KEM 800/1184/1568;
      ML-DSA 1312/1952/2592). Run ~142 s; the dev device's intermittent
      reachability (bounces, TCP-close per response) required the test's
      per-op transient-network retry to complete a stable run.
