/*
 * ejwt_login_vector.h — golden eJWT fixture for REQ-AUTH-001.
 *
 * Inputs + expected output for one full login-token build, captured from the
 * reference python client (encedo-hem-python-api) so the C shim + eJWT builder
 * can be proven byte-for-byte identical to the WORKING client (the one that
 * authenticates against the dev-machine HEM).
 *
 * These inputs are the module fixture in the python client's
 * tests/unit/test_auth_vectors.py (challenge + built_ejwt fixtures).
 *
 * REGENERATE (from the python client repo root, cryptography installed):
 *   python3 - <<'PY'
 *   import base64, sys; sys.path.insert(0, 'src')
 *   from encedo_hem.auth import build_ejwt
 *   from encedo_hem.models import AuthChallenge
 *   ch = AuthChallenge(eid="d4ad81b06b1d493ab2b6f9b1a3e2c7f0",
 *                      spk=base64.b64encode(b"\x01"*32).decode(),
 *                      jti="0123456789abcdef", exp=2_000_000_000, lbl="alice")
 *   print(build_ejwt(challenge=ch, scope="keymgmt:gen",
 *                    passphrase=b"correct horse battery staple",
 *                    now=1_700_000_000, requested_exp=1_700_003_600))
 *   PY
 */
#ifndef EHEM_FIXTURE_EJWT_LOGIN_VECTOR_H
#define EHEM_FIXTURE_EJWT_LOGIN_VECTOR_H

/* --- inputs --------------------------------------------------------------- */
#define EJWT_FX_EID          "d4ad81b06b1d493ab2b6f9b1a3e2c7f0"  /* PBKDF2 salt (raw UTF-8) */
#define EJWT_FX_SPK          "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE="  /* base64(0x01*32) */
#define EJWT_FX_JTI          "0123456789abcdef"
#define EJWT_FX_SCOPE        "keymgmt:gen"
#define EJWT_FX_PASSPHRASE   "correct horse battery staple"
#define EJWT_FX_NOW          1700000000
#define EJWT_FX_REQUESTED_EXP 1700003600
#define EJWT_FX_CHALLENGE_EXP 2000000000
/* exp claim = min(requested_exp, challenge_exp) = 1700003600 */

/* --- expected outputs ----------------------------------------------------- */
/* user X25519 public key, standard base64 (padded) — the "iss" claim. */
#define EJWT_FX_EXPECT_ISS   "CdLq53eX780FeZR4/hOee5rTtcl7ajJdYwVwcjL5cxY="

/* the full compact eJWT, byte-for-byte. */
#define EJWT_FX_EXPECT_EJWT \
    "eyJlY2RoIjoieDI1NTE5IiwiYWxnIjoiSFMyNTYiLCJ0eXAiOiJKV1QifQ" \
    ".eyJqdGkiOiIwMTIzNDU2Nzg5YWJjZGVmIiwiYXVkIjoiQVFFQkFRRUJBUUVCQVFFQkFRRUJBUUVCQVFFQkFRRUJBUUVCQVFFQkFRRT0iLCJleHAiOjE3MDAwMDM2MDAsImlhdCI6MTcwMDAwMDAwMCwiaXNzIjoiQ2RMcTUzZVg3ODBGZVpSNC9oT2VlNXJUdGNsN2FqSmRZd1Z3Y2pMNWN4WT0iLCJzY29wZSI6ImtleW1nbXQ6Z2VuIn0" \
    ".oPm7pWzqB9VImg5OpTllCqbQAO-xkdgoRfdzyLj09qE"

#endif /* EHEM_FIXTURE_EJWT_LOGIN_VECTOR_H */
