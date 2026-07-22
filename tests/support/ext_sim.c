/*
 * ext_sim.c — simulated ExtAuth authenticator (see ext_sim.h).
 *
 * implements: REQ-TEST-006
 */
#include "ext_sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crypto_shim.h"
#include "ejwt.h"

/* The eJWT header (ejwt.c emits the same fixed bytes — kept in sync by
 * test_ext_sim's header-identity check against ehem_ejwt_build output). */
static const char SIM_JWT_HEADER[] =
    "{\"ecdh\":\"x25519\",\"alg\":\"HS256\",\"typ\":\"JWT\"}";

/* --- keypairs ------------------------------------------------------------- */

int ehem_sim_keypair_from_seed(const uint8_t seed[EHEM_SIM_KEY_SIZE],
                               ehem_sim_keypair *kp)
{
    if (seed == NULL || kp == NULL) {
        return -1;
    }
    return ehem_x25519_keypair_from_seed(seed, kp->priv, kp->pub) == EHEM_OK
               ? 0 : -1;
}

int ehem_sim_keypair_gen(ehem_sim_keypair *kp)
{
    /* Uniqueness (not unpredictability) is the requirement: mix wall clock,
     * CPU clock, and a process-local counter through HMAC-SHA256. */
    static unsigned counter;
    uint8_t material[sizeof(time_t) + sizeof(clock_t) + sizeof(unsigned)];
    uint8_t seed[EHEM_SIM_KEY_SIZE];

    time_t now = time(NULL);
    clock_t cpu = clock();
    unsigned n = counter++;
    memcpy(material, &now, sizeof now);
    memcpy(material + sizeof now, &cpu, sizeof cpu);
    memcpy(material + sizeof now + sizeof cpu, &n, sizeof n);

    if (ehem_hmac_sha256((const uint8_t *)"ehem-ext-sim-seed", 17,
                         material, sizeof material, seed) != EHEM_OK) {
        return -1;
    }
    return ehem_sim_keypair_from_seed(seed, kp);
}

int ehem_sim_shared(const ehem_sim_keypair *kp,
                    const uint8_t peer_pub[EHEM_SIM_KEY_SIZE],
                    uint8_t out[EHEM_SIM_KEY_SIZE])
{
    if (kp == NULL) {
        return -1;
    }
    return ehem_x25519_shared(kp->priv, peer_pub, out) == EHEM_OK ? 0 : -1;
}

/* --- base64 claim-value helpers ------------------------------------------- */

char *ehem_sim_b64(const uint8_t *in, size_t len)
{
    size_t cap = ehem_b64_std_encoded_len(len) + 1;
    char *s = malloc(cap);
    if (s == NULL) {
        return NULL;
    }
    if (ehem_b64_std_encode(in, len, s, cap) == (size_t)-1) {
        free(s);
        return NULL;
    }
    return s;
}

int ehem_sim_b64_key(const char *b64, uint8_t out[EHEM_SIM_KEY_SIZE])
{
    if (b64 == NULL || out == NULL) {
        return -1;
    }
    size_t len = strlen(b64);
    uint8_t buf[3 * ((EHEM_SIM_KEY_SIZE / 3) + 2)]; /* room to detect overlong */
    size_t n = ehem_b64_std_decode(b64, len, buf, sizeof buf);
    if (n != EHEM_SIM_KEY_SIZE) {
        return -1;
    }
    memcpy(out, buf, EHEM_SIM_KEY_SIZE);
    return 0;
}

/* --- compact HS256 JWT ---------------------------------------------------- */

