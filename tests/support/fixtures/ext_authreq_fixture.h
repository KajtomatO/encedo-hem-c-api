/*
 * ext_authreq_fixture.h — a REAL authreq captured from the live device
 * (my.ence.do, fw v1.2.2, 2026-07-23) together with the simulated-
 * authenticator keys that can open it, frozen so the unit suite proves the
 * scheme-A codec and JWT verifier against genuine device bytes offline.
 *
 * supports: REQ-TEST-006, REQ-AUTH-007
 *
 * Captured by scratchpad ext_capture.c (STEP-M8-030): pair a simulated
 * authenticator (identity key below, pid below) → POST /api/auth/ext/request
 * with scope "keymgmt:list", ctx "fixture-ctx", the ephemeral below → freeze
 * the returned authreq verbatim → unpair. The authreq is HS256 over
 * ECDH(EIDkey, ephemeral); its scope object holds ONE entry for the paired
 * authenticator, scheme-A encrypted under HMAC(ECDH(identity, EIDkey), jti).
 * NB the firmware's header key order is {"ecdh","typ","alg"} — libjwt's,
 * different from the login eJWT builder's — verification never rebuilds the
 * header, so only parsers that reconstruct segments would care.
 */
#ifndef EHEM_EXT_AUTHREQ_FIXTURE_H
#define EHEM_EXT_AUTHREQ_FIXTURE_H

/* captured from the live device — see ext_capture.c */
#define EXT_FX_AUTHREQ \
    "eyJlY2RoIjoieDI1NTE5IiwidHlwIjoiSldUIiwiYWxnIjoiSFMyNTYifQ.eyJpc3MiOiJyY1BGOVhFdnBzRjZ0UFczUFR2YnVDMlVDcUMrMGtDT0p1WC9rZ1kxdVRrPSIsImF1ZCI6IjlQZ1NIUlZJUFdMdno5Z1diQ0lYazg0NzVaZVJod0xDd0c5cGs1aWJ1R009IiwiaWF0IjoxNzg0NzY3NzY0LCJleHAiOjE3ODQ3NzEzNjQsImp0aSI6IkZHVmhhcGRhZCs4YXA5eDZ0MFVRQi8yTzFHL3ZhYml0aXNqWmRRVHhKME09IiwiY3R4IjoiZml4dHVyZS1jdHgiLCJzY29wZSI6eyJTbXVQT04zWDFFZVJoL3JrMlZ0VkZCc3k2UFJxWktsdk1SZ0NOZDZqbkdrPSI6IkE4Yk9UL291MFBOa1RISCsxU3I1NzAwK29yL0ZvQkZ5bFlXVjVJWjhCeFFaa2c1NUVvWEVsZ0JxSVBOVmJwdFFvIn19.Ch2QfHydtkSmCOBwkS7JZg7FxiKUlC8nOx8xeRmzRbg"
#define EXT_FX_IDENTITY_PRIV_HEX \
    "20e84bfe712c139fa4874fa9c88d50bdee6853fdc2941343ba4146118908ba78"
#define EXT_FX_IDENTITY_PUB_HEX \
    "5b22487ba20056fb4f6db333d7b7566112a549ea6c8ea5a305cb668df509736c"
#define EXT_FX_REQ_EPH_PRIV_HEX \
    "108441ed5d6cdefea3a83b2d0f895d6f443829a45b40ca62e122f7f30e153667"
#define EXT_FX_EID_HEX \
    "adc3c5f5712fa6c17ab4f5b73d3bdbb82d940aa0bed2408e26e5ff920635b939"
#define EXT_FX_PID_B64 "SmuPON3X1EeRh/rk2VtVFBsy6PRqZKlvMRgCNd6jnGk="
#define EXT_FX_SCOPE "keymgmt:list"
#define EXT_FX_CTX "fixture-ctx"

#endif /* EHEM_EXT_AUTHREQ_FIXTURE_H */
