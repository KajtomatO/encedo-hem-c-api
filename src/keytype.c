/*
 * keytype.c — client-side key-type classifier: device type strings → typed
 * metadata (family, mode/role flags, wire sizes).
 *
 * implements: REQ-KEY-006
 *
 * Pure string processing — no network, no allocation, no crypto backend. The
 * vocabulary is the device's own (encedo-hem-api-doc keymgmt/create.md +
 * get.md bare names; the comma flag-set form observed live on fw v1.2.2).
 * Size facts mirror what the other bindings put on the wire: ehem_key_get
 * exports NIST-EC public keys as COMPRESSED X9.63 points (1 + ⌈bits/8⌉ bytes)
 * and ECDSA signatures from /api/crypto/exdsa/sign are DER (variable length);
 * Ed25519/Ed448 and ML-DSA signatures are fixed-size raw bytes.
 */
#include <string.h>

#include "ehem/keymgmt.h"

/* One family's static facts. */
typedef struct family_info {
    const char     *token;        /* device vocabulary name (also display name) */
    ehem_key_family family;
    size_t          pubkey_len;
    size_t          sig_max_len;
    int             sig_der;
} family_info;

/*
 * DER ECDSA-Sig-Value maxima: SEQUENCE of two INTEGERs of up to ⌈bits/8⌉+1
 * bytes each (a possible 0x00 sign pad), plus headers — 72/104 for P-256/P-384
 * (short-form length) and 141 for P-521 (long-form length byte). ML-DSA sizes
 * are FIPS 204; ML-KEM encapsulation-key sizes are FIPS 203.
 */
static const family_info FAMILIES[] = {
    { "SECP256R1",  EHEM_KEY_FAMILY_SECP256R1,     33,   72, 1 },
    { "SECP384R1",  EHEM_KEY_FAMILY_SECP384R1,     49,  104, 1 },
    { "SECP521R1",  EHEM_KEY_FAMILY_SECP521R1,     67,  141, 1 },
    { "SECP256K1",  EHEM_KEY_FAMILY_SECP256K1,     33,   72, 1 },
    { "ED25519",    EHEM_KEY_FAMILY_ED25519,       32,   64, 0 },
    { "ED448",      EHEM_KEY_FAMILY_ED448,         57,  114, 0 },
    { "CURVE25519", EHEM_KEY_FAMILY_CURVE25519,    32,    0, 0 },
    { "CURVE448",   EHEM_KEY_FAMILY_CURVE448,      56,    0, 0 },
    { "AES128",     EHEM_KEY_FAMILY_AES128,         0,    0, 0 },
    { "AES192",     EHEM_KEY_FAMILY_AES192,         0,    0, 0 },
    { "AES256",     EHEM_KEY_FAMILY_AES256,         0,    0, 0 },
    { "SHA2-256",   EHEM_KEY_FAMILY_HMAC_SHA2_256,  0,    0, 0 },
    { "SHA2-384",   EHEM_KEY_FAMILY_HMAC_SHA2_384,  0,    0, 0 },
    { "SHA2-512",   EHEM_KEY_FAMILY_HMAC_SHA2_512,  0,    0, 0 },
    { "SHA3-256",   EHEM_KEY_FAMILY_HMAC_SHA3_256,  0,    0, 0 },
    { "SHA3-384",   EHEM_KEY_FAMILY_HMAC_SHA3_384,  0,    0, 0 },
    { "SHA3-512",   EHEM_KEY_FAMILY_HMAC_SHA3_512,  0,    0, 0 },
    { "MLKEM512",   EHEM_KEY_FAMILY_MLKEM512,     800,    0, 0 },
    { "MLKEM768",   EHEM_KEY_FAMILY_MLKEM768,    1184,    0, 0 },
    { "MLKEM1024",  EHEM_KEY_FAMILY_MLKEM1024,   1568,    0, 0 },
    { "MLDSA44",    EHEM_KEY_FAMILY_MLDSA44,     1312, 2420, 0 },
    { "MLDSA65",    EHEM_KEY_FAMILY_MLDSA65,     1952, 3309, 0 },
    { "MLDSA87",    EHEM_KEY_FAMILY_MLDSA87,     2592, 4627, 0 },
    /* Generic DER containers: material is variable-length (details.der). */
    { "CERT",       EHEM_KEY_FAMILY_CERT,           0,    0, 0 },
    { "DER_PKEY",   EHEM_KEY_FAMILY_DER_PKEY,       0,    0, 0 },
};
#define FAMILY_COUNT (sizeof FAMILIES / sizeof FAMILIES[0])

