---
id: REQ-TEST-004
title: Per-family generation matrix — every fw create type exercised live
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §11 (M5 gate); user decision 2026-07-16 (M5 decomposition); firmware v1.2.2 api_keymgmt.c:866-963 (create type vocabulary, ground truth per REQ-MGMT §8); approved 2026-07-16
depends_on: ["REQ-KEY-001", "REQ-KEY-003", "REQ-KEY-004", "REQ-KEY-005", "REQ-KEY-006", "REQ-OPS-001", "REQ-TEST-002", "REQ-TEST-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#11-milestones"]
---

# Per-family generation matrix — every fw create type exercised live

The integration suite SHALL include a per-family generation matrix test
that, for **every** key type accepted by the device firmware's create
endpoint (fw v1.2.2 vocabulary, api_keymgmt.c:866-963 — 21 types:
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

**Rationale:** this is the substance of the M5 gate ("generate → list →
sign where ExDSA-capable → delete cycle per family on the real device",
§11 wording per the 2026-07-16 decomposition). The exdsa sign leg cannot
apply to AES/HMAC/ML-KEM/ML-DSA/X25519/X448 — their "use" operations
(HMAC, cipher, encaps/decaps, ML-DSA sign, ECDH) arrive at M6, which
extends the use leg. The flag-set capture closes REQ-KEY-006's open
vocabulary criterion. Firmware-vocabulary ground truth beats the doc:
unknown types are device 400, repo failure 406.

**Acceptance criteria:**
- [ ] The matrix is table-driven and covers exactly the 21 fw v1.2.2
      types; adding a future type is a one-row change (code review +
      unit-testable table sanity where practical).
- [ ] Sequential create→…→delete with ≤ 1 matrix key alive; a mid-cycle
      failure still attempts deletion of the live key before failing
      (no stranded `EHEMTEST` keys on a green-or-red run).
- [ ] Sign leg: signatures from all six ExDSA-capable families verify
      locally via the crypto shim (Ed448 via a new `ehem_ed448_verify`;
      a NOT_COMPILED_IN wolfSSL build maps to EHEM_ERR_UNSUPPORTED and
      is recorded, not failed — mirrors the compressed-point pattern).
- [ ] OPEN (live, resolves at the M5 gate): the observed live flag-set
      strings for AES/HMAC/MLKEM/MLDSA families are recorded into
      REQ-KEY-006 (its open criterion); tolerant parsing means at most
      token additions to the classifier.
- [ ] OPEN (live, resolves at the M5 gate): full matrix green against
      the dev device; per-family observations (timings, quirks, e.g.
      slow ML-DSA keygen) recorded here.
