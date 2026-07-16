---
id: STEP-M2-020
title: "base64/base64url codec + eJWT encoder, byte-exact against a python-client fixture"
milestone: M2
implements: ["REQ-AUTH-001"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session"]
depends_on: ["STEP-M2-010"]
evidence:
  commits:
    - "56b1385 — base64/eJWT implementation (evidence backfilled in a follow-up commit)"
  tests:
    - "verifies: REQ-AUTH-001 — tests/unit/test_ejwt.c (base64/base64url round-trip + rejection; byte-exact eJWT vs python fixture; exp cap; arg validation)"
  notes: |
    src/ejwt.{h,c} (implements: REQ-AUTH-001): standard base64 (padded) +
    base64url (no pad) encode/decode, and ehem_ejwt_build(). Added to
    EHEM_SOURCES. Payload JSON is built via NEW json-layer helpers
    (ehem_json_new_object / add_string / add_int64) so cJSON stays contained to
    json.c (json.c is "the only TU that includes cJSON") — ejwt.c speaks json.h
    + crypto_shim.h only. ehem_json_print is already compact
    (cJSON_PrintUnformatted). HMAC tag via ehem_hmac_sha256 (shim).

    ehem_ejwt_build boundary mirrors the python client's build_ejwt except the
    passphrase-derived material (shared secret + user pubkey) is produced by the
    crypto shim and passed in. Header is the fixed byte string; claims emitted
    jti/aud/exp/iat/iss/scope; exp = min(requested_exp, challenge_exp); iss is
    std base64 of the pubkey; segments base64url no-pad. *out_ejwt cleared on
    every error path (ehem_ctx_create convention — fixed a first-pass bug where
    it was set after the arg check, caught by the arg test).

    Fixture tests/support/fixtures/ejwt_login_vector.h: inputs + expected eJWT
    captured from the REAL python client (encedo-hem-python-api auth.py
    build_ejwt, its test_auth_vectors.py module fixture: eid d4ad81…, spk
    base64(0x01*32), jti 0123456789abcdef, scope keymgmt:gen, passphrase
    "correct horse battery staple", now 1.7e9, requested_exp 1.7e9+3600,
    challenge_exp 2e9). Regen command in the fixture header. Verified BEFORE
    coding that vendored cJSON reproduces the exact compact payload bytes
    (incl. large-int exp/iat).

    test_ejwt_matches_python_fixture runs the FULL pipeline (PBKDF2 600k →
    keypair → ECDH → build) and asserts the token equals the fixture
    byte-for-byte (also iss == CdLq53…) — this integration-tests M2-010 + M2-020
    against the working client. Verified on the dev machine:
      - ./dev ci → gcc + clang, 11/11 unit tests each under -Werror.
      - ./dev test asan → ASan/LSan clean; secret intermediates (seed/priv/
        shared) zeroized in the test via ehem_zeroize.
      - export + header gates green (new json helpers are not EHEM_API).
    base64 coverage: round-trip over 0..7 bytes (all in_len%3 classes) for both
    variants; RFC 4648 known vectors; rejection of wrong-alphabet chars
    (std↔url '+/' vs '-_'), impossible n%4==1 length, and undersized buffers.
reopened: []
cancelled: null
---

**Goal:** `src/ejwt.{h,c}`: standard base64 encode/decode (with padding)
and base64url encode/decode (no padding), plus `ehem_ejwt_build()` taking
the challenge fields + scope + passphrase-derived key material and
producing the compact eJWT per REQ-AUTH-001 (hardcoded header bytes
`{"ecdh":"x25519","alg":"HS256","typ":"JWT"}`, claim order
jti/aud/exp/iat/iss/scope, compact JSON separators, HMAC-SHA256 tag).

**Notes:** The claim JSON must be built with cJSON configured for compact
output — byte-exactness with the python fixture requires identical
separators and key order (python uses dict insertion order with
`separators=(",", ":")`). Generate the fixture with
`encedo-hem-python-api`'s `build_ejwt` from fixed inputs (challenge,
passphrase, now, requested_exp) and commit inputs + expected output under
`tests/support/fixtures/`; regeneration command recorded in the fixture
header comment.

**Definition of done**
- [x] base64/base64url round-trip unit tests incl. padding edge lengths
      (0..3 remainder bytes) and reject-on-invalid input.
- [x] `ehem_ejwt_build` reproduces the python-client fixture eJWT
      byte-for-byte (unit test; `verifies:` REQ-AUTH-001).
- [x] Secret intermediates zeroized; ASan/LSan clean; `-Werror` green on
      GCC + Clang.
