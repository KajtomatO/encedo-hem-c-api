---
id: REQ-OPS-002
title: ehem_random — device hardware RNG via encrypt-IV harvest
status: verified
priority: must
revision: 2
source: user decision 2026-07-16 (encrypt-IV harvest, M5 decomposition discussion); HEM-SDK-7 / HEM-OP-3 (hardware random); encedo-hem-api-doc FIRMWARE_NOTES.md:49 (encrypt always returns a fresh random IV); firmware v1.2.2 api_crypto.c (no random endpoint; cipher/wrap rejects empty msg — generate path unreachable); approved 2026-07-17 (rev2 design)
depends_on: ["REQ-AUTH-003", "REQ-API-003", "REQ-OPS-006"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#11-milestones"]
---

# ehem_random — device hardware RNG via encrypt-IV harvest

The SDK SHALL provide `ehem_random(ctx, kid, buf, len)` that fills `buf`
with `len` bytes of device-hardware-RNG output, harvested from the
random IVs returned by `POST /api/crypto/cipher/encrypt`: the firmware
ignores caller-supplied IVs and always generates a fresh 16-byte IV via
`my_rng_gen_block` (the hardware RNG), returning it for CBC/GCM
(FIRMWARE_NOTES.md:49; api_crypto.c:1385). The call performs ⌈len/16⌉
encrypt round-trips on a throwaway payload using the caller-designated
AES key `kid` (per-KID scope `keymgmt:use:<kid>`), concatenating the
returned IVs.

**Design (resolved at M6 decomposition, 2026-07-17):**
- **Cipher selector:** `AES128-CBC` — CBC/GCM both return the IV (ECB
  does not, api_crypto.c:1408); CBC is the cheapest device-side, and the
  AES128 width works with **any** stored AES key (firmware accepts
  requested width ≤ key width, crypto.c:1304-1321).
- **Throwaway payload:** a single zero byte (PKCS#7-padded by the device
  to one block). The ciphertext is discarded.
- **Key:** caller-designated existing AES key — the SDK SHALL NOT create
  or delete keys implicitly (tests and tools orchestrate their own
  `EHEMTEST` key per REQ-TEST-003 / REQ-TOOL-010).
- The round-trips ride the REQ-OPS-006 binding internals (per-KID token
  cached once; REQ-NET-006 pacing applies between requests).

**Rationale:** fw v1.2.2 exposes **no random endpoint** (complete
handler inventory in api.h; no doc page; finding 2026-07-16 at M5
decomposition). The cipher-wrap "empty msg → generate" path suggested by
a stale firmware comment (api_crypto.c:767) and the python client's
docstring is unreachable — the handler rejects missing/empty `msg` with
400 (api_crypto.c:814-818, isvalid_base64 rejects ""; cipher-wrap.md
records the same). The encrypt-IV harvest is the only reachable source
of raw device RNG and satisfies HEM-OP-3's "hardware RNG" literally
(user decision 2026-07-16). Implemented at M6 on top of the
cipher-encrypt binding (REQ-OPS-006).

**Acceptance criteria:**
- [x] ~~OPEN (M6 design)~~ **RESOLVED (2026-07-17, recorded above):**
      AES128-CBC selector, single-zero-byte payload, caller-designated
      AES key (no implicit key creation).
- [ ] Against the fake transport: `len` 1..48 issues ⌈len/16⌉ encrypt
      requests, concatenated IVs fill `buf` exactly, partial tail
      handled; zero `len` or NULL args → `EHEM_ERR_ARG` with no I/O.
- [ ] Live: two consecutive calls return non-equal output; each
      harvested IV matches the IV echoed in the encrypt response.