static int tok_is(const char *tok, size_t len, const char *lit)
{
    return strlen(lit) == len && memcmp(tok, lit, len) == 0;
}

static const family_info *family_lookup(ehem_key_family family)
{
    for (size_t i = 0; i < FAMILY_COUNT; i++) {
        if (FAMILIES[i].family == family) {
            return &FAMILIES[i];
        }
    }
    return NULL;
}

ehem_rc ehem_key_type_parse(const char *type, ehem_key_type_info *out)
{
    if (type == NULL || out == NULL) {
        if (out != NULL) {
            memset(out, 0, sizeof *out);
        }
        return EHEM_ERR_ARG;
    }

    memset(out, 0, sizeof *out);

    const char *p = type;
    while (*p != '\0') {
        const char *comma = strchr(p, ',');
        size_t len = (comma != NULL) ? (size_t)(comma - p) : strlen(p);

        if (tok_is(p, len, "ExDSA")) {
            out->modes |= EHEM_KEY_MODE_EXDSA;
        } else if (tok_is(p, len, "ECDH")) {
            out->modes |= EHEM_KEY_MODE_ECDH;
        } else if (tok_is(p, len, "ATT")) {
            out->roles |= EHEM_KEY_ROLE_ATT;
        } else if (tok_is(p, len, "PKEY")) {
            out->roles |= EHEM_KEY_ROLE_PKEY;
        } else if (tok_is(p, len, "CERT")) {
            out->roles |= EHEM_KEY_ROLE_CERT;
        } else if (tok_is(p, len, "GENERIC_DER")) {
            out->roles |= EHEM_KEY_ROLE_GENERIC_DER;
        } else if (out->family == EHEM_KEY_FAMILY_UNKNOWN) {
            for (size_t i = 0; i < FAMILY_COUNT; i++) {
                if (tok_is(p, len, FAMILIES[i].token)) {
                    out->family = FAMILIES[i].family;
                    break;
                }
            }
            /* Unrecognized token: skipped (tolerant parsing, REQ-KEY-006). */
        }

        p = (comma != NULL) ? comma + 1 : p + len;
    }

    /*
     * DER containers name themselves through role tokens: the flag-set form is
     * "CERT,GENERIC_DER" / "PKEY,GENERIC_DER" (and the get form is the bare
     * "CERT" / "DER_PKEY", handled by the table). Resolved after the walk so
     * token order cannot matter.
     */
    if (out->family == EHEM_KEY_FAMILY_UNKNOWN) {
        if (out->roles & EHEM_KEY_ROLE_CERT) {
            out->family = EHEM_KEY_FAMILY_CERT;
        } else if (out->roles & EHEM_KEY_ROLE_GENERIC_DER) {
            out->family = EHEM_KEY_FAMILY_DER_PKEY;
        }
    }

    const family_info *fi = family_lookup(out->family);
    if (fi != NULL) {
        out->pubkey_len  = fi->pubkey_len;
        out->sig_max_len = fi->sig_max_len;
        out->sig_der     = fi->sig_der;
    }
    return EHEM_OK;
}

const char *ehem_key_family_str(ehem_key_family family)
{
    const family_info *fi = family_lookup(family);
    return (fi != NULL && family != EHEM_KEY_FAMILY_UNKNOWN) ? fi->token
                                                             : "unknown";
}