int ehem_sim_jwt_build(const ehem_json *claims,
                       const uint8_t key[EHEM_SIM_KEY_SIZE], char **out_jwt)
{
    if (claims == NULL || key == NULL || out_jwt == NULL) {
        return -1;
    }
    int ret = -1;
    char *payload = ehem_json_print(claims);
    char *hseg = NULL, *pseg = NULL, *token = NULL;
    if (payload == NULL) {
        return -1;
    }

    size_t hlen = ehem_b64url_encoded_len(sizeof SIM_JWT_HEADER - 1);
    size_t plen = ehem_b64url_encoded_len(strlen(payload));
    hseg = malloc(hlen + 1);
    pseg = malloc(plen + 1);
    if (hseg == NULL || pseg == NULL) {
        goto out;
    }
    if (ehem_b64url_encode((const uint8_t *)SIM_JWT_HEADER,
                           sizeof SIM_JWT_HEADER - 1, hseg, hlen + 1) ==
            (size_t)-1 ||
        ehem_b64url_encode((const uint8_t *)payload, strlen(payload),
                           pseg, plen + 1) == (size_t)-1) {
        goto out;
    }

    /* signature over "<h>.<p>" */
    size_t siglen = ehem_b64url_encoded_len(EHEM_SHA256_SIZE);
    token = malloc(hlen + 1 + plen + 1 + siglen + 1);
    if (token == NULL) {
        goto out;
    }
    sprintf(token, "%s.%s", hseg, pseg);

    uint8_t tag[EHEM_SHA256_SIZE];
    if (ehem_hmac_sha256(key, EHEM_SIM_KEY_SIZE,
                         (const uint8_t *)token, hlen + 1 + plen,
                         tag) != EHEM_OK) {
        goto out;
    }
    token[hlen + 1 + plen] = '.';
    if (ehem_b64url_encode(tag, sizeof tag,
                           token + hlen + 1 + plen + 1, siglen + 1) ==
        (size_t)-1) {
        goto out;
    }
    *out_jwt = token;
    token = NULL;
    ret = 0;
out:
    free(hseg);
    free(pseg);
    free(token);
    ehem_json_string_free(payload);
    return ret;
}

/* Split "h.p.s"; returns 0 and the three segment spans on success. */
static int jwt_split(const char *jwt, const char **p, size_t *plen,
                     const char **s, size_t *slen, size_t *signed_len)
{
    const char *dot1 = strchr(jwt, '.');
    if (dot1 == NULL) {
        return -1;
    }
    const char *dot2 = strchr(dot1 + 1, '.');
    if (dot2 == NULL || strchr(dot2 + 1, '.') != NULL) {
        return -1;
    }
    *p = dot1 + 1;
    *plen = (size_t)(dot2 - dot1 - 1);
    *s = dot2 + 1;
    *slen = strlen(dot2 + 1);
    *signed_len = (size_t)(dot2 - jwt);
    return 0;
}

static ehem_json *jwt_payload_parse(const char *pseg, size_t plen)
{
    uint8_t *buf = malloc(plen + 1);
    if (buf == NULL) {
        return NULL;
    }
    size_t n = ehem_b64url_decode(pseg, plen, buf, plen + 1);
    ehem_json *doc = NULL;
    if (n != (size_t)-1) {
        doc = ehem_json_parse((const char *)buf, n);
    }
    free(buf);
    return doc;
}

ehem_json *ehem_sim_jwt_open(const char *jwt,
                             const uint8_t key[EHEM_SIM_KEY_SIZE])
{
    const char *pseg, *sseg;
    size_t plen, slen, signed_len;
    if (jwt == NULL || key == NULL ||
        jwt_split(jwt, &pseg, &plen, &sseg, &slen, &signed_len) != 0) {
        return NULL;
    }

    uint8_t got[EHEM_SHA256_SIZE + 4], want[EHEM_SHA256_SIZE];
    if (ehem_b64url_decode(sseg, slen, got, sizeof got) != EHEM_SHA256_SIZE) {
        return NULL;
    }
    if (ehem_hmac_sha256(key, EHEM_SIM_KEY_SIZE,
                         (const uint8_t *)jwt, signed_len, want) != EHEM_OK ||
        memcmp(got, want, EHEM_SHA256_SIZE) != 0) {
        return NULL;
    }
    return jwt_payload_parse(pseg, plen);
}

ehem_json *ehem_sim_jwt_peek(const char *jwt)
{
    const char *pseg, *sseg;
    size_t plen, slen, signed_len;
    if (jwt == NULL ||
        jwt_split(jwt, &pseg, &plen, &sseg, &slen, &signed_len) != 0) {
        return NULL;
    }
    return jwt_payload_parse(pseg, plen);
}

/* --- scheme A ------------------------------------------------------------- */

/* K = HMAC-SHA256(key = ecdh, msg = jti) — fw api_auth.c:1527/1744. */
static int scheme_a_key(const uint8_t jti[EHEM_SIM_KEY_SIZE],
                        const uint8_t ecdh[EHEM_SIM_KEY_SIZE],
                        uint8_t k_out[EHEM_SHA256_SIZE])
{
    return ehem_hmac_sha256(ecdh, EHEM_SIM_KEY_SIZE,
                            jti, EHEM_SIM_KEY_SIZE, k_out) == EHEM_OK ? 0 : -1;
}

