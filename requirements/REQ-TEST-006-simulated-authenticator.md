---
id: REQ-TEST-006
title: Simulated authenticator — device-local ExtAuth testing and broker-test gating
status: verified
priority: must
revision: 2
source: user decision 2026-07-22 (M8 decomposition; broker tests gated into the disruptive label — user decision same day); encedo_firmware api_auth.c scheme-A construction (:1511-1545, :1743-1801); REQ-TEST-003 EHEMTEST policy
depends_on: ["REQ-TEST-002", "REQ-TEST-003", "REQ-AUTH-006", "REQ-AUTH-007"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#9-testing-policy"]
---

# Simulated authenticator — device-local ExtAuth testing and broker-test gating

Integration tests SHALL exercise the complete ExtAuth pairing and login
cycle against the real device through a test-support simulated
authenticator — no cloud broker, no phone — while every live test that
contacts the notification broker's registration or event endpoints
carries the `disruptive` CTest label.

- **The simulated authenticator** (tests/support, linking the static lib
  for shim primitives, the test_auth_live precedent) implements the
  mobile app's crypto: per-run-unique X25519 identity + confirmation
  keypairs (time-seeded — fixed material trips the REQ-KEY-008 repo
  dedup, the M7-072 lesson), reply/authreply JWT build (base64url-nopad
  segments + HS256 over `ECDH(sim_priv, eid)`, echoing the device's
  `jti`), request/authreq parse + signature verification, and the
  scheme-`A` codec:
  `K = HMAC-SHA256(key=ECDH_secret, msg=jti_raw)`; AES-128-CBC with
  key = K[0..15], IV = K[16..31] over the PKCS#7-style padded scope
  (pad byte = pad count, always ≥1 block); trailer =
  `HMAC-SHA256(key=K, msg=unpadded scope)`; value = `"A" +
  base64(ciphertext ‖ trailer)` keyed by base64(32-byte descriptor
  suffix). AES-128-CBC comes from new crypto-shim helpers (wolfCrypt
  `wc_AesCbc*`, internal, not exported).
- **Hygiene:** simulated pairings use an `EHEMTEST`-prefixed label
  (REQ-TEST-003 applies — the label is what `keys rm --all` and the
  sweep see) and delete their EXTAID key in cleanup, pass or fail.
- **Broker gating (user decision 2026-07-22):** live tests touching
  `register/init|check|finalise` or `event/new|check` are
  `disruptive`-labeled (`EHEM_TEST_URL` + `EHEM_ALLOW_DISRUPTIVE=1`,
  REQ-TEST-002 machinery) — once a real phone is paired, an unattended
  `event/new` would ring it; registrations create dangling broker state.
  The read-only `notify/session` probe MAY run under the plain
  `integration` label. Real-phone approve/reject/timeout runs are
  attended-manual (STEP-M8-080), the REQ-SYS-008 precedent.
- Unit tests cover the codec and JWT builders offline with fixture
  vectors (round-trip + a capture from the live device once available).

**Rationale:** the ExtAuth handlers validate pure crypto, not phone-ness
— anything holding the paired private key IS an authenticator. A
simulated one turns the whole M8 surface except the push itself into
ordinary unattended `-L integration` coverage on every run, exactly like
the rest of the suite, and keeps the user's phone out of CI.

**Acceptance criteria:**
- [x] Unit (test_ext_sim, 2026-07-23): scheme-A round-trip + tamper
      cases (flipped ct, wrong scheme, truncation, wrong K) + a
      hand-decrypt against the firmware construction; JWT
      build/open/peek round-trip; AND a REAL captured device authreq
      (fixtures/ext_authreq_fixture.h) verified + decrypted offline.
- [x] Live (`integration` label, 2026-07-23): full simulated cycle —
      pair (init→reply→validate, code verified, test_ext_pair_live) →
      login (request→decrypt own entry→authreply→token→bearer used on
      config, test_ext_login_live) — green unattended, zero broker
      traffic.
- [x] Live: cleanup via the tracked-keyreg teardown (runs pass or
      fail); both tests leave no EHEMTEST/EXTAID residue (dedup-probe
      406 creates no key).
- [x] Grep-check (2026-07-23, with the full M8 broker test population):
      no live test outside the `disruptive` label touches the broker
      register/event paths — test_notify_session_live (integration)
      touches only `/session`; register polling lives in
      tests/disruptive/; event legs were attended-only (M8-080).
