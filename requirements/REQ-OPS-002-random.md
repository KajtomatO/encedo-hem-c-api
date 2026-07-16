---
id: REQ-OPS-002
title: ehem_random — device hardware RNG via encrypt-IV harvest
status: draft
priority: must
revision: 1
source: user decision 2026-07-16 (encrypt-IV harvest, M5 decomposition discussion); HEM-SDK-7 / HEM-OP-3 (hardware random); encedo-hem-api-doc FIRMWARE_NOTES.md:49 (encrypt always returns a fresh random IV); firmware v1.2.2 api_crypto.c (no random endpoint; cipher/wrap rejects empty msg — generate path unreachable)
depends_on: ["REQ-AUTH-003", "REQ-API-003"]
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
(FIRMWARE_NOTES.md:49). The call performs ⌈len/16⌉ encrypt round-trips
on a throwaway payload using the caller-designated AES key `kid`
(per-KID scope `keymgmt:use:<kid>`), concatenating the returned IVs.

**Rationale:** fw v1.2.2 exposes **no random endpoint** (complete
handler inventory in api.h; no doc page; finding 2026-07-16 at M5
decomposition). The cipher-wrap "empty msg → generate" path suggested by
a stale firmware comment (api_crypto.c:767) and the python client's
docstring is unreachable — the handler rejects missing/empty `msg` with
400 (api_crypto.c:814-818, isvalid_base64 rejects ""; cipher-wrap.md
records the same). The encrypt-IV harvest is the only reachable source
of raw device RNG and satisfies HEM-OP-3's "hardware RNG" literally
(user decision 2026-07-16). Implemented at M6 alongside the
cipher-encrypt binding this rides on; expected to gain a `depends_on`
to the M6 cipher-encrypt REQ when that is drafted.

**Acceptance criteria:**
- [ ] OPEN (M6 design): choice of throwaway payload, cipher mode
      (CBC vs GCM), and whether the SDK requires/creates a dedicated
      AES key — recorded here before implementation.
- [ ] Against the fake transport: `len` 1..48 issues ⌈len/16⌉ encrypt
      requests, concatenated IVs fill `buf` exactly, partial tail
      handled; zero `len` or NULL args → `EHEM_ERR_ARG` with no I/O.
- [ ] Live: two consecutive calls return non-equal output; each
      harvested IV matches the IV echoed in the encrypt response.
