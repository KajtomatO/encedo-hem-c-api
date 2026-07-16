---
id: STEP-M2-020
title: "base64/base64url codec + eJWT encoder, byte-exact against a python-client fixture"
milestone: M2
implements: ["REQ-AUTH-001"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session"]
depends_on: ["STEP-M2-010"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] base64/base64url round-trip unit tests incl. padding edge lengths
      (0..3 remainder bytes) and reject-on-invalid input.
- [ ] `ehem_ejwt_build` reproduces the python-client fixture eJWT
      byte-for-byte (unit test; `verifies:` REQ-AUTH-001).
- [ ] Secret intermediates zeroized; ASan/LSan clean; `-Werror` green on
      GCC + Clang.