int ehem_sim_scheme_a_encrypt(const char *scope,
                              const uint8_t jti[EHEM_SIM_KEY_SIZE],
                              const uint8_t ecdh[EHEM_SIM_KEY_SIZE],
                              char **out_value)
{
    if (scope == NULL || jti == NULL || ecdh == NULL || out_value == NULL) {
        return -1;
    }
    int ret = -1;
    size_t scope_len = strlen(scope);
    /* fw: ((len/16)+1)*16 — always at least one pad byte. */
    size_t padded = ((scope_len / EHEM_AES_BLOCK_SIZE) + 1) *
                    EHEM_AES_BLOCK_SIZE;
    size_t blob_len = padded + EHEM_SHA256_SIZE;

    uint8_t k[EHEM_SHA256_SIZE];
    uint8_t *blob = malloc(blob_len);
    char *value = NULL;
    if (blob == NULL || scheme_a_key(jti, ecdh, k) != 0) {
        goto out;
    }

    memcpy(blob, scope, scope_len);
    memset(blob + scope_len, (int)(padded - scope_len), padded - scope_len);
    if (ehem_aes128_cbc_encrypt(k, k + EHEM_AES128_KEY_SIZE,
                                blob, padded, blob) != EHEM_OK) {
        goto out;
    }
    if (ehem_hmac_sha256(k, sizeof k, (const uint8_t *)scope, scope_len,
                         blob + padded) != EHEM_OK) {
        goto out;
    }

    size_t b64cap = ehem_b64_std_encoded_len(blob_len) + 1;
    value = malloc(1 + b64cap);
    if (value == NULL) {
        goto out;
    }
    value[0] = 'A';
    if (ehem_b64_std_encode(blob, blob_len, value + 1, b64cap) == (size_t)-1) {
        goto out;
    }
    *out_value = value;
    value = NULL;
    ret = 0;
out:
    free(blob);
    free(value);
    ehem_zeroize(k, sizeof k);
    return ret;
}

int ehem_sim_scheme_a_decrypt(const char *value,
                              const uint8_t jti[EHEM_SIM_KEY_SIZE],
                              const uint8_t ecdh[EHEM_SIM_KEY_SIZE],
                              char **out_scope)
{
    if (value == NULL || jti == NULL || ecdh == NULL || out_scope == NULL ||
        value[0] != 'A') {
        return -1;
    }
    int ret = -1;
    size_t b64len = strlen(value + 1);
    uint8_t k[EHEM_SHA256_SIZE], tag[EHEM_SHA256_SIZE];
    uint8_t *blob = malloc(b64len + 1);
    char *scope = NULL;
    if (blob == NULL) {
        return -1;
    }

    size_t blob_len = ehem_b64_std_decode(value + 1, b64len, blob, b64len + 1);
    if (blob_len == (size_t)-1 ||
        blob_len < EHEM_AES_BLOCK_SIZE + EHEM_SHA256_SIZE ||
        (blob_len - EHEM_SHA256_SIZE) % EHEM_AES_BLOCK_SIZE != 0 ||
        scheme_a_key(jti, ecdh, k) != 0) {
        goto out;
    }
    size_t ct_len = blob_len - EHEM_SHA256_SIZE;

    if (ehem_aes128_cbc_decrypt(k, k + EHEM_AES128_KEY_SIZE,
                                blob, ct_len, blob) != EHEM_OK) {
        goto out;
    }

    /* fw pad check: last byte in 1..16 and <= ct_len (api_auth.c:1770). */
    uint8_t pad = blob[ct_len - 1];
    if (pad == 0 || pad > EHEM_AES_BLOCK_SIZE || pad > ct_len) {
        goto out;
    }
    size_t scope_len = ct_len - pad;

    if (ehem_hmac_sha256(k, sizeof k, blob, scope_len, tag) != EHEM_OK ||
        memcmp(tag, blob + ct_len, EHEM_SHA256_SIZE) != 0) {
        goto out;
    }

    scope = malloc(scope_len + 1);
    if (scope == NULL) {
        goto out;
    }
    memcpy(scope, blob, scope_len);
    scope[scope_len] = '\0';
    *out_scope = scope;
    scope = NULL;
    ret = 0;
out:
    free(blob);
    free(scope);
    ehem_zeroize(k, sizeof k);
    return ret;
}
