---
id: REQ-OPS-007
title: Bindings for /api/crypto/pqc/mlkem/encaps and /decaps — ML-KEM by KID
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §6, §11 (M6); encedo-hem-api-doc crypto/pqc/mlkem-encaps.md, mlkem-decaps.md; encedo_firmware api_crypto.c api_post_crypto_pqc_mlkem_encaps/_decaps + crypto.c CRYPTO_MLKEM_Encaps/Decaps (fw v1.2.2); HEM-SDK-7; approved 2026-07-17
depends_on: ["REQ-AUTH-002", "REQ-AUTH-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#5-auth--session"]
---

# Bindings for /api/crypto/pqc/mlkem/encaps and /decaps — ML-KEM by KID

The SDK SHALL provide `ehem_mlkem_encaps(ctx, kid, &out)` over
`POST /api/crypto/pqc/mlkem/encaps` — returning caller-owned `{alg string;
ss (32 bytes, zeroized on free); ct}` — and `ehem_mlkem_decaps(ctx, kid,
ct, ct_len, &out)` over `POST /api/crypto/pqc/mlkem/decaps` returning
`{ss}` (zeroized on free).

- **Request shapes** (firmware api_crypto.c:1939-2228): encaps body is
  `{kid}` only; decaps is `{kid, ct(base64)}`. The parameter set is fixed
  by the key (`MLKEM512`/`768`/`1024`); `alg` in the response reports it
  (encaps: `"MLKEM512"|"MLKEM768"|"MLKEM1024"`, crypto.c:1765-1774).
- **Sizes** (device-enforced): ss = 32 (`WC_ML_KEM_SS_SZ`); ct = 768/1088/
  1568 by set; decaps requires ct_len to match the key's set **exactly**
  (crypto.c:1855, mismatch → 406). SDK pre-validation: kid not 32 hex,
  ct NULL/empty or ct_len > 1568 → `EHEM_ERR_ARG`, no I/O; exact-length
  policing stays device-side (the SDK does not know the key's set).
- **Decaps `alg` field is unreliable:** the decaps handler echoes a buffer
  `CRYPTO_MLKEM_Decaps` never writes (its prototype has no alg_used param,
  crypto.h:27, while the handler passes one, api_crypto.c:2177) — the SDK
  treats `alg` as optional in both responses (tolerant parse, may be
  absent/garbage on decaps; open criterion records the live value).
- **Note:** the shared secret crosses the wire base64-encoded inside TLS
  in both directions (encaps returns it, that is the endpoint's design);
  the SDK zeroizes its copies (struct `_free` + JSON/transport buffers per
  ARCHITECTURE §4).
- **Scope:** exact `keymgmt:use:<kid>`, sub != M, shared per-KID token.
  **Errors:** 403 → `EHEM_ERR_SCOPE_DENIED`; 400 → `EHEM_ERR_DEVICE`; 406
  (not ML-KEM / not found / ct size mismatch / crypto failure) →
  `EHEM_ERR_DEVICE` with detail.

**Rationale:** M6 milestone core (ARCHITECTURE §11). ML-KEM keys became
creatable at M5 (flag-sets `ATT,PKEY,PQC,MLKEM…`, REQ-KEY-006). The full
KEM contract (both sides derive the same ss) is verifiable on one device
because encaps uses the public half and decaps the private half of the
same kid.

**Acceptance criteria:**
- [ ] Against the fake transport: encaps body is exactly `{kid}`, decaps
      exactly `{kid, ct(b64)}`; `ss`/`ct` decoded to caller-owned buffers,
      `alg` tolerated absent; ss zeroized on free, `_free` NULL-safe;
      error mapping and `EHEM_ERR_ARG` pre-validation with zero transport
      calls (unit tests asserting body bytes).
- [ ] Live round-trip on an `EHEMTEST` ML-KEM-768 key: encaps → alg
      `"MLKEM768"`, 32-byte ss, 1088-byte ct; decaps(ct) → identical ss;
      decaps with a truncated ct → 406/`EHEM_ERR_DEVICE`; one additional
      set (512 or 1024) round-tripped with its ct size verified; cleanup
      per REQ-TEST-003.
- [ ] OPEN (live, informational): record what decaps actually returns in
      `alg` on fw v1.2.2 (expected garbage/absent per the prototype
      mismatch) here.
