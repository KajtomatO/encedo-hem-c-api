---
id: REQ-AUTH-006
title: ExtAuth pairing bindings — init, validate, mac
status: approved
priority: must
revision: 2
source: user decision 2026-07-22 (M8 decomposition); encedo-hem-api-doc auth/ext-init.md, ext-validate.md, ext-mac.md; encedo_firmware api_auth.c:838 (init), :980 (validate), :1174 (mac); hem-api-tester test_5.php
depends_on: ["REQ-AUTH-003", "REQ-KEY-004"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#6-protocol-bindings"]
---

# ExtAuth pairing bindings — init, validate, mac

The SDK SHALL provide bindings for the three ExtAuth pairing endpoints —
`ehem_ext_init`, `ehem_ext_validate`, `ehem_ext_mac` — with typed results,
authenticated with scope `auth:ext:pair`.

- **Access (all three, fw api_auth.c):** Bearer with `sub="U"` and scope
  prefix-matching `auth:ext:pair` OR `system:config`
  (`strstr(scope,X)==scope`). The SDK requests the exact scope
  `auth:ext:pair` through the normal `ensure_token` path. Preconditions
  checked before auth: `fls_state==0` (else 409), initialised (else 409);
  bad/missing bearer 401, wrong scope/sub 403 — the shared REQ-AUTH-003
  mapping applies.
- **`ehem_ext_init(ctx, epk_b64, &out)`** → POST `/api/auth/ext/init`
  `{"epk": <base64 32-byte Curve25519 pub>}`; 200 →
  `{request, eid}` (`ehem_ext_init_info`). `request` is an opaque JWT the
  caller forwards to the authenticator (header `{"ecdh":"x25519"}` +
  HS256 over `ECDH(EIDkey, epk)`; grants `iss`=eid, `aud`=epk,
  `jti`=base64(32-byte time nonce), `exp`=now+86400 — fw adds NO `iat`,
  doc's "iat+86400" is imprecise). `eid` = device EncedoID (base64
  Curve25519 pub). No device state changes on this call. 400 = malformed
  epk (not 32 bytes).
- **`ehem_ext_validate(ctx, pid_b64, reply_jwt, &out)`** → POST
  `/ext/validate` `{"pid", "reply"}`; 200 → `{kid, code}`
  (`ehem_ext_validate_info`). Firmware facts (api_auth.c:980):
  - `reply` must be HS256-signed with `ECDH(authenticator_priv,
    EIDkey_pub)` and carry grants `jti` (MUST echo the request's `jti` —
    revalidated as a fresh device time-nonce), `iss` (authenticator
    Curve25519 pub, base64 — imported verbatim), `label` (becomes the
    repo key label — REQ-KEY-005 label rules apply, max 32), `epk`
    (base64 32-byte confirmation input).
  - On success the authenticator pubkey is imported as a `CURVE25519`
    repo key with descriptor `"EXTAID" + base64_decode(pid)`; `pid`
    SHOULD decode to exactly 32 bytes (the request-side enumeration and
    `/ext/token` lookup read a fixed 32-byte descriptor suffix).
  - `code` = base64 `HMAC-SHA256(reply-JWT-string, key=ECDH(EIDkey,
    epk-from-reply))` — the caller forwards it so the authenticator can
    verify acceptance; the SDK documents the recipe so tests verify it
    locally via the crypto shim.
  - 406 = repo import failed: slot table full (`EXT_AUTH_MAX_CLIENTS` =
    8, api_auth.c:24) or repo dedup of an identical pubkey (the
    REQ-KEY-008 dedup behavior) — ambiguous on the wire, mapped
    `EHEM_ERR_DEVICE` with detail naming both causes.
- **`ehem_ext_mac(ctx, epk_b64, &out)`** → POST `/ext/mac` `{"epk"}`;
  200 → `{nonce, mac, eid}` (`ehem_ext_mac_info`): `nonce` = base64
  32-byte time nonce, `mac` = base64 `HMAC-SHA256(nonce_raw,
  key=ECDH(EIDkey, epk))`, `eid` as in init. Stateless liveness/identity
  proof; the device does NOT consume the nonce — replay rejection is the
  counterparty's job (doc note confirmed in source: no
  `nonce_validate_time_based` on this path).
- Unpairing is NOT a new binding: paired authenticators are ordinary repo
  keys removed via `ehem_key_delete` (REQ-KEY-004).
- **Firmware quirk (live-probed 2026-07-23, rev 2):** `/keymgmt/get` OMITS
  `descr` for the ext-paired CURVE25519 key (`descr_len` 0) even though
  BOTH list and search return the full 38-byte `"EXTAID"+pid` blob — and
  get reports the bare type `CURVE25519` where list shows the flag-set
  `ECDH,CURVE25519`. REPO_* source is absent from the fw checkout, so this
  is pinned from device behavior (test_ext_pair_live). Inspecting a
  pairing's pid therefore goes through search/list, never get. The
  `/ext/request` enumeration reads the repo directly and is unaffected.

**Rationale:** pairing is the precondition for the M8 push-confirm login
flow (REQ-AUTH-007/009). Grouping the three endpoints in one REQ follows
the REQ-SYS-009 logger-family precedent. The base64 helper the firmware
uses for `code`/`nonce`/`mac`/`eid` values (`jwt_Base64encode`) must be
pinned live — standard-vs-url alphabet is not obvious from source alone.

**Acceptance criteria:**
- [x] Unit (fake transport, tests/unit/test_ext.c, 2026-07-23):
      request/response shapes for all three bindings; scope
      `auth:ext:pair` requested (bearer on the wire); 401/403/406/409
      mapping; epk/pid argument validation (`EHEM_ERR_ARG` before any
      network I/O for non-32-byte input).
- [x] Live (test_ext_pair_live, 2026-07-23, 2/2 stable): full simulated
      pairing — init → locally-built reply → validate returns `kid` +
      `code`, `code` verified locally via the shim recipe; the request
      JWT's signature verifies with `ECDH(our_ephemeral, eid)`; EXTAID
      key inspected via get (pubkey == our identity key) + SEARCH
      (label + `"EXTAID"+pid` descriptor — get omits descr, see the
      quirk above) and deleted in cleanup.
- [x] Live (same run): `ehem_ext_mac` round-trip — `mac` verified
      locally against `HMAC-SHA256(nonce, ECDH(our_ephemeral_priv,
      eid))`; base64 variant of `code`/`nonce`/`mac`/`eid` = STANDARD
      with padding (byte-equal to the shim's std-b64 output).
- [x] Live probe (2026-07-23): re-pairing an identical `iss` pubkey
      under a FRESH pid → HTTP 406 with an EMPTY payload (repo dedup) —
      indistinguishable on the wire from slot exhaustion, as predicted;
      the SDK detail names both causes. Slot-exhaustion (8 pairings) not
      separately provoked — same wire shape, nothing more to learn.
