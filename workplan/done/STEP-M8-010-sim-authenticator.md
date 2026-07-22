---
id: STEP-M8-010
title: Simulated-authenticator test support — shim AES-128-CBC, scheme-A codec, ExtAuth JWT helpers
milestone: M8
implements: ["REQ-TEST-006"]
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy"]
depends_on: []
evidence:
  commits: ["7128513"]
  tests: ["verifies: REQ-TEST-006 — tests/unit/test_ext_sim.c (8 cases: NIST SP 800-38A F.2.1/F.2.2 AES-128-CBC known answer incl. in-place; keypair uniqueness/determinism/DH agreement; std-b64 helpers; HS256 JWT build/open/peek + wrong-key/tamper/structure rejections; header byte-identity with ehem_ejwt_build; scheme-A round-trip over 4 scope shapes incl. full-pad-block and #-meta; hand-decrypt against the firmware construction; 4 tamper rejections)"]
  notes: "Unit 33/33 gcc+clang (./dev ci) + ASan/LSan clean; export + public-header gates green (AES helpers stay internal). MinGW cross-syntax: ext_sim.c clean; test TU needs cmocka (CI-on-push leg, established boundary). Live-captured authreq fixture deliberately deferred to STEP-M8-030 as planned."
reopened: []
cancelled: null
---

**Goal:** everything a fake mobile app needs, offline-tested: crypto-shim
AES-128-CBC helpers (internal, wolfCrypt `wc_AesCbc*`), and a
`tests/support` simulated-authenticator module — per-run-unique
time-seeded X25519 keypairs, scheme-A codec (K = HMAC(key=ECDH,
msg=jti); AES key K[0..15], IV K[16..31]; PKCS#7-style pad; trailer =
HMAC(key=K, msg=unpadded)), reply/authreply JWT build and
request/authreq parse + HS256 verify (echoing the device `jti`).

**Notes:** support module links the static lib (test_auth_live
precedent) to reach shim + base64url internals; nothing new exported.
Unit vectors: local round-trips + tamper cases (bad pad byte, bad
trailer, wrong K); a live-captured fixture is added later at M8-030.
MinGW: run the cross-syntax check (ms_printf gotcha) and mind wolfSSL
AES availability (HAVE_AES_CBC is in both Debian and MSYS2 builds —
verify, the HAVE_AES_KEYWRAP lesson).

**Definition of done**
- [x] Shim gains internal AES-128-CBC encrypt/decrypt; unit-tested with
      a NIST/wolfCrypt vector; not exported (export gate green)
- [x] Simulated authenticator: keypair gen, scheme-A encode/decode,
      reply/authreply build, request/authreq parse+verify — unit
      round-trips + tamper cases green
- [x] `./dev ci` (gcc+clang) and `./dev test asan` green; header/export
      gates green; MinGW cross-syntax check clean (ext_sim.c; the
      cmocka test TU is the CI-on-push leg)
- [x] `implements: REQ-TEST-006` tag placed (crypto_shim.h AES section,
      ext_sim.h/.c); evidence filled
